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

/* General quit handling code for SDL */

#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif

#include "events.h"
#include "events_c.h"


#ifdef HAVE_SIGNAL_H
static void HandleSIG(int sig)
{
    /* Reset the signal handler */
    signal(sig, HandleSIG);

    /* Signal a quit interrupt */
    PrivateQuit();
}
#endif /* HAVE_SIGNAL_H */

/* Public functions */
int QuitInit(void)
{
#ifdef HAVE_SIGACTION
    struct sigaction action;
    sigaction(SIGINT, NULL, &action);
#  ifdef HAVE_SA_SIGACTION
    if ( action.sa_handler == SIG_DFL && action.sa_sigaction == (void*)SIG_DFL ) {
#  else
    if ( action.sa_handler == SIG_DFL ) {
#  endif
        action.sa_handler = HandleSIG;
        sigaction(SIGINT, &action, NULL);
    }
    sigaction(SIGTERM, NULL, &action);
#  ifdef HAVE_SA_SIGACTION
    if ( action.sa_handler == SIG_DFL && action.sa_sigaction == (void*)SIG_DFL ) {
#  else
    if ( action.sa_handler == SIG_DFL ) {
#  endif
        action.sa_handler = HandleSIG;
        sigaction(SIGTERM, &action, NULL);
    }
#elif HAVE_SIGNAL_H
    void (*ohandler)(int);

    /* Both SIGINT and SIGTERM are translated into quit interrupts */
    ohandler = signal(SIGINT, HandleSIG);
    if ( ohandler != SIG_DFL )
        signal(SIGINT, ohandler);
    ohandler = signal(SIGTERM, HandleSIG);
    if ( ohandler != SIG_DFL )
        signal(SIGTERM, ohandler);
#endif /* HAVE_SIGNAL_H */

    /* That's it! */
    return(0);
}
void QuitQuit(void)
{
#ifdef HAVE_SIGACTION
    struct sigaction action;
    sigaction(SIGINT, NULL, &action);
    if ( action.sa_handler == HandleSIG ) {
        action.sa_handler = SIG_DFL;
        sigaction(SIGINT, &action, NULL);
    }
    sigaction(SIGTERM, NULL, &action);
    if ( action.sa_handler == HandleSIG ) {
        action.sa_handler = SIG_DFL;
        sigaction(SIGTERM, &action, NULL);
    }
#elif HAVE_SIGNAL_H
    void (*ohandler)(int);

    ohandler = signal(SIGINT, SIG_DFL);
    if ( ohandler != HandleSIG )
        signal(SIGINT, ohandler);
    ohandler = signal(SIGTERM, SIG_DFL);
    if ( ohandler != HandleSIG )
        signal(SIGTERM, ohandler);
#endif /* HAVE_SIGNAL_H */
}

/* This function returns 1 if it's okay to close the application window */
int PrivateQuit(void)
{
    int posted;

    posted = 0;
    if ( ProcessEvents[QUIT] == ENABLE ) {
        Event_t event;
        event.type = QUIT;
        if ( (EventOK == NULL) || (*EventOK)(&event) ) {
            posted = 1;
            PushEvent(&event);
        }
    }
    return(posted);
}
