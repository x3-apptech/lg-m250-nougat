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

#ifndef _mutex_h
#define _mutex_h

/** @file mutex.h
 *  Functions to provide thread synchronization primitives
 *
 *  @note These are independent of the other SDL routines.
 */

#include "stdinc.h"
//#include "error.h"

/* Set up for C function definitions, even when using C++ */
#ifdef __cplusplus
extern "C" {
#endif

/** Synchronization functions which can time out return this value
 *  if they time out.
 */
#define MUTEX_TIMEDOUT    1

/** This is the timeout value which corresponds to never time out */
#define MUTEX_MAXWAIT    (~(Uint32)0)


/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/** @name Mutex functions                                        */ /*@{*/
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/** The SDL mutex structure, defined in mutex.c */
typedef struct mutex {
    pthread_mutex_t id;
} Mutex_t;

/** Create a mutex, initialized unlocked */
extern  Mutex_t *  CreateMutex(void);

#define LockMutex(m)    mutexP(m)
/** Lock the mutex
 *  @return 0, or -1 on error
 */
extern  int  mutexP(Mutex_t *mutex);

#define UnlockMutex(m)    mutexV(m)
/** Unlock the mutex
 *  @return 0, or -1 on error
 *
 *  It is an error to unlock a mutex that has not been locked by
 *  the current thread, and doing so results in undefined behavior.
 */
extern  int  mutexV(Mutex_t *mutex);

/** Destroy a mutex */
extern  void  DestroyMutex(Mutex_t *mutex);

/*@}*/

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/** @name Semaphore functions                                    */ /*@{*/
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <semaphore.h>
/** The SDL semaphore structure, defined in sem.c */
typedef struct semaphore {
        sem_t sem;
}Sem_t;

/** Create a semaphore, initialized with value, returns NULL on failure. */
extern  Sem_t *  CreateSemaphore(Uint32 initial_value);

/** Destroy a semaphore */
extern  void  DestroySemaphore(Sem_t *sem);

/**
 * This function suspends the calling thread until the semaphore pointed
 * to by sem has a positive count. It then atomically decreases the semaphore
 * count.
 */
extern  int  SemWait(Sem_t *sem);

/** Non-blocking variant of SemWait().
 *  @return 0 if the wait succeeds,
 *  MUTEX_TIMEDOUT if the wait would block, and -1 on error.
 */
extern  int  SemTryWait(Sem_t *sem);

/** Variant of SemWait() with a timeout in milliseconds, returns 0 if
 *  the wait succeeds, MUTEX_TIMEDOUT if the wait does not succeed in
 *  the allotted time, and -1 on error.
 *
 *  On some platforms this function is implemented by looping with a delay
 *  of 1 ms, and so should be avoided if possible.
 */
extern  int  SemWaitTimeout(Sem_t *sem, Uint32 ms);

/** Atomically increases the semaphore's count (not blocking).
 *  @return 0, or -1 on error.
 */
extern  int  SemPost(Sem_t *sem);

/** Returns the current count of the semaphore */
extern  Uint32  SemValue(Sem_t *sem);

/*@}*/

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/** @name Condition_variable_functions                           */ /*@{*/
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*@{*/
/** The SDL condition variable structure, defined in cond.c */
struct cond;
typedef struct cond cond;
/*@}*/

/** Create a condition variable */
extern  cond *  CreateCond(void);

/** Destroy a condition variable */
extern  void  DestroyCond(cond *cond);

/** Restart one of the threads that are waiting on the condition variable,
 *  @return 0 or -1 on error.
 */
extern  int  CondSignal(cond *cond);

/** Restart all threads that are waiting on the condition variable,
 *  @return 0 or -1 on error.
 */
extern  int  CondBroadcast(cond *cond);

/** Wait on the condition variable, unlocking the provided mutex.
 *  The mutex must be locked before entering this function!
 *  The mutex is re-locked once the condition variable is signaled.
 *  @return 0 when it is signaled, or -1 on error.
 */
extern  int  CondWait(cond *cond, Mutex_t *mut);

/** Waits for at most 'ms' milliseconds, and returns 0 if the condition
 *  variable is signaled, MUTEX_TIMEDOUT if the condition is not
 *  signaled in the allotted time, and -1 on error.
 *  On some platforms this function is implemented by looping with a delay
 *  of 1 ms, and so should be avoided if possible.
 */
extern  int  CondWaitTimeout(cond *cond, Mutex_t *mutex, Uint32 ms);

/*@}*/

/* Ends C function definitions when using C++ */
#ifdef __cplusplus
}
#endif

#endif /* _mutex_h */
