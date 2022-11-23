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

/* General event handling code for SDL */

//#include "SDL.h"
#include "syswm.h"
#include "events_c.h"
#include "timer_c.h"
#include "mutex.h"
#include "thread.h"

/* Public data -- the event filter */
EventFilter EventOK = NULL;
Uint8 ProcessEvents[NUMEVENTS];
static Uint32 eventstate = 0;

// variables for initialization
static Uint32 initialized = 0;
static Uint32 ticks_started = 0;

/* Private data -- event queue */
#define MAXEVENTS    128
static struct {
    Mutex_t *lock;
    int active;
    int head;
    int tail;
    Event_t event[MAXEVENTS];
    int wmmsg_next;
    struct SysWMmsg wmmsg[MAXEVENTS];
} EventQ;

/* Private data -- event locking structure */
static struct {
    Mutex_t *lock;
    int safe;
} EventLock;

/* Thread functions */
static Thread_t *EventThread = NULL;    /* Thread handle */
static Uint32 event_thread;            /* The event thread id */

void Lock_EventThread(void)
{
    if ( EventThread && (ThreadID() != event_thread) ) {
        /* Grab lock and spin until we're sure event thread stopped */
        mutexP(EventLock.lock);
        while ( ! EventLock.safe ) {
            Delay(1);
        }
    }
}
void Unlock_EventThread(void)
{
    if ( EventThread && (ThreadID() != event_thread) ) {
        mutexV(EventLock.lock);
    }
}

static int GobbleEvents(void *unused)
{
    event_thread = ThreadID();

    while ( EventQ.active ) {
        //jcmaeng VideoDevice *video = current_video;
        //jcmaeng VideoDevice *this  = current_video;

        /* Get events from the video subsystem */
        //jcmaeng if ( video ) {
            //jcmaeng video->PumpEvents(this);
        //}

        /* Queue pending key-repeat events */
        CheckKeyRepeat();

        /* Give up the CPU for the rest of our timeslice */
        EventLock.safe = 1;
        if ( timer_running ) {
            ThreadedTimerCheck();
        }
        Delay(1);

        /* Check for event locking.
           On the P of the lock mutex, if the lock is held, this thread
           will wait until the lock is released before continuing.  The
           safe flag will be set, meaning that the other thread can go
           about it's business.  The safe flag is reset before the V,
           so as soon as the mutex is free, other threads can see that
           it's not safe to interfere with the event thread.
         */
        mutexP(EventLock.lock);
        EventLock.safe = 0;
        mutexV(EventLock.lock);
    }
    SetTimerThreaded(0);
    event_thread = 0;
    return(0);
}

static int StartEventThread(Uint32 flags)
{
    /* Reset everything to zero */
    EventThread = NULL;
    memset(&EventLock, 0, sizeof(EventLock));

    /* Create the lock and set ourselves active */
#if !THREADS_DISABLED
    EventQ.lock = CreateMutex();
    if ( EventQ.lock == NULL ) {
#ifdef __MACOS__ /* MacOS classic you can't multithread, so no lock needed */
        ;
#else
        return(-1);
#endif
    }
#endif /* !THREADS_DISABLED */
    EventQ.active = 1;

{
    extern Bool InputManager_Start(void);
    InputManager_Start();
}

    if ( (flags&INIT_EVENTTHREAD) == INIT_EVENTTHREAD ) {
        EventLock.lock = CreateMutex();
        if ( EventLock.lock == NULL ) {
            return(-1);
        }
        EventLock.safe = 0;

        /* The event thread will handle timers too */
        SetTimerThreaded(2);
        EventThread = CreateThread(GobbleEvents, NULL);
        if ( EventThread == NULL ) {
            return(-1);
        }
    } else {
        event_thread = 0;
    }
    return(0);
}

static void StopEventThread(void)
{
    extern Bool InputManager_Start(void);
    InputManager_Stop();

    EventQ.active = 0;
    if ( EventThread ) {
        WaitThread(EventThread, NULL);
        EventThread = NULL;
        DestroyMutex(EventLock.lock);
        EventLock.lock = NULL;
    }
#ifndef IPOD
    DestroyMutex(EventQ.lock);
    EventQ.lock = NULL;
#endif
}

Uint32 EventThreadID(void)
{
    return(event_thread);
}

/* Public functions */

void StopEventLoop(void)
{
    /* Halt the event thread, if running */
    StopEventThread();

    /* Shutdown event handlers */
    AppActiveQuit();
    KeyboardQuit();
    MouseQuit();
    QuitQuit();

    /* Clean out EventQ */
    EventQ.head = 0;
    EventQ.tail = 0;
    EventQ.wmmsg_next = 0;
}

/* This function (and associated calls) may be called more than once */
int StartEventLoop(Uint32 flags)
{
    int retcode;

    /* Clean out the event queue */
    EventThread = NULL;
    EventQ.lock = NULL;
    StopEventLoop();

    /* No filter to start with, process most event types */
    EventOK = NULL;
    memset(ProcessEvents,ENABLE,sizeof(ProcessEvents));
    eventstate = ~0;
    /* It's not save to call EventState() yet */
    eventstate &= ~(0x00000001 << SYSWMEVENT);
    ProcessEvents[SYSWMEVENT] = IGNORE;

    /* Initialize event handlers */
    retcode = 0;
    retcode += AppActiveInit();
    retcode += KeyboardInit();
    retcode += MouseInit();
    retcode += QuitInit();
    if ( retcode < 0 ) {
        /* We don't expect them to fail, but... */
        return(-1);
    }

    /* Create the lock and event thread */
    if ( StartEventThread(flags) < 0 ) {
        StopEventLoop();
        return(-1);
    }
    return(0);
}


/* Add an event to the event queue -- called with the queue locked */
static int AddEvent(Event_t *event)
{
    int tail, added;

    tail = (EventQ.tail+1)%MAXEVENTS;
    if ( tail == EventQ.head ) {
        /* Overflow, drop event */
        added = 0;
    } else {
        EventQ.event[EventQ.tail] = *event;
        if (event->type == SYSWMEVENT) {
            /* Note that it's possible to lose an event */
            int next = EventQ.wmmsg_next;
            EventQ.wmmsg[next] = *event->syswm.msg;
                EventQ.event[EventQ.tail].syswm.msg =
                        &EventQ.wmmsg[next];
            EventQ.wmmsg_next = (next+1)%MAXEVENTS;
        }
        EventQ.tail = tail;
        added = 1;
    }
    return(added);
}

/* Cut an event, and return the next valid spot, or the tail */
/*                           -- called with the queue locked */
static int CutEvent(int spot)
{
    if ( spot == EventQ.head ) {
        EventQ.head = (EventQ.head+1)%MAXEVENTS;
        return(EventQ.head);
    } else
    if ( (spot+1)%MAXEVENTS == EventQ.tail ) {
        EventQ.tail = spot;
        return(EventQ.tail);
    } else
    /* We cut the middle -- shift everything over */
    {
        int here, next;

        /* This can probably be optimized with memcpy() -- careful! */
        if ( --EventQ.tail < 0 ) {
            EventQ.tail = MAXEVENTS-1;
        }
        for ( here=spot; here != EventQ.tail; here = next ) {
            next = (here+1)%MAXEVENTS;
            EventQ.event[here] = EventQ.event[next];
        }
        return(spot);
    }
    /* NOTREACHED */
}

/* Lock the event queue, take a peep at it, and unlock it */
int PeepEvents(Event_t *events, int numevents, eventaction action,
                                Uint32 mask)
{
    int i, used;

    /* Don't look after we've quit */
    if ( ! EventQ.active ) {
        return(-1);
    }
    /* Lock the event queue */
    used = 0;
    if ( mutexP(EventQ.lock) == 0 ) {
        if ( action == ADDEVENT ) {
            for ( i=0; i<numevents; ++i ) {
                used += AddEvent(&events[i]);
            }
        } else {
            Event_t tmpevent;
            int spot;

            /* If 'events' is NULL, just see if they exist */
            if ( events == NULL ) {
                action = PEEKEVENT;
                numevents = 1;
                events = &tmpevent;
            }
            spot = EventQ.head;
            while ((used < numevents)&&(spot != EventQ.tail)) {
                if ( mask & EVENTMASK(EventQ.event[spot].type) ) {
                    events[used++] = EventQ.event[spot];
                    if ( action == GETEVENT ) {
                        spot = CutEvent(spot);
                    } else {
                        spot = (spot+1)%MAXEVENTS;
                    }
                } else {
                    spot = (spot+1)%MAXEVENTS;
                }
            }
        }
        mutexV(EventQ.lock);
    } else {
        SetError("Couldn't lock event queue");
        used = -1;
    }
    return(used);
}

/* Run the system dependent event loops */
void PumpEvents(void)
{
//jcmaeng
#if 0
    if ( !EventThread ) {
        VideoDevice *video = current_video;
        VideoDevice *this  = current_video;

        /* Get events from the video subsystem */
        if ( video ) {
            video->PumpEvents(this);
        }

        /* Queue pending key-repeat events */
        CheckKeyRepeat();

    }
#endif
}

/* Public functions */

int PollEvent (Event_t *event)
{
    PumpEvents();

    /* We can't return -1, just return 0 (no event) on error */
    if ( PeepEvents(event, 1, GETEVENT, ALLEVENTS) <= 0 )
        return 0;
    return 1;
}

int WaitEvent (Event_t *event)
{
    while ( 1 ) {
        PumpEvents();
        switch(PeepEvents(event, 1, GETEVENT, ALLEVENTS)) {
            case -1: return 0;
            case 1: return 1;
            case 0: Delay(10);
        }
    }
}

int PushEvent(Event_t *event)
{
    if ( PeepEvents(event, 1, ADDEVENT, 0) <= 0 )
        return -1;
    return 0;
}

void SetEventFilter (EventFilter filter)
{
    Event_t bitbucket;

    /* Set filter and discard pending events */
    EventOK = filter;
    while ( PollEvent(&bitbucket) > 0 )
        ;
}

EventFilter GetEventFilter(void)
{
    return(EventOK);
}

Uint8 EventState (Uint8 type, int state)
{
    Event_t bitbucket;
    Uint8 current_state;

    /* If ALLEVENTS was specified... */
    if ( type == 0xFF ) {
        current_state = IGNORE;
        for ( type=0; type<NUMEVENTS; ++type ) {
            if ( ProcessEvents[type] != IGNORE ) {
                current_state = ENABLE;
            }
            ProcessEvents[type] = state;
            if ( state == ENABLE ) {
                eventstate |= (0x00000001 << (type));
            } else {
                eventstate &= ~(0x00000001 << (type));
            }
        }
        while ( PollEvent(&bitbucket) > 0 )
            ;
        return(current_state);
    }

    /* Just set the state for one event type */
    current_state = ProcessEvents[type];
    switch (state) {
        case IGNORE:
        case ENABLE:
            /* Set state and discard pending events */
            ProcessEvents[type] = state;
            if ( state == ENABLE ) {
                eventstate |= (0x00000001 << (type));
            } else {
                eventstate &= ~(0x00000001 << (type));
            }
            while ( PollEvent(&bitbucket) > 0 )
                ;
            break;
        default:
            /* Querying state? */
            break;
    }
    return(current_state);
}

/* This is a generic event handler.
 */
int PrivateSysWMEvent(SysWMmsg *message)
{
    int posted;

    posted = 0;
    if ( ProcessEvents[SYSWMEVENT] == ENABLE ) {
        Event_t event;
        memset(&event, 0, sizeof(event));
        event.type = SYSWMEVENT;
        event.syswm.msg = message;
        if ( (EventOK == NULL) || (*EventOK)(&event) ) {
            posted = 1;
            PushEvent(&event);
        }
    }
    /* Update internal event state */
    return(posted);
}

/* This is a sensor interface function
 */
extern Bool SensorManager_Status(void);
extern Bool SensorManager_GetInfo(int32_t sensor_type, SensorData* data);
extern int SensorManager_SetCommand(int32_t sensor_type, int32_t command);

Bool GetSensorStatus(void)
{
    return SensorManager_Status();
}

int SetSensorCommand(int32_t sensor_type, int32_t command)
{
    if(SensorManager_Status()) {
        return SensorManager_SetCommand(sensor_type, command);
    }
    else {
        return 0;
    }
}

Bool GetSensorInfo(int32_t sensor_type, SensorData* data)
{
    if(SensorManager_Status()) {
        return SensorManager_GetInfo(sensor_type, data);
    }
    else {
        return false;
    }
}

int EventInit(Uint32 flags)
{
    if (!ticks_started) {
        StartTicks();
        ticks_started = 1;
    }
    if (flags&INIT_TIMER) {
        if (TimerInit() < 0) {
            return (-1);
        }
    }
    initialized |= INIT_TIMER;

    StartEventLoop(flags);
    return 1;
}

void EventQuit()
{
    if (ticks_started) {
        TimerQuit();
    }
    StopEventLoop();
}
