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

/* Refresh event handling code for SDL */

#include "events.h"
#include "events_c.h"


/* This is global for eventloop.c */
int PrivateExpose(void)
{
    int posted;
    Event_t events[32];

    /* Pull out all old refresh events */
    PeepEvents(events, sizeof(events)/sizeof(events[0]),
                        GETEVENT, VIDEOEXPOSEMASK);

    /* Post the event, if desired */
    posted = 0;
    if ( ProcessEvents[VIDEOEXPOSE] == ENABLE ) {
        Event_t event;
        event.type = VIDEOEXPOSE;
        if ( (EventOK == NULL) || (*EventOK)(&event) ) {
            posted = 1;
            PushEvent(&event);
        }
    }
    return(posted);
}
