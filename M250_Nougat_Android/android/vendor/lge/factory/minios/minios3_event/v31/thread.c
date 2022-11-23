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

#define LOG_NDEBUG 0
#define LOG_TAG "EventLib"
#define LOG_TITLE "Thread"
#include "config.h"

/* System independent thread management routines for SDL */

#include "mutex.h"
#include "thread.h"
#include "error.h"
#include <cutils/log.h>

#define ARRAY_CHUNKSIZE    32
/* The array of threads currently active in the application
   (except the main thread)
   The manipulation of an array here is safer than using a linked list.
*/
static int maxthreads = 0;
static int numthreads = 0;
static Thread_t **Threads = NULL;
static Mutex_t *thread_lock = NULL;

int ThreadsInit(void)
{
    int retval;

    retval = 0;
    thread_lock = CreateMutex();
    if ( thread_lock == NULL ) {
        retval = -1;
    }
    return(retval);
}

/* This should never be called...
   If this is called by Quit(), we don't know whether or not we should
   clean up threads here.  If any threads are still running after this call,
   they will no longer have access to any per-thread data.
 */
void ThreadsQuit(void)
{
    Mutex_t *mutex;

    mutex = thread_lock;
    thread_lock = NULL;
    if ( mutex != NULL ) {
        DestroyMutex(mutex);
    }
}

/* Routines for manipulating the thread list */
static void AddThread(Thread_t *thread)
{
    /* WARNING:
       If the very first threads are created simultaneously, then
       there could be a race condition causing memory corruption.
       In practice, this isn't a problem because by definition there
       is only one thread running the first time this is called.
    */
    if ( !thread_lock ) {
        if ( ThreadsInit() < 0 ) {
            return;
        }
    }
    mutexP(thread_lock);

    /* Expand the list of threads, if necessary */
#ifdef DEBUG_THREADS
    ALOGD("Adding thread (%d already - %d max)\n", numthreads, maxthreads);
#endif
    if ( numthreads == maxthreads ) {
        Thread_t **threads;
        threads = (Thread_t **)realloc(Threads,
            (maxthreads+ARRAY_CHUNKSIZE)*(sizeof *threads));
        if ( threads == NULL ) {
            OutOfMemory();
            goto done;
        }
        maxthreads += ARRAY_CHUNKSIZE;
        Threads = threads;
    }
    Threads[numthreads++] = thread;
done:
    mutexV(thread_lock);
}

static void DelThread(Thread_t *thread)
{
    int i;

    if ( !thread_lock ) {
        return;
    }
    mutexP(thread_lock);
    for ( i=0; i<numthreads; ++i ) {
        if ( thread == Threads[i] ) {
            break;
        }
    }
    if ( i < numthreads ) {
        if ( --numthreads > 0 ) {
            while ( i < numthreads ) {
                Threads[i] = Threads[i+1];
                ++i;
            }
        } else {
            maxthreads = 0;
            free(Threads);
            Threads = NULL;
        }
#ifdef DEBUG_THREADS
        ALOGD("Deleting thread (%d left - %d max)\n", numthreads, maxthreads);
#endif
    }
    mutexV(thread_lock);

#if 0    /* There could be memory corruption if another thread is starting */
    if ( Threads == NULL ) {
        ThreadsQuit();
    }
#endif
}

/* The default (non-thread-safe) global error variable */
static error_t global_error;

/* Routine to get the thread-specific error variable */
error_t *GetErrBuf(void)
{
    error_t *errbuf;

    errbuf = &global_error;
    if ( Threads ) {
        int i;
        Uint32 this_thread;

        this_thread = ThreadID();
        mutexP(thread_lock);
        for ( i=0; i<numthreads; ++i ) {
            if ( this_thread == Threads[i]->threadid ) {
                errbuf = &Threads[i]->errbuf;
                break;
            }
        }
        mutexV(thread_lock);
    }
    return(errbuf);
}


/* Arguments and callback to setup and run the user thread function */
typedef struct {
    int ( *func)(void *);
    void *data;
    Thread_t *info;
    Sem_t *wait;
} thread_args;

void RunThread(void *data)
{
    thread_args *args;
    int ( *userfunc)(void *);
    void *userdata;
    int *statusloc;

    //ALOGI("RunThread");
    /* Perform any system-dependent setup
       - this function cannot fail, and cannot use SetError()
     */
    SYS_SetupThread();

    /* Get the thread id */
    args = (thread_args *)data;
    args->info->threadid = ThreadID();

    /* Figure out what function to run */
    userfunc = args->func;
    userdata = args->data;
    statusloc = &args->info->status;

    /* Wake up the parent thread */
    ALOGD("RunThread: fn[0x%x]: Wake up the parent thread\n", userfunc);
    SemPost(args->wait);

    /* Run the function */
    *statusloc = userfunc(userdata);
}

Thread_t *  CreateThread(int ( *fn)(void *), void *data)
{
    Thread_t *thread;
    thread_args *args;
    int ret;

    //ALOGI("CreateThread");
//printf("sehkim: CreateThread: fn[0x%x]:\n", fn);
    /* Allocate memory for the thread info structure */
    thread = (Thread_t *)malloc(sizeof(*thread));
    if ( thread == NULL ) {
        OutOfMemory();
        return(NULL);
    }
    memset(thread, 0, (sizeof *thread));
    thread->status = -1;

    /* Set up the arguments for the thread */
    args = (thread_args *)malloc(sizeof(*args));
    if ( args == NULL ) {
        OutOfMemory();
        free(thread);
        return(NULL);
    }
    args->func = fn;
    args->data = data;
    args->info = thread;
    args->wait = CreateSemaphore(0);
    if ( args->wait == NULL ) {
        free(thread);
        free(args);
        return(NULL);
    }

    /* Add the thread to the list of available threads */
    AddThread(thread);

    /* Create the thread and go! */
    ret = SYS_CreateThread(thread, args);
//printf("sehkim: CreateThread: fn[0x%x]: SYS_CreateThread ret[%d]\n",fn, ret );
    if ( ret >= 0 ) {
//printf("sehkim: CreateThread: fn[0x%x]: Wait for Child. Wait[%d]\n", fn, args->wait);
        /* Wait for the thread function to use arguments */
        SemWait(args->wait);
//printf("sehkim: CreateThread: fn[0x%x]: Wait Finished  for Child. Wait[%d]\n", fn, args->wait);
    } else {
        /* Oops, failed.  Gotta free everything */
        DelThread(thread);
        free(thread);
        thread = NULL;
    }
    DestroySemaphore(args->wait);
    free(args);

//printf("sehkim: CreateThread: fn[0x%x] ret[0x%x]\n", fn, thread);
    /* Everything is running now */
    return(thread);
}

void WaitThread(Thread_t *thread, int *status)
{
    //ALOGI("WaitThread");
    if ( thread ) {
        SYS_WaitThread(thread);
        if ( status ) {
            *status = thread->status;
        }
        DelThread(thread);
        free(thread);
    }
}

Uint32 GetThreadID(Thread_t *thread)
{
    Uint32 id;

    //ALOGI("GetThreadID");
    if ( thread ) {
        id = thread->threadid;
    } else {
        id = ThreadID();
    }
    return(id);
}

void KillThread(Thread_t *thread)
{
    //ALOGI("KillThread");
    if ( thread ) {
        SYS_KillThread(thread);
        WaitThread(thread, NULL);
    }
}
