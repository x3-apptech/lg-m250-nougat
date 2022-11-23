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
#include "config.h"

/* Useful functions and variables from events.c */
#include "events.h"

/* Start and stop the event processing loop */
extern int StartEventLoop(Uint32 flags);
extern void StopEventLoop(void);
extern void QuitInterrupt(void);

extern void Lock_EventThread(void);
extern void Unlock_EventThread(void);
extern Uint32 EventThreadID(void);

/* Event handler init routines */
extern int  AppActiveInit(void);
extern int  KeyboardInit(void);
extern int  MouseInit(void);
extern int  QuitInit(void);

/* Event handler quit routines */
extern void AppActiveQuit(void);
extern void KeyboardQuit(void);
extern void MouseQuit(void);
extern void QuitQuit(void);

/* The event filter function */
extern EventFilter EventOK;

/* The array of event processing states */
extern Uint8 ProcessEvents[NUMEVENTS];

/* Internal event queueing functions
   (from active.c, mouse.c, keyboard.c, quit.c, events.c)
 */
extern int PrivateMouseMotion(Uint8 buttonstate, Uint8 button, Sint16 x, Sint16 y);
extern int PrivateMouseButton(Uint8 buttonstate, Uint8 button,Sint16 x,Sint16 y);
extern int PrivateKeyboard(Uint8 state, keysym *key);
extern int PrivateResize(int w, int h);
extern int PrivateExpose(void);
extern int PrivateQuit(void);
extern int PrivateSysWMEvent(SysWMmsg *message);

/* Used to clamp the mouse coordinates separately from the video surface */
extern void SetMouseRange(int maxX, int maxY);

/* Used by the activity event handler to remove mouse focus */
extern void ResetMouse(void);

/* Used by the activity event handler to remove keyboard focus */
extern void ResetKeyboard(void);

/* Used by the event loop to queue pending keyboard repeat events */
extern void CheckKeyRepeat(void);

/* Used by the OS keyboard code to detect whether or not to do UNICODE */
#ifndef DEFAULT_UNICODE_TRANSLATION
#define DEFAULT_UNICODE_TRANSLATION 0    /* Default off because of overhead */
#endif
extern int TranslateUNICODE;
