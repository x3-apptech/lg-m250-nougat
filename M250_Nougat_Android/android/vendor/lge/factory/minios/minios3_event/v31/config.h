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

#ifndef _config_minimal_h
#define _config_minimal_h

#define __LINUX__    1

/* This is the minimal configuration that can be used to build SDL */

#include <stdarg.h>

/*
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef signed short int16_t;
typedef unsigned short uint16_t;
typedef signed int int32_t;
typedef unsigned int uint32_t;
typedef unsigned int size_t;
typedef unsigned long uintptr_t;
*/

#ifdef true
#undef true
#endif

#ifdef false
#undef false
#endif

#define true    1
#define false    0

/* Enable the dummy audio driver (src/audio/dummy/\*.c) */
#define AUDIO_DRIVER_ANDROID 1
//#define AUDIO_DISABLED    1
///#define AUDIO_DRIVER_OSS 1 // AUDIO_DRIVER_DUMMY    1

/* Enable the stub cdrom driver (src/cdrom/dummy/\*.c) */
#define CDROM_DISABLED    1

/* Enable the stub joystick driver (src/joystick/dummy/\*.c) */
#define JOYSTICK_DISABLED    1

/* Enable the stub shared object loader (src/loadso/dummy/\*.c) */
#define LOADSO_DISABLED    1

/*  Enable the stub thread support (src/thread/generic/\*.c) */
#define THREAD_PTHREAD    1
#define THREAD_PTHREAD_RECURSIVE_MUTEX 1
//#define THREADS_DISABLED    1

/* Enable the stub timer support (src/timer/dummy/\*.c) */
#define TIMER_UNIX 1 // TIMERS_DISABLED    1

/* Enable the dummy video driver (src/video/dummy/\*.c) */
//#define VIDEO_DRIVER_FBCON 0 // VIDEO_DRIVER_DUMMY    1
#define VIDEO_DRIVER_ANDROID 1 //

#define HAVE_STDIO_H 1
#define NO_MALLINFO 1
#define ANDROID_BUILD 1
#define HAVE_CLOCK_GETTIME 1
//#define INPUT_TSLIB 1


// from SDL.h
#define INIT_TIMER            0x00000001
#define INIT_EVENTTHREAD    0x01000000

#endif /* _config_minimal_h */
