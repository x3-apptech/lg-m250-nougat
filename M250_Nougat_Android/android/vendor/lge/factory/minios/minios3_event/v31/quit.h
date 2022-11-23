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

/** @file quit.h
 *  Include file for SDL quit event handling
 */

#ifndef _quit_h
#define _quit_h

#include "stdinc.h"
#include "error.h"

/** @file quit.h
 *  An QUITEVENT is generated when the user tries to close the application
 *  window.  If it is ignored or filtered out, the window will remain open.
 *  If it is not ignored or filtered, it is queued normally and the window
 *  is allowed to close.  When the window is closed, screen updates will
 *  complete, but have no effect.
 *
 *  Init() installs signal handlers for SIGINT (keyboard interrupt)
 *  and SIGTERM (system termination request), if handlers do not already
 *  exist, that generate QUITEVENT events as well.  There is no way
 *  to determine the cause of an QUITEVENT, but setting a signal
 *  handler in your application will override the default generation of
 *  quit events for that signal.
 */

/** @file quit.h
 *  There are no functions directly affecting the quit event
 */

#define QuitRequested() \
        (PumpEvents(), PeepEvents(NULL,0,PEEKEVENT,QUITMASK))

#endif /* _quit_h */
