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

/** @file keyboard.h
 *  Include file for SDL keyboard event handling
 */

#ifndef _keyboard_h
#define _keyboard_h

#include "stdinc.h"
#include "error.h"
#include "keysym.h"

/* Set up for C function definitions, even when using C++ */
#ifdef __cplusplus
extern "C" {
#endif

/** Keysym structure
 *
 *  - The scancode is hardware dependent, and should not be used by general
 *    applications.  If no hardware scancode is available, it will be 0.
 *
 *  - The 'unicode' translated character is only available when character
 *    translation is enabled by the EnableUNICODE() API.  If non-zero,
 *    this is a UNICODE character corresponding to the keypress.  If the
 *    high 9 bits of the character are 0, then this maps to the equivalent
 *    ASCII character:
 *      @code
 *    char ch;
 *    if ( (keysym.unicode & 0xFF80) == 0 ) {
 *        ch = keysym.unicode & 0x7F;
 *    } else {
 *        An international character..
 *    }
 *      @endcode
 */
typedef struct keysym {
    Uint8 scancode;            /**< hardware specific scancode */
    SDLKey sym;            /**< SDL virtual keysym */
    SDLMod mod;            /**< current key modifiers */
    Uint16 unicode;            /**< translated character */
} keysym;

/** This is the mask which refers to all hotkey bindings */
#define ALL_HOTKEYS        0xFFFFFFFF

/* Function prototypes */
/**
 * Enable/Disable UNICODE translation of keyboard input.
 *
 * This translation has some overhead, so translation defaults off.
 *
 * @param[in] enable
 * If 'enable' is 1, translation is enabled.
 * If 'enable' is 0, translation is disabled.
 * If 'enable' is -1, the translation state is not changed.
 *
 * @return It returns the previous state of keyboard translation.
 */
extern  int  EnableUNICODE(int enable);

#define DEFAULT_REPEAT_DELAY    500
#define DEFAULT_REPEAT_INTERVAL    30
/**
 * Enable/Disable keyboard repeat.  Keyboard repeat defaults to off.
 *
 *  @param[in] delay
 *  'delay' is the initial delay in ms between the time when a key is
 *  pressed, and keyboard repeat begins.
 *
 *  @param[in] interval
 *  'interval' is the time in ms between keyboard repeat events.
 *
 *  If 'delay' is set to 0, keyboard repeat is disabled.
 */
extern  int  EnableKeyRepeat(int delay, int interval);
extern  void  GetKeyRepeat(int *delay, int *interval);

/**
 * Get a snapshot of the current state of the keyboard.
 * Returns an array of keystates, indexed by the SDLK_* syms.
 * Usage:
 *    @code
 *     Uint8 *keystate = GetKeyState(NULL);
 *    if ( keystate[SDLK_RETURN] ) //... \<RETURN> is pressed.
 *    @endcode
 */
extern  Uint8 *  GetKeyState(int *numkeys);

/**
 * Get the current key modifier state
 */
extern  SDLMod  GetModState(void);

/**
 * Set the current key modifier state.
 * This does not change the keyboard state, only the key modifier flags.
 */
extern  void  SetModState(SDLMod modstate);

/**
 * Get the name of an SDL virtual keysym
 */
extern  char *  GetKeyName(SDLKey key);

/**
 * Get the keycode of an SDL virtual keysym
 */
extern  int GetKeyCode(char *name);

/* Ends C function definitions when using C++ */
#ifdef __cplusplus
}
#endif

#endif /* _keyboard_h */
