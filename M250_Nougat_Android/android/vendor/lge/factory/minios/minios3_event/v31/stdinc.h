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

/** @file stdinc.h
 *  This is a general header that includes C language support
 */

#ifndef _stdinc_h
#define _stdinc_h

#include "config.h"


/** The number of elements in an array */
#define arraysize(array)    (sizeof(array)/sizeof(array[0]))
#define TABLESIZE(table)    arraysize(table)
#include "utils/Log.h"
#define SetError    ALOGE

/** @name Basic data types */
/*@{*/
typedef enum {
    FALSE = 0,
    TRUE  = 1
} Bool;

typedef int8_t        Sint8;
typedef uint8_t        Uint8;
typedef int16_t        Sint16;
typedef uint16_t    Uint16;
typedef int32_t        Sint32;
typedef uint32_t    Uint32;

#ifdef HAS_64BIT_TYPE
typedef int64_t        Sint64;
#ifndef SYMBIAN32_GCCE
typedef uint64_t    Uint64;
#endif
#else
/* This is really just a hack to prevent the compiler from complaining */
typedef struct {
    Uint32 hi;
    Uint32 lo;
} Uint64, Sint64;
#endif

/*@}*/

/** @name Make sure the types really have the right sizes */
/*@{*/
#define COMPILE_TIME_ASSERT(name, x)               \
       typedef int dummy_ ## name[(x) * 2 - 1]

COMPILE_TIME_ASSERT(uint8, sizeof(Uint8) == 1);
COMPILE_TIME_ASSERT(sint8, sizeof(Sint8) == 1);
COMPILE_TIME_ASSERT(uint16, sizeof(Uint16) == 2);
COMPILE_TIME_ASSERT(sint16, sizeof(Sint16) == 2);
COMPILE_TIME_ASSERT(uint32, sizeof(Uint32) == 4);
COMPILE_TIME_ASSERT(sint32, sizeof(Sint32) == 4);
COMPILE_TIME_ASSERT(uint64, sizeof(Uint64) == 8);
COMPILE_TIME_ASSERT(sint64, sizeof(Sint64) == 8);
/*@}*/

/** @name Enum Size Check
 *  Check to make sure enums are the size of ints, for structure packing.
 *  For both Watcom C/C++ and Borland C/C++ the compiler option that makes
 *  enums having the size of an int must be enabled.
 *  This is "-b" for Borland C/C++ and "-ei" for Watcom C/C++ (v11).
 */
/* Enable enums always int in CodeWarrior (for MPW use "-enum int") */
#ifdef __MWERKS__
#pragma enumsalwaysint on
#endif

typedef enum {
    DUMMY_ENUM_VALUE
} DUMMY_ENUM;

#ifndef __NDS__
COMPILE_TIME_ASSERT(enum, sizeof(DUMMY_ENUM) == sizeof(int));
#endif
/*@}*/

#endif /* _stdinc_h */
