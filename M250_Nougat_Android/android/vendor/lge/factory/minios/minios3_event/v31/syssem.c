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
#include <semaphore.h>
#include <errno.h>
#include <sys/time.h>

#include "error.h"
#include "thread.h"
#include "timer.h"
#include "mutex.h"

/* Wrapper around POSIX 1003.1b semaphores */
Sem_t sem;

/* Create a semaphore, initialized with value */
Sem_t *CreateSemaphore(Uint32 initial_value)
{
    Sem_t *sem = (Sem_t *) malloc(sizeof(sem));
    if ( sem ) {
        if ( sem_init(&sem->sem, 0, initial_value) < 0 ) {
            SetError("sem_init() failed");
            free(sem);
            sem = NULL;
        }
    } else {
        OutOfMemory();
    }
    return sem;
}

void DestroySemaphore(Sem_t *sem)
{
    if ( sem ) {
        sem_destroy(&sem->sem);
        free(sem);
    }
}

int SemTryWait(Sem_t *sem)
{
    int retval;

    if ( ! sem ) {
        SetError("Passed a NULL semaphore");
        return -1;
    }
    retval = MUTEX_TIMEDOUT;
    if ( sem_trywait(&sem->sem) == 0 ) {
        retval = 0;
    }
    return retval;
}

int SemWait(Sem_t *sem)
{
    int retval;

    if ( ! sem ) {
        SetError("Passed a NULL semaphore");
        return -1;
    }

    while ( ((retval = sem_wait(&sem->sem)) == -1) && (errno == EINTR) ) {}
    if ( retval < 0 ) {
        SetError("sem_wait() failed");
    }
    return retval;
}

int SemWaitTimeout(Sem_t *sem, Uint32 timeout)
{
    int retval;
#ifdef HAVE_SEM_TIMEDWAIT
    struct timeval now;
    struct timespec ts_timeout;
#else
    Uint32 end;
#endif

    if ( ! sem ) {
        SetError("Passed a NULL semaphore");
        return -1;
    }

    /* Try the easy cases first */
    if ( timeout == 0 ) {
        return SemTryWait(sem);
    }
    if ( timeout == MUTEX_MAXWAIT ) {
        return SemWait(sem);
    }

#ifdef HAVE_SEM_TIMEDWAIT
    /* Setup the timeout. sem_timedwait doesn't wait for
     * a lapse of time, but until we reach a certain time.
     * This time is now plus the timeout.
     */
    gettimeofday(&now, NULL);

    /* Add our timeout to current time */
    now.tv_usec += (timeout % 1000) * 1000;
    now.tv_sec += timeout / 1000;

    /* Wrap the second if needed */
    if ( now.tv_usec >= 1000000 ) {
        now.tv_usec -= 1000000;
        now.tv_sec ++;
    }

    /* Convert to timespec */
    ts_timeout.tv_sec = now.tv_sec;
    ts_timeout.tv_nsec = now.tv_usec * 1000;

    /* Wait. */
    do
        retval = sem_timedwait(&sem->sem, &ts_timeout);
    while (retval == -1 && errno == EINTR);

    if (retval == -1)
        SetError(strerror(errno));
#else
    end = GetTicks() + timeout;
    while ((retval = SemTryWait(sem)) == MUTEX_TIMEDOUT) {
        if ((GetTicks() - end) >= 0) {
            break;
        }
        Delay(0);
    }
#endif /* HAVE_SEM_TIMEDWAIT */

    return retval;
}

Uint32 SemValue(Sem_t *sem)
{
    int ret = 0;
    if ( sem ) {
        sem_getvalue(&sem->sem, &ret);
        if ( ret < 0 ) {
            ret = 0;
        }
    }
    return (Uint32)ret;
}

int SemPost(Sem_t *sem)
{
    int retval;

    if ( ! sem ) {
        SetError("Passed a NULL semaphore");
        return -1;
    }

    retval = sem_post(&sem->sem);
    if ( retval < 0 ) {
        SetError("sem_post() failed");
    }
    return retval;
}
