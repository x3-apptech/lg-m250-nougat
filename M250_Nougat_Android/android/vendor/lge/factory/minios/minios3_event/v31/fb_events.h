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

#include <linux/fb.h>
#include <termios.h>

//#include "SDL_fbvideo.h"

/* Variables and functions exported by SDL_sysevents.c to other parts
   of the native video subsystem (SDL_sysvideo.c)
*/
int FB_OpenKeyboard(/* _THIS */);
void FB_CloseKeyboard(/* _THIS */);
int FB_OpenMouse(/* _THIS */);
void FB_CloseMouse(/* _THIS */);
int FB_EnterGraphicsMode(/* _THIS */);
int FB_InGraphicsMode(/* _THIS */);
void FB_LeaveGraphicsMode(/* _THIS */);

void FB_InitOSKeymap(/* _THIS */);

// from Private variables
int console_fd;
struct fb_var_screeninfo cache_vinfo;
struct fb_var_screeninfo saved_vinfo;
int saved_cmaplen;
__u16 *saved_cmap;

int current_vt;
int saved_vt;
int keyboard_fd;
int saved_kbd_mode;
struct termios saved_kbd_termios;

int mouse_fd;
#if INPUT_TSLIB
struct tsdev *ts_dev;
#endif
