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

#ifdef TIMER_UNIX

#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "timer.h"
#include "timer_c.h"

/* The clock_gettime provides monotonous time, so we should use it if
   it's available. The clock_gettime function is behind ifdef
   for __USE_POSIX199309
   Tommi Kyntola (tommi.kyntola@ray.fi) 27/09/2005
*/
#if HAVE_NANOSLEEP || HAVE_CLOCK_GETTIME
#include <time.h>
#endif

#if THREADS_DISABLED
#define USE_ITIMER
#endif

/* The first ticks value of the application */
#ifdef HAVE_CLOCK_GETTIME
static struct timespec start;
#else
static struct timeval start;
#endif /* HAVE_CLOCK_GETTIME */


void StartTicks(void)
{
    /* Set first ticks value */
#if HAVE_CLOCK_GETTIME
    clock_gettime(CLOCK_MONOTONIC,&start);
#else
    gettimeofday(&start, NULL);
#endif
}

Uint32 GetTicks (void)
{
#if HAVE_CLOCK_GETTIME
    Uint32 ticks;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC,&now);
    ticks=(now.tv_sec-start.tv_sec)*1000+(now.tv_nsec-start.tv_nsec)/1000000;
    return(ticks);
#else
    Uint32 ticks;
    struct timeval now;
    gettimeofday(&now, NULL);
    ticks=(now.tv_sec-start.tv_sec)*1000+(now.tv_usec-start.tv_usec)/1000;
    return(ticks);
#endif
}

void Delay (Uint32 ms)
{
    int was_error;

#if HAVE_NANOSLEEP
    struct timespec elapsed, tv;
#else
    struct timeval tv;
    Uint32 then, now, elapsed;
#endif

    /* Set the timeout interval */
#if HAVE_NANOSLEEP
    elapsed.tv_sec = ms/1000;
    elapsed.tv_nsec = (ms%1000)*1000000;
#else
    then = GetTicks();
#endif
    do {
//        errno = 0;

#if HAVE_NANOSLEEP
        tv.tv_sec = elapsed.tv_sec;
        tv.tv_nsec = elapsed.tv_nsec;
        was_error = nanosleep(&tv, &elapsed);
#else
        /* Calculate the time interval left (in case of interrupt) */
        now = GetTicks();
        elapsed = (now-then);
        then = now;
        if ( elapsed >= ms ) {
            break;
        }
        ms -= elapsed;
        tv.tv_sec = ms/1000;
        tv.tv_usec = (ms%1000)*1000;

        was_error = select(0, NULL, NULL, NULL, &tv);
#endif /* HAVE_NANOSLEEP */
    } while ( was_error /*&& (errno == EINTR)*/ );
}

#ifdef USE_ITIMER

static void HandleAlarm(int sig)
{
    Uint32 ms;

    if ( alarm_callback ) {
        ms = (*alarm_callback)(alarm_interval);
        if ( ms != alarm_interval ) {
            SetTimer(ms, alarm_callback);
        }
    }
}

int SYS_TimerInit(void)
{
    struct sigaction action;

    /* Set the alarm handler (Linux specific) */
    memset(&action, 0, sizeof(action));
    action.sa_handler = HandleAlarm;
    action.sa_flags = SA_RESTART;
//    sigemptyset(&action.sa_mask);
    sigaction(SIGALRM, &action, NULL);
    return(0);
}

void SYS_TimerQuit(void)
{
    SetTimer(0, NULL);
}

int SYS_StartTimer(void)
{
    struct itimerval timer;

    timer.it_value.tv_sec = (alarm_interval/1000);
    timer.it_value.tv_usec = (alarm_interval%1000)*1000;
    timer.it_interval.tv_sec = (alarm_interval/1000);
    timer.it_interval.tv_usec = (alarm_interval%1000)*1000;
    setitimer(ITIMER_REAL, &timer, NULL);
    return(0);
}

void SYS_StopTimer(void)
{
    struct itimerval timer;

    memset(&timer, 0, (sizeof timer));
    setitimer(ITIMER_REAL, &timer, NULL);
}

#else /* USE_ITIMER */

#include "thread.h"

/* Data to handle a single periodic alarm */
static int timer_alive = 0;
static Thread_t *timer = NULL;

static int RunTimer(void *unused)
{
    while ( timer_alive ) {
        if ( timer_running ) {
            ThreadedTimerCheck();
        }
        Delay(1);
    }
    return(0);
}

/* This is only called if the event thread is not running */
int SYS_TimerInit(void)
{
    timer_alive = 1;
    timer = CreateThread(RunTimer, NULL);
    if ( timer == NULL )
        return(-1);
    return(SetTimerThreaded(1));
}

void SYS_TimerQuit(void)
{
    timer_alive = 0;
    if ( timer ) {
        WaitThread(timer, NULL);
        timer = NULL;
    }
}

int SYS_StartTimer(void)
{
    SetError("Internal logic error: Linux uses threaded timer");
    return(-1);
}

void SYS_StopTimer(void)
{
    return;
}

#endif /* USE_ITIMER */

#endif /* TIMER_UNIX */
