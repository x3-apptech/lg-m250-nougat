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

/**
 *  @file active.h
 *  Include file for SDL application focus event handling
 */

#ifndef _active_h
#define _active_h

#include "stdinc.h"
#include "error.h"

/* Set up for C function definitions, even when using C++ */
#ifdef __cplusplus
extern "C" {
#endif

/** @name The available application states */
/*@{*/
#define APPMOUSEFOCUS    0x01        /**< The app has mouse coverage */
#define APPINPUTFOCUS    0x02        /**< The app has input focus */
#define APPACTIVE        0x04        /**< The application is active */
/*@}*/

/* Function prototypes */
/**
 * This function returns the current state of the application, which is a
 * bitwise combination of APPMOUSEFOCUS, APPINPUTFOCUS, and
 * APPACTIVE.  If APPACTIVE is set, then the user is able to
 * see your application, otherwise it has been iconified or disabled.
 */
extern  Uint8  GetAppState(void);


/* Ends C function definitions when using C++ */
#ifdef __cplusplus
}
#endif

#endif /* _active_h */
