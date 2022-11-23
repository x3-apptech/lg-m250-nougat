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

#include "timer.h"
#include "timer_c.h"
#include "mutex.h"
#include "systimer.h"

/* #define DEBUG_TIMERS */

int timer_started = 0;
int timer_running = 0;

/* Data to handle a single periodic alarm */
Uint32 alarm_interval = 0;
TimerCallback alarm_callback;

/* Data used for a thread-based timer */
static int timer_threaded = 0;

struct _TimerID {
    Uint32 interval;
    NewTimerCallback cb;
    void *param;
    Uint32 last_alarm;
    struct _TimerID *next;
};

static TimerID timers = NULL;
static Mutex_t *timer_mutex;
static volatile Bool list_changed = FALSE;

static struct _TimerID timer_pool[MAX_TIMER_COUNT];

/* Set whether or not the timer should use a thread.
   This should not be called while the timer subsystem is running.
*/
int SetTimerThreaded(int value)
{
    int retval;

    if(timer_started) {
        SetError("Timer already initialized");
        retval = -1;
    }
    else {
        retval = 0;
        timer_threaded = value;
    }

    return retval;
}

int TimerInit(void)
{
    int retval;

    retval = 0;
    if(timer_started) {
        TimerQuit();
    }
    if(!timer_threaded) {
        memset(timer_pool, 0x00, (sizeof(struct _TimerID) * MAX_TIMER_COUNT));
        timer_running = 0;
        retval = SYS_TimerInit();
    }

    if(timer_threaded) {
        timer_mutex = CreateMutex();
    }

    if(retval == 0) {
        timer_started = 1;
    }

    return(retval);
}

void TimerQuit(void)
{
    SetTimer(0, NULL);
    if(timer_threaded < 2) {
        SYS_TimerQuit();
    }

    if(timer_threaded) {
        DestroyMutex(timer_mutex);
        timer_mutex = NULL;
    }

    timer_started = 0;
    timer_threaded = 0;
}

void ThreadedTimerCheck(void)
{
    int index;
    Uint32 now, ms;
    TimerID t;

    mutexP(timer_mutex);
    now = GetTicks();
    list_changed = FALSE;
    for(index=0; index<MAX_TIMER_COUNT; index++) {
        t = &timer_pool[index];
        if(TIMESLICE > t->interval) {
            continue;
        }

        ms = t->interval - TIMESLICE;

        if((int)(now - t->last_alarm) > (int)ms) {
            if((now - t->last_alarm) < t->interval) {
                t->last_alarm += t->interval;
            }
            else {
                t->last_alarm = now;
            }

            mutexV(timer_mutex);
            ms = t->cb(t->interval, t->param);
            mutexP(timer_mutex);

            if(list_changed) {
                break;
            }

            if(ms != t->interval) {
                if(ms) {
                    t->interval = ROUND_RESOLUTION(ms);
                }
                else {
                    memset(t, 0x00, sizeof(struct _TimerID));
                    timer_running--;
                    if(timer_running <= 0) {
                        timer_running = 0;
                    }
                }
            }
        }
    }
    mutexV(timer_mutex);
}

static TimerID AddTimerInternal(Uint32 interval, NewTimerCallback callback, void *param)
{
    int index;
    TimerID t;

    for(index=0; index<MAX_TIMER_COUNT; index++) {
        t = &timer_pool[index];
        if(t->interval == 0) {
            break;
        }
    }

    if(t) {
        t->interval = ROUND_RESOLUTION(interval);
        t->cb = callback;
        t->param = param;
        t->last_alarm = GetTicks();
        timer_running++;
    }
    else {
        return NULL;
    }

    return t;
}

TimerID AddTimer(Uint32 interval, NewTimerCallback callback, void *param)
{
    TimerID t;
    if(!timer_mutex) {
        if(timer_started) {
            SetError("This platform doesn't support multiple timers");
        }
        else {
            SetError("You must call Init(INIT_TIMER) first");
        }
        return NULL;
    }

    if(!timer_threaded) {
        SetError("Multiple timers require threaded events!");
        return NULL;
    }

    if(TIMESLICE > ROUND_RESOLUTION(interval)) {
        SetError("Interval is too short!");
        return NULL;
    }

    mutexP(timer_mutex);
    t = AddTimerInternal(interval, callback, param);
    mutexV(timer_mutex);

    return t;
}

Bool RemoveTimer(TimerID id)
{
    int index;
    TimerID t;

    mutexP(timer_mutex);
    /* Look for id in the linked list of timers */
    for(index=0; index<MAX_TIMER_COUNT; index++) {
        t = &timer_pool[index];
        if (t == id) {
            memset(t, 0x00, sizeof(struct _TimerID));
            timer_running--;
            if(timer_running <= 0) {
                timer_running = 0;
            }
            list_changed = TRUE;
            break;
        }
    }
    mutexV(timer_mutex);

    if(index >= MAX_TIMER_COUNT) {
        return FALSE;
    }

    return TRUE;
}

/* Old style callback functions are wrapped through this */
static Uint32  callback_wrapper(Uint32 ms, void *param)
{
    TimerCallback func = (TimerCallback) param;
    return (*func)(ms);
}

int SetTimer(Uint32 ms, TimerCallback callback)
{
    int retval;
    retval = 0;

    if(timer_threaded) {
        mutexP(timer_mutex);
    }

    memset(timer_pool, 0x00, (sizeof(struct _TimerID) * MAX_TIMER_COUNT));
    list_changed = TRUE;
    timer_running = 0;

    if(ms) {
        if(timer_threaded) {
            if(AddTimerInternal(ms, callback_wrapper, (void *)callback) == NULL) {
                retval = -1;
            }
        }
        else {
            timer_running = 1;
            alarm_interval = ms;
            alarm_callback = callback;
            retval = SYS_StartTimer();
        }
    }

    if(timer_threaded) {
        mutexV(timer_mutex);
    }

    return retval;
}
