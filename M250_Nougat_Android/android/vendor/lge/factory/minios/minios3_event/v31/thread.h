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

#ifndef _evt_thread_h
#define _evt_thread_h

/** @file thread.h
 *  Header for the SDL thread management routines
 *
 *  @note These are independent of the other SDL routines.
 */

#include "stdinc.h"
#include "error.h"
#include <pthread.h>

/* Thread synchronization primitives */
#include "error_c.h"

/* Set up for C function definitions, even when using C++ */
#ifdef __cplusplus
extern "C" {
#endif

/** The SDL thread structure, defined in thread.c */
typedef struct Thread {
    Uint32 threadid;
    pthread_t handle;
    int status;
    error_t errbuf;
    void *data;
} Thread_t;


/* This is the function called to run a thread */
extern void RunThread(void *data);

/** Create a thread */
/**
 *  We compile SDL into a DLL on OS/2. This means, that it's the DLL which
 *  creates a new thread for the calling process with the CreateThread()
 *  API. There is a problem with this, that only the RTL of the SDL.DLL will
 *  be initialized for those threads, and not the RTL of the calling application!
 *  To solve this, we make a little hack here.
 *  We'll always use the caller's _beginthread() and _endthread() APIs to
 *  start a new thread. This way, if it's the SDL.DLL which uses this API,
 *  then the RTL of SDL.DLL will be used to create the new thread, and if it's
 *  the application, then the RTL of the application will be used.
 *  So, in short:
 *  Always use the _beginthread() and _endthread() of the calling runtime library!
 */
extern Thread_t *CreateThread(int (*fn)(void *), void *data);

/** Get the 32-bit thread identifier for the current thread */
extern Uint32  ThreadID(void);

/** Get the 32-bit thread identifier for the specified thread,
 *  equivalent to ThreadID() if the specified thread is NULL.
 */
extern Uint32  GetThreadID(Thread_t *thread);

/** Wait for a thread to finish.
 *  The return code for the thread function is placed in the area
 *  pointed to by 'status', if 'status' is not NULL.
 */
extern void  WaitThread(Thread_t *thread, int *status);

/** Forcefully kill a thread without worrying about its state */
extern void  KillThread(Thread_t *thread);

/* Low-level thread funtions */

void *SYS_RunThread(void *data);
int SYS_CreateThread(Thread_t *thread, void *args);
void SYS_SetupThread(void);
Uint32 ThreadID(void);
void SYS_WaitThread(Thread_t *thread);
void SYS_KillThread(Thread_t *thread);

/* Set up for C function definitions, even when using C++ */
#ifdef __cplusplus
}
#endif

#endif /* _thread_h */
