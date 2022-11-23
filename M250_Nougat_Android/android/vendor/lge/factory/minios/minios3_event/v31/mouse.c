/*
    SDL - Simple DirectMedia Layer
    Copyright (C) 1997-2012 Sam Lantinga

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    Sam Lantinga
    slouken@libsdl.org
*/
//#include "SDL.h"
#include "config.h"

/* General mouse handling code for SDL */

#include "events.h"
#include "events_c.h"
#include "timer.h"
#include "error.h"

#include "minios3_displayinfo.h"
#include <cutils/log.h>

#define DELAY_CLICK_LONG    1000    // msec

struct ScreenInfo {
    int32_t width;
    int32_t height;
    int32_t stride;
    int32_t offset;
    int32_t BytesPerPixel;
} s_info;

/* These are static for our mouse handling code */
struct MouseBackupInfo {
    Sint16 MouseX;
    Sint16 MouseY;
    Sint16 DeltaX;
    Sint16 DeltaY;
} m_backup_info[MAX_POINTERS];

static Sint16 MouseMaxX = 0;
static Sint16 MouseMaxY = 0;
static Uint32 ButtonState = 0;
static Uint8 LongClickState = 0;

TimerID t = NULL;

/* Public functions */
int MouseInit(void)
{
    /* The mouse is at (0,0) */
    memset(m_backup_info, 0x00, (sizeof(struct MouseBackupInfo) * MAX_POINTERS));

    // Display information
    s_info.width = getDisplayWidth();
    s_info.height = getDisplayHeight();

    if(s_info.width > s_info.height) {
        int tmp = s_info.width;
        s_info.width = s_info.height;
        s_info.height = tmp;
    }

    s_info.stride = (s_info.width + 15) & ~15;
    s_info.offset = s_info.stride - s_info.width;
    s_info.BytesPerPixel = 32;
    ALOGD("[MouseInit] info.width = %d,info.height=%u,info.stride=%u,info.offset=%u",s_info.width,s_info.height,s_info.stride,s_info.offset) ;

    SetMouseRange(s_info.width, s_info.height);
    /* That's it! */
    return(0);
}
void MouseQuit(void)
{
}

/* We lost the mouse, so post button up messages for all pressed buttons */
void ResetMouse(void)
{
    Uint8 i;
    for ( i = 0; i < sizeof(ButtonState)*8; ++i ) {
        if ( ButtonState & BUTTON(i) ) {
            PrivateMouseButton(RELEASED, i, m_backup_info[i].MouseX, m_backup_info[i].MouseY);
        }
    }
}

static void ClipOffset(Sint16 *x, Sint16 *y)
{
    /* This clips absolute mouse coordinates when the apparent
       display surface is smaller than the real display surface.
     */
    if (s_info.offset)
    {
        *y -= s_info.offset/s_info.stride;
        *x -= (s_info.offset%s_info.stride)/s_info.BytesPerPixel;
    }
}

void SetMouseRange(int maxX, int maxY)
{
    MouseMaxX = (Sint16)maxX;
    MouseMaxY = (Sint16)maxY;
}

/* These are global for eventloop.c */
int PrivateMouseMotion(Uint8 buttonstate, Uint8 button, Sint16 x, Sint16 y)
{
    int posted;
    Uint16 X, Y;
    Sint16 Xrel;
    Sint16 Yrel;
    Sint16 coordinate;
    const char *rotation;

    /* Default buttonstate is the current one */
    if ( ! buttonstate ) {
        buttonstate = ButtonState;
    }

    Xrel = x;
    Yrel = y;

    /* Do we need to clip {x,y} ? */
    ClipOffset(&x, &y);

    /* Mouse coordinates range from 0 - width-1 and 0 - height-1 */
    if ( x < 0 )
        X = 0;
    else
    if ( x >= MouseMaxX )
        X = MouseMaxX-1;
    else
        X = (Uint16)x;

    if ( y < 0 )
        Y = 0;
    else
    if ( y >= MouseMaxY )
        Y = MouseMaxY-1;
    else
        Y = (Uint16)y;

    /* If not relative mode, generate relative motion from clamped X/Y.
       This prevents lots of extraneous large delta relative motion when
       the screen is windowed mode and the mouse is outside the window.
    */
    Xrel = X-m_backup_info[button].MouseX;
    Yrel = Y-m_backup_info[button].MouseY;

#if PERFORM_H2_PRJ
    /* Drop events that don't change state */
//    if( !Xrel && !Yrel ) {
        //ALOGD("Mouse event didn't change state - dropped!\n");
//        return(0);
//    }
#else
    if( !Xrel && !Yrel ) {
        return(0);
    }
#endif
    /* Update internal mouse state */
    ButtonState = buttonstate;
    m_backup_info[button].MouseX = X;
    m_backup_info[button].MouseY = Y;
    m_backup_info[button].DeltaX += Xrel;
    m_backup_info[button].DeltaY += Yrel;

    /* Post the event, if desired */
    posted = 0;
    if ( ProcessEvents[MOUSEMOTION] == ENABLE ) {
        Event_t event;
        memset(&event, 0, sizeof(event));
        event.type = MOUSEMOTION;
        event.motion.state = buttonstate;
        event.motion.button = button;
        event.motion.x = X;
        event.motion.y = Y;
        event.motion.xrel = Xrel;
        event.motion.yrel = Yrel;
        if ( (EventOK == NULL) || (*EventOK)(&event) ) {
            posted = 1;
            PushEvent(&event);
        }
    }
    return(posted);
}

static Uint32
longclick_callback(Uint32 interval, void *param)
{
    /* Save the long click state */
    LongClickState = 1;

    /* Simulate the long click event */
    PrivateMouseButton(PRESSED, 0 /* First finger */, m_backup_info[0].MouseX, m_backup_info[0].MouseY);

    /* The long click event occures just one time */
    if(t != NULL) {
        RemoveTimer(t);
        t = NULL;
    }

    LongClickState = 0;

    return(0);
}

static Uint8
long_click_timer_add(void)
{
    /*
     * If long click timer is expired,
     * callback function make event for long click.
     */
    if(t != NULL) {
        RemoveTimer(t);
        t = NULL;
    }

    LongClickState = 0;

    t = AddTimer(DELAY_CLICK_LONG, longclick_callback, (void*)1);
    if(!t) {
        ALOGD("Could not create SDL timer\n");
    }

    return(0);
}

static Uint8
long_click_timer_remove(void)
{
    /*
     * Change the state of long click to before
     * and remove SDL Timer for long click.
     */
    if(t != NULL) {
        RemoveTimer(t);
        t = NULL;
    }

    LongClickState = 0;

    return(0);
}

int PrivateMouseButton(Uint8 state, Uint8 button, Sint16 x, Sint16 y)
{
    Event_t event;
    int posted;
    Uint32 buttonstate;
    Sint16 coordinate;
    const char *rotation;

    memset(&event, 0, sizeof(event));

    /* Check parameters */
    ClipOffset(&x, &y);

    /* Mouse coordinates range from 0 - width-1 and 0 - height-1 */
    x = (x < 0) ? 0 : x;
    x = (x >= MouseMaxX) ? (MouseMaxX-1) : x;
    y = (y < 0) ? 0 : y;
    y = (y >= MouseMaxY) ? (MouseMaxY-1) : y;

    /* Figure out which event to perform */
    buttonstate = ButtonState;
    switch ( state ) {
        case PRESSED:
            if ((LongClickState == 1) && (button == 0)) {
                event.type = MOUSEBUTTONLONG;
            } else {
                event.type = MOUSEBUTTONDOWN;
                if(button == 0) {
                    long_click_timer_add();
                }
                buttonstate |= BUTTON(button);
            }
            break;
        case RELEASED:
            event.type = MOUSEBUTTONUP;
            if(button == 0) {
                long_click_timer_remove();
            }
            buttonstate &= ~BUTTON(button);
            break;
        default:
            /* Invalid state -- bail */
            return(0);
    }

    /* Update internal mouse state */
    ButtonState = buttonstate;
    m_backup_info[button].MouseX = x;
    m_backup_info[button].MouseY = y;

    /* Post the event, if desired */
    posted = 0;
    if ( ProcessEvents[event.type] == ENABLE ) {
        event.button.state = state;
        event.button.button = button;
        event.button.x = x;
        event.button.y = y;
        if ( (EventOK == NULL) || (*EventOK)(&event) ) {
            posted = 1;
            PushEvent(&event);
        }
    }
    return(posted);
}
