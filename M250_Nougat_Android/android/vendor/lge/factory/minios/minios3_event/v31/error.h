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
 *  @file error.h
 *  Simple error message routines for SDL
 */

#ifndef _error_h
#define _error_h

#include "stdinc.h"

/**
 *  @name Public functions
 */
/*@{*/
//void  SetError(const char *fmt, ...);
char *  GetError(void);
void  ClearError(void);
/*@}*/

/**
 *  @name Private functions
 *  @internal Private error message function - used internally
 */
/*@{*/
#define OutOfMemory()    Error(ENOMEM)
#define Unsupported()    Error(UNSUPPORTED)
typedef enum {
    EENOMEM,
    EFREAD,
    EFWRITE,
    EFSEEK,
    UNSUPPORTED,
    LASTERROR
} errorcode;
void  Error(errorcode code);
/*@}*/

#endif /* _error_h */
