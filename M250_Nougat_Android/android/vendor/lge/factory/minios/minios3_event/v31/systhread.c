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

#include <pthread.h>
#include <signal.h>

#include "thread.h"
#include "mutex.h"
//#include "systhread.h"

/* List of signals to mask in the subthreads */
static int sig_list[] = {
    SIGHUP, SIGINT, SIGQUIT, SIGPIPE, SIGALRM, SIGTERM, SIGCHLD, SIGWINCH,
    SIGVTALRM, SIGPROF, 0
};

#ifdef __RISCOS__
/* RISC OS needs to know the main thread for
 * it's timer and event processing. */
int riscos_using_threads = 0;
Uint32 riscos_main_thread = 0; /* Thread running events */
#endif

typedef struct {
    int ( *func)(void *);
    void *data;
    Thread_t *info;
    Sem_t *wait;
} thread_args;


void *SYS_RunThread(void *data)
{

    thread_args *_args;
    _args = (thread_args *) data;

    RunThread(data);

    ALOGD("SYS_RunThread: fn[0x%x]: exiting thread\n", _args->func);
    pthread_exit((void*)0);
    return((void *)0);        /* Prevent compiler warning */
}

int SYS_CreateThread(Thread_t *thread, void *args)
{
    pthread_attr_t type;

    thread_args *_args;
    _args = (thread_args *) args;
    ALOGD("SYS_CreateThread: fn[0x%x]:\n", _args->func);
    /* Set the thread attributes */
    if ( pthread_attr_init(&type) != 0 ) {
        SetError("Couldn't initialize pthread attributes");
        return(-1);
    }
    pthread_attr_setdetachstate(&type, PTHREAD_CREATE_JOINABLE);

    /* Create the thread and go! */
    if ( pthread_create(&thread->handle, &type, SYS_RunThread, args) != 0 ) {
        SetError("Not enough resources to create thread");
        ALOGE("SYS_CreateThread: Not enough resources to create thread\n");
        return(-1);
    }

#ifdef __RISCOS__
    if (riscos_using_threads == 0) {
        riscos_using_threads = 1;
        riscos_main_thread = ThreadID();
    }
#endif

    return(0);
}

void SYS_SetupThread(void)
{
    int i;
    sigset_t mask;

    /* Mask asynchronous signals for this thread */
    sigemptyset(&mask);
    for ( i=0; sig_list[i]; ++i ) {
        sigaddset(&mask, sig_list[i]);
    }
    pthread_sigmask(SIG_BLOCK, &mask, 0);

#ifdef PTHREAD_CANCEL_ASYNCHRONOUS
    /* Allow ourselves to be asynchronously cancelled */
    { int oldstate;
        pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldstate);
    }
#endif
}

/* WARNING:  This may not work for systems with 64-bit pid_t */
Uint32 ThreadID(void)
{
    return((Uint32)((size_t)pthread_self()));
}

void SYS_WaitThread(Thread_t *thread)
{
    pthread_join(thread->handle, 0);
}

void SYS_KillThread(Thread_t *thread)
{
#ifdef PTHREAD_CANCEL_ASYNCHRONOUS
    pthread_cancel(thread->handle);
#else
    pthread_kill(thread->handle, SIGKILL);
#endif
}
