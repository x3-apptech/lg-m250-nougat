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

#include "error.h"
#include "thread.h"

#if !THREAD_PTHREAD_RECURSIVE_MUTEX && \
    !THREAD_PTHREAD_RECURSIVE_MUTEX_NP
#define FAKE_RECURSIVE_MUTEX 1
#else
#define FAKE_RECURSIVE_MUTEX 0
#endif

typedef struct mutex {
    pthread_mutex_t id;
#if FAKE_RECURSIVE_MUTEX
    int recursive;
    pthread_t owner;
#endif
} Mutex_t;

Mutex_t *CreateMutex (void)
{
    Mutex_t *mutex;
    pthread_mutexattr_t attr;

    /* Allocate the structure */
    mutex = (Mutex_t *)calloc(1, sizeof(*mutex));
    if ( mutex ) {
        pthread_mutexattr_init(&attr);
#if THREAD_PTHREAD_RECURSIVE_MUTEX
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
#elif THREAD_PTHREAD_RECURSIVE_MUTEX_NP
        pthread_mutexattr_setkind_np(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
#else
        /* No extra attributes necessary */
#endif
        if ( pthread_mutex_init(&mutex->id, &attr) != 0 ) {
            SetError("pthread_mutex_init() failed");
            free(mutex);
            mutex = NULL;
        }
    } else {
        OutOfMemory();
    }
    return(mutex);
}

void DestroyMutex(Mutex_t *mutex)
{
    if ( mutex ) {
        pthread_mutex_destroy(&mutex->id);
        free(mutex);
    }
}

/* Lock the mutex */
int mutexP(Mutex_t *mutex)
{
    int retval;
#if FAKE_RECURSIVE_MUTEX
    pthread_t this_thread;
#endif

    if ( mutex == NULL ) {
        SetError("Passed a NULL mutex");
        return -1;
    }

    retval = 0;
#if FAKE_RECURSIVE_MUTEX
    this_thread = pthread_self();
    if ( mutex->owner == this_thread ) {
        ++mutex->recursive;
    } else {
        /* The order of operations is important.
           We set the locking thread id after we obtain the lock
           so unlocks from other threads will fail.
        */
        if ( pthread_mutex_lock(&mutex->id) == 0 ) {
            mutex->owner = this_thread;
            mutex->recursive = 0;
        } else {
            SetError("pthread_mutex_lock() failed");
            retval = -1;
        }
    }
#else
    if ( pthread_mutex_lock(&mutex->id) < 0 ) {
        SetError("pthread_mutex_lock() failed");
        retval = -1;
    }
#endif
    return retval;
}

int mutexV(Mutex_t *mutex)
{
    int retval;

    if ( mutex == NULL ) {
        SetError("Passed a NULL mutex");
        return -1;
    }

    retval = 0;
#if FAKE_RECURSIVE_MUTEX
    /* We can only unlock the mutex if we own it */
    if ( pthread_self() == mutex->owner ) {
        if ( mutex->recursive ) {
            --mutex->recursive;
        } else {
            /* The order of operations is important.
               First reset the owner so another thread doesn't lock
               the mutex and set the ownership before we reset it,
               then release the lock semaphore.
             */
            mutex->owner = 0;
            pthread_mutex_unlock(&mutex->id);
        }
    } else {
        SetError("mutex not owned by this thread");
        retval = -1;
    }

#else
    if ( pthread_mutex_unlock(&mutex->id) < 0 ) {
        SetError("pthread_mutex_unlock() failed");
        retval = -1;
    }
#endif /* FAKE_RECURSIVE_MUTEX */

    return retval;
}
