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

/** @file syswm.h
 *  Include file for SDL custom system window manager hooks
 */

#ifndef _syswm_h
#define _syswm_h

#include "stdinc.h"
#include "error.h"
//#include "version.h"

/* Set up for C function definitions, even when using C++ */
#ifdef __cplusplus
extern "C" {
#endif

/** @file syswm.h
 *  Your application has access to a special type of event 'SYSWMEVENT',
 *  which contains window-manager specific information and arrives whenever
 *  an unhandled window event occurs.  This event is ignored by default, but
 *  you can enable it with EventState()
 */
#ifdef PROTOTYPES_ONLY
struct SysWMinfo;
typedef struct SysWMinfo SysWMinfo;
#else

/* This is the structure for custom window manager events */
#if defined(VIDEO_DRIVER_X11)
#if defined(__APPLE__) && defined(__MACH__)
/* conflicts with Quickdraw.h */
#define Cursor X11Cursor
#endif

#include <X11/Xlib.h>
#include <X11/Xatom.h>

/** These are the various supported subsystems under UNIX */
typedef enum {
    SYSWM_X11
} SYSWM_TYPE;

/** The UNIX custom event structure */
struct SysWMmsg {
    version version;
    SYSWM_TYPE subsystem;
    union {
        XEvent xevent;
    } event;
};

/** The UNIX custom window manager information structure.
 *  When this structure is returned, it holds information about which
 *  low level system it is using, and will be one of SYSWM_TYPE.
 */
typedef struct SysWMinfo {
    version version;
    SYSWM_TYPE subsystem;
    union {
        struct {
            Display *display;    /**< The X11 display */
            Window window;        /**< The X11 display window */
        /** These locking functions should be called around
                 *  any X11 functions using the display variable,
                 *  but not the gfxdisplay variable.
                 *  They lock the event thread, so should not be
         *  called around event functions or from event filters.
         */
                /*@{*/
        void (*lock_func)(void);
        void (*unlock_func)(void);
                /*@}*/

        /** @name Introduced in SDL 1.0.2 */
                /*@{*/
            Window fswindow;    /**< The X11 fullscreen window */
            Window wmwindow;    /**< The X11 managed input window */
                /*@}*/

        /** @name Introduced in SDL 1.2.12 */
                /*@{*/
        Display *gfxdisplay;    /**< The X11 display to which rendering is done */
                /*@}*/
        } x11;
    } info;
} SysWMinfo;

#elif defined(VIDEO_DRIVER_NANOX)
#include <microwin/nano-X.h>

/** The generic custom event structure */
struct SysWMmsg {
    version version;
    int data;
};

/** The windows custom window manager information structure */
typedef struct SysWMinfo {
    version version ;
    GR_WINDOW_ID window ;    /* The display window */
} SysWMinfo;

#elif defined(VIDEO_DRIVER_WINDIB) || defined(VIDEO_DRIVER_DDRAW) || defined(VIDEO_DRIVER_GAPI)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/** The windows custom event structure */
struct SysWMmsg {
    version version;
    HWND hwnd;            /**< The window for the message */
    UINT msg;            /**< The type of message */
    WPARAM wParam;            /**< WORD message parameter */
    LPARAM lParam;            /**< LONG message parameter */
};

/** The windows custom window manager information structure */
typedef struct SysWMinfo {
    version version;
    HWND window;            /**< The Win32 display window */
    HGLRC hglrc;            /**< The OpenGL context, if any */
} SysWMinfo;

#elif defined(VIDEO_DRIVER_RISCOS)

/** RISC OS custom event structure */
struct SysWMmsg {
    version version;
    int eventCode;        /**< The window for the message */
    int pollBlock[64];
};

/** The RISC OS custom window manager information structure */
typedef struct SysWMinfo {
    version version;
    int wimpVersion;    /**< Wimp version running under */
    int taskHandle;     /**< The RISC OS task handle */
    int window;        /**< The RISC OS display window */
} SysWMinfo;

#elif defined(VIDEO_DRIVER_PHOTON)
#include <sys/neutrino.h>
#include <Ph.h>

/** The QNX custom event structure */
struct SysWMmsg {
    version version;
    int data;
};

/** The QNX custom window manager information structure */
typedef struct SysWMinfo {
    version version;
    int data;
} SysWMinfo;

#else

/** The generic custom event structure */
struct SysWMmsg {
    int version;
    int data;
};

/** The generic custom window manager information structure */
typedef struct SysWMinfo {
    int version;
    int data;
} SysWMinfo;

#endif /* video driver type */

#endif /* PROTOTYPES_ONLY */

/* Function prototypes */
/**
 * This function gives you custom hooks into the window manager information.
 * It fills the structure pointed to by 'info' with custom information and
 * returns 0 if the function is not implemented, 1 if the function is
 * implemented and no error occurred, and -1 if the version member of
 * the 'info' structure is not filled in or not supported.
 *
 * You typically use this function like this:
 * @code
 * SysWMinfo info;
 * VERSION(&info.version);
 * if ( GetWMInfo(&info) ) { ... }
 * @endcode
 */
extern  int  GetWMInfo(SysWMinfo *info);


/* Ends C function definitions when using C++ */
#ifdef __cplusplus
}
#endif

#endif /* _syswm_h */
