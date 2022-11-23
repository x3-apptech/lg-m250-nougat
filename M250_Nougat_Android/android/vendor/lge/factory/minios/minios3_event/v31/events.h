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

/**
 *  @file events.h
 *  Include file for SDL event handling
 */

#ifndef _events_h
#define _events_h

#include "stdinc.h"
#include "error.h"
#include "active.h"
#include "keyboard.h"
#include "mouse.h"
#include "quit.h"
#include "sensor.h"

/* Set up for C function definitions, even when using C++ */
#ifdef __cplusplus
extern "C" {
#endif

/** @name General touchpad max point count */
#define MAX_POINTERS 32

/** @name General keyboard/mouse state definitions */
/*@{*/
#define RELEASED    0
#define PRESSED    1
/*@}*/

/** Event enumerations */
typedef enum {
       NOEVENT = 0,            /**< Unused (do not remove) */
       ACTIVEEVENT,            /**< Application loses/gains visibility */
       KEYDOWN,            /**< Keys pressed */
       KEYUP,            /**< Keys released */
       MOUSEMOTION,            /**< Mouse moved */
       MOUSEBUTTONDOWN,        /**< Mouse button pressed */
       MOUSEBUTTONUP,        /**< Mouse button released */
       JOYAXISMOTION,        /**< Joystick axis motion */
       JOYBALLMOTION,        /**< Joystick trackball motion */
       JOYHATMOTION,        /**< Joystick hat position change */
       JOYBUTTONDOWN,        /**< Joystick button pressed */
       JOYBUTTONUP,            /**< Joystick button released */
       QUIT,            /**< User-requested quit */
       SYSWMEVENT,            /**< System specific event */
       EVENT_RESERVEDA,        /**< Reserved for future use.. */
       EVENT_RESERVEDB,        /**< Reserved for future use.. */
       VIDEORESIZE,            /**< User resized video mode */
       VIDEOEXPOSE,            /**< Screen needs to be redrawn */
       MOUSEBUTTONLONG,        /**< Mouse button keep pressing */
//       EVENT_RESERVED2,        /**< Reserved for future use.. */
       EVENT_RESERVED3,        /**< Reserved for future use.. */
       EVENT_RESERVED4,        /**< Reserved for future use.. */
       EVENT_RESERVED5,        /**< Reserved for future use.. */
       EVENT_RESERVED6,        /**< Reserved for future use.. */
       EVENT_RESERVED7,        /**< Reserved for future use.. */
       /** Events USEREVENT through MAXEVENTS-1 are for your use */
       USEREVENT,
       /** This last event is only for bounding internal arrays
    *  It is the number of bits in the event mask datatype -- Uint32
        */
       NUMEVENTS = 32
} EventType;

/** @name Predefined event masks */
/*@{*/
#define EVENTMASK(X)    (1<<(X))
typedef enum {
    ACTIVEEVENTMASK    = EVENTMASK(ACTIVEEVENT),
    KEYDOWNMASK        = EVENTMASK(KEYDOWN),
    KEYUPMASK        = EVENTMASK(KEYUP),
    KEYEVENTMASK    = EVENTMASK(KEYDOWN)|
                              EVENTMASK(KEYUP),
    MOUSEMOTIONMASK    = EVENTMASK(MOUSEMOTION),
    MOUSEBUTTONDOWNMASK    = EVENTMASK(MOUSEBUTTONDOWN),
    MOUSEBUTTONUPMASK    = EVENTMASK(MOUSEBUTTONUP),
    MOUSEEVENTMASK    = EVENTMASK(MOUSEMOTION)|
                              EVENTMASK(MOUSEBUTTONDOWN)|
                              EVENTMASK(MOUSEBUTTONUP),
    JOYAXISMOTIONMASK    = EVENTMASK(JOYAXISMOTION),
    JOYBALLMOTIONMASK    = EVENTMASK(JOYBALLMOTION),
    JOYHATMOTIONMASK    = EVENTMASK(JOYHATMOTION),
    JOYBUTTONDOWNMASK    = EVENTMASK(JOYBUTTONDOWN),
    JOYBUTTONUPMASK    = EVENTMASK(JOYBUTTONUP),
    JOYEVENTMASK    = EVENTMASK(JOYAXISMOTION)|
                              EVENTMASK(JOYBALLMOTION)|
                              EVENTMASK(JOYHATMOTION)|
                              EVENTMASK(JOYBUTTONDOWN)|
                              EVENTMASK(JOYBUTTONUP),
    VIDEORESIZEMASK    = EVENTMASK(VIDEORESIZE),
    VIDEOEXPOSEMASK    = EVENTMASK(VIDEOEXPOSE),
    QUITMASK        = EVENTMASK(QUIT),
    SYSWMEVENTMASK    = EVENTMASK(SYSWMEVENT)
} EventMask ;
#define ALLEVENTS        0xFFFFFFFF
/*@}*/

/** Application visibility event structure */
typedef struct ActiveEvent {
    Uint8 type;    /**< ACTIVEEVENT */
    Uint8 gain;    /**< Whether given states were gained or lost (1/0) */
    Uint8 state;    /**< A mask of the focus states */
} ActiveEvent;

/** Keyboard event structure */
typedef struct KeyboardEvent {
    Uint8 type;    /**< KEYDOWN or KEYUP */
    Uint8 which;    /**< The keyboard device index */
    Uint8 state;    /**< PRESSED or RELEASED */
    keysym keysym;
} KeyboardEvent;

/** Mouse motion event structure */
typedef struct MouseMotionEvent {
    Uint8 type;    /**< MOUSEMOTION */
    Uint8 which;    /**< The mouse device index */
    Uint8 button;    /**< The mouse button index */
    Uint8 state;    /**< The current button state */
    Uint16 x, y;    /**< The X/Y coordinates of the mouse */
    Sint16 xrel;    /**< The relative motion in the X direction */
    Sint16 yrel;    /**< The relative motion in the Y direction */
} MouseMotionEvent;

/** Mouse button event structure */
typedef struct MouseButtonEvent {
    Uint8 type;    /**< MOUSEBUTTONDOWN or MOUSEBUTTONUP */
    Uint8 which;    /**< The mouse device index */
    Uint8 button;    /**< The mouse button index */
    Uint8 state;    /**< PRESSED or RELEASED */
    Uint16 x, y;    /**< The X/Y coordinates of the mouse at press time */
} MouseButtonEvent;

/** Joystick axis motion event structure */
typedef struct JoyAxisEvent {
    Uint8 type;    /**< JOYAXISMOTION */
    Uint8 which;    /**< The joystick device index */
    Uint8 axis;    /**< The joystick axis index */
    Sint16 value;    /**< The axis value (range: -32768 to 32767) */
} JoyAxisEvent;

/** Joystick trackball motion event structure */
typedef struct JoyBallEvent {
    Uint8 type;    /**< JOYBALLMOTION */
    Uint8 which;    /**< The joystick device index */
    Uint8 ball;    /**< The joystick trackball index */
    Sint16 xrel;    /**< The relative motion in the X direction */
    Sint16 yrel;    /**< The relative motion in the Y direction */
} JoyBallEvent;

/** Joystick hat position change event structure */
typedef struct JoyHatEvent {
    Uint8 type;    /**< JOYHATMOTION */
    Uint8 which;    /**< The joystick device index */
    Uint8 hat;    /**< The joystick hat index */
    Uint8 value;    /**< The hat position value:
             *   HAT_LEFTUP   HAT_UP       HAT_RIGHTUP
             *   HAT_LEFT     HAT_CENTERED HAT_RIGHT
             *   HAT_LEFTDOWN HAT_DOWN     HAT_RIGHTDOWN
             *  Note that zero means the POV is centered.
             */
} JoyHatEvent;

/** Joystick button event structure */
typedef struct JoyButtonEvent {
    Uint8 type;    /**< JOYBUTTONDOWN or JOYBUTTONUP */
    Uint8 which;    /**< The joystick device index */
    Uint8 button;    /**< The joystick button index */
    Uint8 state;    /**< PRESSED or RELEASED */
} JoyButtonEvent;

/** The "window resized" event
 *  When you get this event, you are responsible for setting a new video
 *  mode with the new width and height.
 */
typedef struct ResizeEvent {
    Uint8 type;    /**< VIDEORESIZE */
    int w;        /**< New width */
    int h;        /**< New height */
} ResizeEvent;

/** The "screen redraw" event */
typedef struct ExposeEvent {
    Uint8 type;    /**< VIDEOEXPOSE */
} ExposeEvent;

/** The "quit requested" event */
typedef struct QuitEvent {
    Uint8 type;    /**< QUIT */
} QuitEvent;

/** A user-defined event type */
typedef struct UserEvent {
    Uint8 type;    /**< USEREVENT through NUMEVENTS-1 */
    int code;    /**< User defined event code */
    int state;
    void *data1;    /**< User defined data pointer */
    void *data2;    /**< User defined data pointer */
} UserEvent;

/** If you want to use this event, you should include syswm.h */
struct SysWMmsg;
typedef struct SysWMmsg SysWMmsg;
typedef struct SysWMEvent {
    Uint8 type;
    SysWMmsg *msg;
} SysWMEvent;

/** General event structure */
typedef union Event {
    Uint8 type;
    ActiveEvent active;
    KeyboardEvent key;
    MouseMotionEvent motion;
    MouseButtonEvent button;
    JoyAxisEvent jaxis;
    JoyBallEvent jball;
    JoyHatEvent jhat;
    JoyButtonEvent jbutton;
    ResizeEvent resize;
    ExposeEvent expose;
    QuitEvent quit;
    UserEvent user;
    SysWMEvent syswm;
} Event_t;


/* Function prototypes */

/** Pumps the event loop, gathering events from the input devices.
 *  This function updates the event queue and internal input device state.
 *  This should only be run in the thread that sets the video mode.
 */
extern  void  PumpEvents(void);

typedef enum {
    ADDEVENT,
    PEEKEVENT,
    GETEVENT
} eventaction;

/**
 *  Checks the event queue for messages and optionally returns them.
 *
 *  If 'action' is ADDEVENT, up to 'numevents' events will be added to
 *  the back of the event queue.
 *  If 'action' is PEEKEVENT, up to 'numevents' events at the front
 *  of the event queue, matching 'mask', will be returned and will not
 *  be removed from the queue.
 *  If 'action' is GETEVENT, up to 'numevents' events at the front
 *  of the event queue, matching 'mask', will be returned and will be
 *  removed from the queue.
 *
 *  @return
 *  This function returns the number of events actually stored, or -1
 *  if there was an error.
 *
 *  This function is thread-safe.
 */
extern  int  PeepEvents(Event_t *events, int numevents,
                eventaction action, Uint32 mask);

/** Polls for currently pending events, and returns 1 if there are any pending
 *  events, or 0 if there are none available.  If 'event' is not NULL, the next
 *  event is removed from the queue and stored in that area.
 */
extern  int  PollEvent(Event_t *event);

/** Waits indefinitely for the next available event, returning 1, or 0 if there
 *  was an error while waiting for events.  If 'event' is not NULL, the next
 *  event is removed from the queue and stored in that area.
 */
extern  int  WaitEvent(Event_t *event);

/** Add an event to the event queue.
 *  This function returns 0 on success, or -1 if the event queue was full
 *  or there was some other error.
 */
extern  int  PushEvent(Event_t *event);

/** @name Event Filtering */
/*@{*/
typedef int ( *EventFilter)(const Event_t *event);
/**
 *  This function sets up a filter to process all events before they
 *  change internal state and are posted to the internal event queue.
 *
 *  The filter is protypted as:
 *      @code typedef int ( *EventFilter)(const Event *event); @endcode
 *
 * If the filter returns 1, then the event will be added to the internal queue.
 * If it returns 0, then the event will be dropped from the queue, but the
 * internal state will still be updated.  This allows selective filtering of
 * dynamically arriving events.
 *
 * @warning  Be very careful of what you do in the event filter function, as
 *           it may run in a different thread!
 *
 * There is one caveat when dealing with the QUITEVENT event type.  The
 * event filter is only called when the window manager desires to close the
 * application window.  If the event filter returns 1, then the window will
 * be closed, otherwise the window will remain open if possible.
 * If the quit event is generated by an interrupt signal, it will bypass the
 * internal queue and be delivered to the application at the next event poll.
 */
extern  void  SetEventFilter(EventFilter filter);

/**
 *  Return the current event filter - can be used to "chain" filters.
 *  If there is no event filter set, this function returns NULL.
 */
extern  EventFilter  GetEventFilter(void);
/*@}*/

/** @name Event State */
/*@{*/
#define QUERY    -1
#define IGNORE     0
#define DISABLE     0
#define ENABLE     1
/*@}*/

/**
* This function allows you to set the state of processing certain events.
* If 'state' is set to IGNORE, that event will be automatically dropped
* from the event queue and will not event be filtered.
* If 'state' is set to ENABLE, that event will be processed normally.
* If 'state' is set to QUERY, EventState() will return the
* current processing state of the specified event.
*/
extern  Uint8  EventState(Uint8 type, int state);

/**
* For sensor control
*/
extern Bool GetSensorStatus(void);
extern int SetSensorCommand(int32_t sensor_type, int32_t command);
extern Bool GetSensorInfo(int32_t sensor_type, SensorData* data);

extern int EventInit(Uint32 flags);
extern void EventQuit();
/* Ends C function definitions when using C++ */
#ifdef __cplusplus
}
#endif

#endif /* _events_h */
