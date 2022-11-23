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

/* Handle the event stream, converting console events into SDL events */

#include <stdio.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

/* For parsing /proc */
#include <dirent.h>
#include <ctype.h>

#include <linux/vt.h>
#include <linux/kd.h>
#include <linux/keyboard.h>
#include <termios.h>

#include "timer.h"
#include "mutex.h"
//#include "sysvideo.h"
#include "events_c.h"
//#include "fbvideo.h"
#include "fb_events.h"
#include "fb_keys.h"

//#include "fbelo.h"

#ifndef GPM_NODE_FIFO
#define GPM_NODE_FIFO    "/dev/gpmdata"
#endif

/*#define DEBUG_KEYBOARD*/
/*#define DEBUG_MOUSE*/

/* The translation tables from a console scancode to a SDL keysym */
#define NUM_VGAKEYMAPS    (1<<KG_CAPSSHIFT)
static Uint16 vga_keymap[NUM_VGAKEYMAPS][NR_KEYS];
static SDLKey keymap[128];
static Uint16 keymap_temp[128]; /* only used at startup */
static keysym *TranslateKey(int scancode, keysym *keysym);

/* Ugh, we have to duplicate the kernel's keysym mapping code...
   Oh, it's not so bad. :-)

   FIXME: Add keyboard LED handling code
 */
static void FB_vgainitkeymaps(int fd)
{
    struct kbentry entry;
    int map, i;

    /* Don't do anything if we are passed a closed keyboard */
    if ( fd < 0 ) {
        return;
    }

    /* Load all the keysym mappings */
    for ( map=0; map<NUM_VGAKEYMAPS; ++map ) {
        memset(vga_keymap[map], 0, NR_KEYS*sizeof(Uint16));
        for ( i=0; i<NR_KEYS; ++i ) {
            entry.kb_table = map;
            entry.kb_index = i;
            if ( ioctl(fd, KDGKBENT, &entry) == 0 ) {
                /* fill keytemp. This replaces fbkeys.h */
                if ( (map == 0) && (i<128) ) {
                    keymap_temp[i] = entry.kb_value;
                }
                /* The "Enter" key is a special case */
                if ( entry.kb_value == K_ENTER ) {
                    entry.kb_value = K(KT_ASCII,13);
                }
                /* Handle numpad specially as well */
                if ( KTYP(entry.kb_value) == KT_PAD ) {
                    switch ( entry.kb_value ) {
                    case K_P0:
                    case K_P1:
                    case K_P2:
                    case K_P3:
                    case K_P4:
                    case K_P5:
                    case K_P6:
                    case K_P7:
                    case K_P8:
                    case K_P9:
                        vga_keymap[map][i]=entry.kb_value;
                        vga_keymap[map][i]+= '0';
                        break;
                                        case K_PPLUS:
                        vga_keymap[map][i]=K(KT_ASCII,'+');
                        break;
                                        case K_PMINUS:
                        vga_keymap[map][i]=K(KT_ASCII,'-');
                        break;
                                        case K_PSTAR:
                        vga_keymap[map][i]=K(KT_ASCII,'*');
                        break;
                                        case K_PSLASH:
                        vga_keymap[map][i]=K(KT_ASCII,'/');
                        break;
                                        case K_PENTER:
                        vga_keymap[map][i]=K(KT_ASCII,'\r');
                        break;
                                        case K_PCOMMA:
                        vga_keymap[map][i]=K(KT_ASCII,',');
                        break;
                                        case K_PDOT:
                        vga_keymap[map][i]=K(KT_ASCII,'.');
                        break;
                    default:
                        break;
                    }
                }
                /* Do the normal key translation */
                if ( (KTYP(entry.kb_value) == KT_LATIN) ||
                     (KTYP(entry.kb_value) == KT_ASCII) ||
                     (KTYP(entry.kb_value) == KT_LETTER) ) {
                    vga_keymap[map][i] = entry.kb_value;
                }
            }
        }
    }
}

int FB_InGraphicsMode(/* _THIS */)
{
    return((keyboard_fd >= 0) && (saved_kbd_mode >= 0));
}

int FB_EnterGraphicsMode(/* _THIS */)
{
    struct termios keyboard_termios;

    /* Set medium-raw keyboard mode */
    if ( (keyboard_fd >= 0) && !FB_InGraphicsMode(/* this */) ) {

        /* Switch to the correct virtual terminal */
        if ( current_vt > 0 ) {
            struct vt_stat vtstate;

            if ( ioctl(keyboard_fd, VT_GETSTATE, &vtstate) == 0 ) {
                saved_vt = vtstate.v_active;
            }
            if ( ioctl(keyboard_fd, VT_ACTIVATE, current_vt) == 0 ) {
                ioctl(keyboard_fd, VT_WAITACTIVE, current_vt);
            }
        }

        /* Set the terminal input mode */
        if ( tcgetattr(keyboard_fd, &saved_kbd_termios) < 0 ) {
            SetError("Unable to get terminal attributes");
            if ( keyboard_fd > 0 ) {
                close(keyboard_fd);
            }
            keyboard_fd = -1;
            return(-1);
        }
        if ( ioctl(keyboard_fd, KDGKBMODE, &saved_kbd_mode) < 0 ) {
            SetError("Unable to get current keyboard mode");
            if ( keyboard_fd > 0 ) {
                close(keyboard_fd);
            }
            keyboard_fd = -1;
            return(-1);
        }
        keyboard_termios = saved_kbd_termios;
        keyboard_termios.c_lflag &= ~(ICANON | ECHO | ISIG);
        keyboard_termios.c_iflag &= ~(ISTRIP | IGNCR | ICRNL | INLCR | IXOFF | IXON);
        keyboard_termios.c_cc[VMIN] = 0;
        keyboard_termios.c_cc[VTIME] = 0;
        if (tcsetattr(keyboard_fd, TCSAFLUSH, &keyboard_termios) < 0) {
            FB_CloseKeyboard(/* this */);
            SetError("Unable to set terminal attributes");
            return(-1);
        }
        /* This will fail if we aren't root or this isn't our tty */
        if ( ioctl(keyboard_fd, KDSKBMODE, K_MEDIUMRAW) < 0 ) {
            FB_CloseKeyboard(/* this */);
            SetError("Unable to set keyboard in raw mode");
            return(-1);
        }
        if ( ioctl(keyboard_fd, KDSETMODE, KD_GRAPHICS) < 0 ) {
            FB_CloseKeyboard(/* this */);
            SetError("Unable to set keyboard in graphics mode");
            return(-1);
        }
        /* Prevent switching the virtual terminal */
        ioctl(keyboard_fd, VT_LOCKSWITCH, 1);
    }
    return(keyboard_fd);
}

void FB_LeaveGraphicsMode(/* _THIS */)
{
    if ( FB_InGraphicsMode(/* this */) ) {
        ioctl(keyboard_fd, KDSETMODE, KD_TEXT);
        ioctl(keyboard_fd, KDSKBMODE, saved_kbd_mode);
        tcsetattr(keyboard_fd, TCSAFLUSH, &saved_kbd_termios);
        saved_kbd_mode = -1;

        /* Head back over to the original virtual terminal */
        ioctl(keyboard_fd, VT_UNLOCKSWITCH, 1);
        if ( saved_vt > 0 ) {
            ioctl(keyboard_fd, VT_ACTIVATE, saved_vt);
        }
    }
}

void FB_CloseKeyboard(/* _THIS */)
{
    if ( keyboard_fd >= 0 ) {
        FB_LeaveGraphicsMode(/* this */);
        if ( keyboard_fd > 0 ) {
            close(keyboard_fd);
        }
    }
    keyboard_fd = -1;
}

int FB_OpenKeyboard(/* _THIS */)
{
    printf("FB_OpenKeyboard: keyboard_fd[%d]\n", keyboard_fd);
    /* Open only if not already opened */
     if ( keyboard_fd < 0 ) {
        static const char * const tty0[] = { "/dev/tty0", "/dev/vc/0", NULL };
        static const char * const vcs[] = { "/dev/vc/%d", "/dev/tty%d", NULL };
        int i, tty0_fd;

        /* Try to query for a free virtual terminal */
        tty0_fd = -1;
        for ( i=0; tty0[i] && (tty0_fd < 0); ++i ) {
            tty0_fd = open(tty0[i], O_WRONLY, 0);
        }
        if ( tty0_fd < 0 ) {
            tty0_fd = dup(0); /* Maybe stdin is a VT? */
        }
        ioctl(tty0_fd, VT_OPENQRY, &current_vt);
        close(tty0_fd);
        if ( (geteuid() == 0) && (current_vt > 0) ) {
            for ( i=0; vcs[i] && (keyboard_fd < 0); ++i ) {
                char vtpath[12];

                snprintf(vtpath, arraysize(vtpath), vcs[i], current_vt);
                keyboard_fd = open(vtpath, O_RDWR, 0);
#ifdef DEBUG_KEYBOARD
                fprintf(stderr, "vtpath = %s, fd = %d\n",
                    vtpath, keyboard_fd);
#endif /* DEBUG_KEYBOARD */

                /* This needs to be our controlling tty
                   so that the kernel ioctl() calls work
                */
                if ( keyboard_fd >= 0 ) {
                    tty0_fd = open("/dev/tty", O_RDWR, 0);
                    if ( tty0_fd >= 0 ) {
                        ioctl(tty0_fd, TIOCNOTTY, 0);
                        close(tty0_fd);
                    }
                }
            }
        }
         if ( keyboard_fd < 0 ) {
            /* Last resort, maybe our tty is a usable VT */
            struct vt_stat vtstate;

            keyboard_fd = open("/dev/tty", O_RDWR);

            if ( ioctl(keyboard_fd, VT_GETSTATE, &vtstate) == 0 ) {
                current_vt = vtstate.v_active;
            } else {
                current_vt = 0;
            }
         }
#ifdef DEBUG_KEYBOARD
        fprintf(stderr, "Current VT: %d\n", current_vt);
#endif
         saved_kbd_mode = -1;

        /* Make sure that our input is a console terminal */
        { int dummy;
          if ( ioctl(keyboard_fd, KDGKBMODE, &dummy) < 0 ) {
            close(keyboard_fd);
            keyboard_fd = -1;
            SetError("Unable to open a console terminal");
          }
        }

        /* Set up keymap */
        FB_vgainitkeymaps(keyboard_fd);
     }
    printf("FB_OpenKeyboard: [%d] done\n", keyboard_fd);
     return(keyboard_fd);
}

static enum {
    MOUSE_NONE = -1,
    MOUSE_MSC,    /* Note: GPM uses the MSC protocol */
    MOUSE_PS2,
    MOUSE_IMPS2,
    MOUSE_MS,
    MOUSE_BM,
    MOUSE_ELO,
    MOUSE_TSLIB,
    NUM_MOUSE_DRVS
} mouse_drv = MOUSE_NONE;

void FB_CloseMouse(/* _THIS */)
{
#if INPUT_TSLIB
    if (ts_dev != NULL) {
        ts_close(ts_dev);
        ts_dev = NULL;
        mouse_fd = -1;
    }
#endif /* INPUT_TSLIB */
    if ( mouse_fd > 0 ) {
        close(mouse_fd);
    }
    mouse_fd = -1;
}

/* Returns processes listed in /proc with the desired name */
static int find_pid(DIR *proc, const char *wanted_name)
{
    struct dirent *entry;
    int pid;

    /* First scan proc for the gpm process */
    pid = 0;
    while ( (pid == 0) && ((entry=readdir(proc)) != NULL) ) {
        if ( isdigit(entry->d_name[0]) ) {
            FILE *status;
            char path[PATH_MAX];
            char name[PATH_MAX];

            snprintf(path, arraysize(path), "/proc/%s/status", entry->d_name);
            status=fopen(path, "r");
            if ( status ) {
                int matches = 0;
                name[0] = '\0';
                matches = fscanf(status, "Name: %s", name);
                if ( (matches == 1) && (strcmp(name, wanted_name) == 0) ) {
                    pid = atoi(entry->d_name);
                }
                fclose(status);
            }
        }
    }
    return pid;
}

/* Returns true if /dev/gpmdata is being written to by gpm */
static int gpm_available(char *proto, size_t protolen)
{
    int available;
    DIR *proc;
    int pid;
    int cmdline, len, arglen;
    char path[PATH_MAX];
    char args[PATH_MAX], *arg;

    /* Don't bother looking if the fifo isn't there */
#ifdef DEBUG_MOUSE
    fprintf(stderr,"testing gpm\n");
#endif
    if ( access(GPM_NODE_FIFO, 0) < 0 ) {
        return(0);
    }

    available = 0;
    proc = opendir("/proc");
    if ( proc ) {
        char raw_proto[10] = { '\0' };
        char repeat_proto[10] = { '\0' };
        while ( !available && (pid=find_pid(proc, "gpm")) > 0 ) {
            snprintf(path, arraysize(path), "/proc/%d/cmdline", pid);
            cmdline = open(path, O_RDONLY, 0);
            if ( cmdline >= 0 ) {
                len = read(cmdline, args, sizeof(args));
                arg = args;
                while ( len > 0 ) {
                    arglen = strlen(arg)+1;
#ifdef DEBUG_MOUSE
                        fprintf(stderr,"gpm arg %s len %d\n",arg,arglen);
#endif
                    if ( strcmp(arg, "-t") == 0) {
                        /* protocol string, keep it for later */
                        char *t, *s;
                        t = arg + arglen;
                        s = strchr(t, ' ');
                        if (s) *s = 0;
                        strlcpy(raw_proto, t, arraysize(raw_proto));
                        if (s) *s = ' ';
                    }
                    if ( strncmp(arg, "-R", 2) == 0 ) {
                        char *t, *s;
                        available = 1;
                        t = arg + 2;
                        s = strchr(t, ' ');
                        if (s) *s = 0;
                        strlcpy(repeat_proto, t, arraysize(repeat_proto));
                        if (s) *s = ' ';
                    }
                    len -= arglen;
                    arg += arglen;
                }
                close(cmdline);
            }
        }
        closedir(proc);

        if ( available ) {
            if ( strcmp(repeat_proto, "raw") == 0 ) {
                strlcpy(proto, raw_proto, protolen);
            } else if ( *repeat_proto ) {
                strlcpy(proto, repeat_proto, protolen);
            } else {
                strlcpy(proto, "msc", protolen);
            }
        }
    }
    return available;
}


/* rcg06112001 Set up IMPS/2 mode, if possible. This gives
 *  us access to the mousewheel, etc. Returns zero if
 *  writes to device failed, but you still need to query the
 *  device to see which mode it's actually in.
 */
static int set_imps2_mode(int fd)
{
    /* If you wanted to control the mouse mode (and we do :)  ) ...
        Set IMPS/2 protocol:
            {0xf3,200,0xf3,100,0xf3,80}
        Reset mouse device:
            {0xFF}
    */
    Uint8 set_imps2[] = {0xf3, 200, 0xf3, 100, 0xf3, 80};
    /*Uint8 reset = 0xff;*/
    fd_set fdset;
    struct timeval tv;
    int retval = 0;

    if ( write(fd, &set_imps2, sizeof(set_imps2)) == sizeof(set_imps2) ) {
        /* Don't reset it, that'll clear IMPS/2 mode on some mice
        if (write(fd, &reset, sizeof (reset)) == sizeof (reset) ) {
            retval = 1;
        }
        */
    }

    /* Get rid of any chatter from the above */
    FD_ZERO(&fdset);
    FD_SET(fd, &fdset);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    while ( select(fd+1, &fdset, 0, 0, &tv) > 0 ) {
        char temp[32];
        if (read(fd, temp, sizeof(temp)) <= 0) {
            break;
        }
    }

    return retval;
}


/* Returns true if the mouse uses the IMPS/2 protocol */
static int detect_imps2(int fd)
{
    int imps2;

    imps2 = 0;

    if ( getenv("MOUSEDEV_IMPS2") ) {
        imps2 = 1;
    }
    if ( ! imps2 ) {
        Uint8 query_ps2 = 0xF2;
        fd_set fdset;
        struct timeval tv;

        /* Get rid of any mouse motion noise */
        FD_ZERO(&fdset);
        FD_SET(fd, &fdset);
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        while ( select(fd+1, &fdset, 0, 0, &tv) > 0 ) {
            char temp[32];
            if (read(fd, temp, sizeof(temp)) <= 0) {
                break;
            }
        }

        /* Query for the type of mouse protocol */
        if ( write(fd, &query_ps2, sizeof (query_ps2)) == sizeof (query_ps2)) {
            Uint8 ch = 0;

            /* Get the mouse protocol response */
            do {
                FD_ZERO(&fdset);
                FD_SET(fd, &fdset);
                tv.tv_sec = 1;
                tv.tv_usec = 0;
                if ( select(fd+1, &fdset, 0, 0, &tv) < 1 ) {
                    break;
                }
            } while ( (read(fd, &ch, sizeof (ch)) == sizeof (ch)) &&
                      ((ch == 0xFA) || (ch == 0xAA)) );

            /* Experimental values (Logitech wheelmouse) */
#ifdef DEBUG_MOUSE
fprintf(stderr, "Last mouse mode: 0x%x\n", ch);
#endif
            if ( (ch == 3) || (ch == 4) ) {
                imps2 = 1;
            }
        }
    }
    return imps2;
}

int FB_OpenMouse(/* _THIS */)
{
    int i;
    const char *mousedev;
    const char *mousedrv;

    printf("FB_OpenMouse:\n");
    mousedrv = getenv("MOUSEDRV");
    mousedev = getenv("MOUSEDEV");
    mouse_fd = -1;

    printf("FB_OpenMouse: MOUSEDRV[%s] MOUSEDEV[%s]\n", ((mousedrv==NULL)?"NULL":mousedrv), ((mousedev==NULL)?"NULL":mousedev));
#if INPUT_TSLIB
    if ( mousedrv && (strcmp(mousedrv, "TSLIB") == 0) ) {
        if (mousedev == NULL) mousedev = getenv("TSLIB_TSDEVICE");
        if (mousedev != NULL) {
            ts_dev = ts_open(mousedev, 1);
            if ((ts_dev != NULL) && (ts_config(ts_dev) >= 0)) {
#ifdef DEBUG_MOUSE
                fprintf(stderr, "Using tslib touchscreen\n");
#endif
                mouse_drv = MOUSE_TSLIB;
                mouse_fd = ts_fd(ts_dev);
                return mouse_fd;
            }
        }
        mouse_drv = MOUSE_NONE;
        return mouse_fd;
    }
#endif /* INPUT_TSLIB */

    /* ELO TOUCHSCREEN SUPPORT */
#if 0
    if ( mousedrv && (strcmp(mousedrv, "ELO") == 0) ) {
        mouse_fd = open(mousedev, O_RDWR);
        if ( mouse_fd >= 0 ) {
            if(eloInitController(mouse_fd)) {
#ifdef DEBUG_MOUSE
fprintf(stderr, "Using ELO touchscreen\n");
#endif
                   mouse_drv = MOUSE_ELO;
            }

        }
        else if ( mouse_fd < 0 ) {
            mouse_drv = MOUSE_NONE;
        }

        return(mouse_fd);
    }
#endif

    /* STD MICE */

    if ( mousedev == NULL ) {
        /* FIXME someday... allow multiple mice in this driver */
        static const char *ps2mice[] = {
            "/dev/input/mice", "/dev/usbmouse", "/dev/psaux", NULL
        };
        /* First try to use GPM in repeater mode */
        if ( mouse_fd < 0 ) {
            char proto[10];
            if ( gpm_available(proto, arraysize(proto)) ) {
                mouse_fd = open(GPM_NODE_FIFO, O_RDONLY, 0);
                if ( mouse_fd >= 0 ) {
                    if ( strcmp(proto, "msc") == 0 ) {
                        mouse_drv = MOUSE_MSC;
                    } else if ( strcmp(proto, "ps2") == 0 ) {
                        mouse_drv = MOUSE_PS2;
                    } else if ( strcmp(proto, "imps2") == 0 ) {
                        mouse_drv = MOUSE_IMPS2;
                    } else if ( strcmp(proto, "ms") == 0 ||
                                strcmp(proto, "bare") == 0 ) {
                        mouse_drv = MOUSE_MS;
                    } else if ( strcmp(proto, "bm") == 0 ) {
                        mouse_drv = MOUSE_BM;
                    } else {
                        /* Unknown protocol... */
#ifdef DEBUG_MOUSE
                        fprintf(stderr, "GPM mouse using unknown protocol = %s\n", proto);
#endif
                        close(mouse_fd);
                        mouse_fd = -1;
                    }
                }
#ifdef DEBUG_MOUSE
                if ( mouse_fd >= 0 ) {
                    fprintf(stderr, "Using GPM mouse, protocol = %s\n", proto);
                }
#endif /* DEBUG_MOUSE */
            }
        }
        /* Now try to use a modern PS/2 mouse */
        for ( i=0; (mouse_fd < 0) && ps2mice[i]; ++i ) {
            mouse_fd = open(ps2mice[i], O_RDWR, 0);
            if (mouse_fd < 0) {
                mouse_fd = open(ps2mice[i], O_RDONLY, 0);
            }
            if (mouse_fd >= 0) {
                /* rcg06112001 Attempt to set IMPS/2 mode */
                set_imps2_mode(mouse_fd);
                if (detect_imps2(mouse_fd)) {
#ifdef DEBUG_MOUSE
fprintf(stderr, "Using IMPS2 mouse\n");
#endif
                    mouse_drv = MOUSE_IMPS2;
                } else {
#ifdef DEBUG_MOUSE
fprintf(stderr, "Using PS2 mouse\n");
#endif
                    mouse_drv = MOUSE_PS2;
                }
            }
        }
        /* Next try to use a PPC ADB port mouse */
        if ( mouse_fd < 0 ) {
            mouse_fd = open("/dev/adbmouse", O_RDONLY, 0);
            if ( mouse_fd >= 0 ) {
#ifdef DEBUG_MOUSE
fprintf(stderr, "Using ADB mouse\n");
#endif
                mouse_drv = MOUSE_BM;
            }
        }
    }
    /* Default to a serial Microsoft mouse */
    if ( mouse_fd < 0 ) {
        if ( mousedev == NULL ) {
            mousedev = "/dev/mouse";
        }
        mouse_fd = open(mousedev, O_RDONLY, 0);
        if ( mouse_fd >= 0 ) {
            struct termios mouse_termios;

            /* Set the sampling speed to 1200 baud */
            tcgetattr(mouse_fd, &mouse_termios);
            mouse_termios.c_iflag = IGNBRK | IGNPAR;
            mouse_termios.c_oflag = 0;
            mouse_termios.c_lflag = 0;
            mouse_termios.c_line = 0;
            mouse_termios.c_cc[VTIME] = 0;
            mouse_termios.c_cc[VMIN] = 1;
            mouse_termios.c_cflag = CREAD | CLOCAL | HUPCL;
            mouse_termios.c_cflag |= CS8;
            mouse_termios.c_cflag |= B1200;
            tcsetattr(mouse_fd, TCSAFLUSH, &mouse_termios);
            if ( mousedrv && (strcmp(mousedrv, "PS2") == 0) ) {
#ifdef DEBUG_MOUSE
fprintf(stderr, "Using (user specified) PS2 mouse on %s\n", mousedev);
#endif
                mouse_drv = MOUSE_PS2;
            } else {
#ifdef DEBUG_MOUSE
fprintf(stderr, "Using (default) MS mouse on %s\n", mousedev);
#endif
                mouse_drv = MOUSE_MS;
            }
        }
    }
    if ( mouse_fd < 0 ) {
        mouse_drv = MOUSE_NONE;
    }
    return(mouse_fd);
}

void FB_InitOSKeymap(/* _THIS */)
{
    int i;

    /* Initialize the Linux key translation table */

    /* First get the ascii keys and others not well handled */
    for (i=0; i<arraysize(keymap); ++i) {
      switch(i) {
      /* These aren't handled by the x86 kernel keymapping (?) */
      case SCANCODE_PRINTSCREEN:
        keymap[i] = SDLK_PRINT;
        break;
      case SCANCODE_BREAK:
        keymap[i] = SDLK_BREAK;
        break;
      case SCANCODE_BREAK_ALTERNATIVE:
        keymap[i] = SDLK_PAUSE;
        break;
      case SCANCODE_LEFTSHIFT:
        keymap[i] = SDLK_LSHIFT;
        break;
      case SCANCODE_RIGHTSHIFT:
        keymap[i] = SDLK_RSHIFT;
        break;
      case SCANCODE_LEFTCONTROL:
        keymap[i] = SDLK_LCTRL;
        break;
      case SCANCODE_RIGHTCONTROL:
        keymap[i] = SDLK_RCTRL;
        break;
      case SCANCODE_RIGHTWIN:
        keymap[i] = SDLK_RSUPER;
        break;
      case SCANCODE_LEFTWIN:
        keymap[i] = SDLK_LSUPER;
        break;
      case SCANCODE_LEFTALT:
        keymap[i] = SDLK_LALT;
        break;
      case SCANCODE_RIGHTALT:
        keymap[i] = SDLK_RALT;
        break;
      case 127:
        keymap[i] = SDLK_MENU;
        break;
      /* this should take care of all standard ascii keys */
      default:
        keymap[i] = KVAL(vga_keymap[0][i]);
        break;
          }
    }
    for (i=0; i<arraysize(keymap); ++i) {
      switch(keymap_temp[i]) {
        case K_F1:  keymap[i] = SDLK_F1;  break;
        case K_F2:  keymap[i] = SDLK_F2;  break;
        case K_F3:  keymap[i] = SDLK_F3;  break;
        case K_F4:  keymap[i] = SDLK_F4;  break;
        case K_F5:  keymap[i] = SDLK_F5;  break;
        case K_F6:  keymap[i] = SDLK_F6;  break;
        case K_F7:  keymap[i] = SDLK_F7;  break;
        case K_F8:  keymap[i] = SDLK_F8;  break;
        case K_F9:  keymap[i] = SDLK_F9;  break;
        case K_F10: keymap[i] = SDLK_F10; break;
        case K_F11: keymap[i] = SDLK_F11; break;
        case K_F12: keymap[i] = SDLK_F12; break;

        case K_DOWN:  keymap[i] = SDLK_DOWN;  break;
        case K_LEFT:  keymap[i] = SDLK_LEFT;  break;
        case K_RIGHT: keymap[i] = SDLK_RIGHT; break;
        case K_UP:    keymap[i] = SDLK_UP;    break;

        case K_P0:     keymap[i] = SDLK_KP0; break;
        case K_P1:     keymap[i] = SDLK_KP1; break;
        case K_P2:     keymap[i] = SDLK_KP2; break;
        case K_P3:     keymap[i] = SDLK_KP3; break;
        case K_P4:     keymap[i] = SDLK_KP4; break;
        case K_P5:     keymap[i] = SDLK_KP5; break;
        case K_P6:     keymap[i] = SDLK_KP6; break;
        case K_P7:     keymap[i] = SDLK_KP7; break;
        case K_P8:     keymap[i] = SDLK_KP8; break;
        case K_P9:     keymap[i] = SDLK_KP9; break;
        case K_PPLUS:  keymap[i] = SDLK_KP_PLUS; break;
        case K_PMINUS: keymap[i] = SDLK_KP_MINUS; break;
        case K_PSTAR:  keymap[i] = SDLK_KP_MULTIPLY; break;
        case K_PSLASH: keymap[i] = SDLK_KP_DIVIDE; break;
        case K_PENTER: keymap[i] = SDLK_KP_ENTER; break;
        case K_PDOT:   keymap[i] = SDLK_KP_PERIOD; break;

        case K_SHIFT:  if ( keymap[i] != SDLK_RSHIFT )
                         keymap[i] = SDLK_LSHIFT;
                       break;
        case K_SHIFTL: keymap[i] = SDLK_LSHIFT; break;
        case K_SHIFTR: keymap[i] = SDLK_RSHIFT; break;
        case K_CTRL:  if ( keymap[i] != SDLK_RCTRL )
                         keymap[i] = SDLK_LCTRL;
                       break;
        case K_CTRLL:  keymap[i] = SDLK_LCTRL;  break;
        case K_CTRLR:  keymap[i] = SDLK_RCTRL;  break;
        case K_ALT:    keymap[i] = SDLK_LALT;   break;
        case K_ALTGR:  keymap[i] = SDLK_RALT;   break;

        case K_INSERT: keymap[i] = SDLK_INSERT;   break;
        case K_REMOVE: keymap[i] = SDLK_DELETE;   break;
        case K_PGUP:   keymap[i] = SDLK_PAGEUP;   break;
        case K_PGDN:   keymap[i] = SDLK_PAGEDOWN; break;
        case K_FIND:   keymap[i] = SDLK_HOME;     break;
        case K_SELECT: keymap[i] = SDLK_END;      break;

        case K_NUM:  keymap[i] = SDLK_NUMLOCK;   break;
        case K_CAPS: keymap[i] = SDLK_CAPSLOCK;  break;

        case K_F13:   keymap[i] = SDLK_PRINT;     break;
        case K_HOLD:  keymap[i] = SDLK_SCROLLOCK; break;
        case K_PAUSE: keymap[i] = SDLK_PAUSE;     break;

        case 127: keymap[i] = SDLK_BACKSPACE; break;

        default: break;
      }
    }
}

static keysym *TranslateKey(int scancode, keysym *keysym)
{
    /* Set the keysym information */
    keysym->scancode = scancode;
    keysym->sym = keymap[scancode];
    keysym->mod = KMOD_NONE;

    /* If UNICODE is on, get the UNICODE value for the key */
    keysym->unicode = 0;
    if ( TranslateUNICODE ) {
        int map;
        SDLMod modstate;

        modstate = GetModState();
        map = 0;
        if ( modstate & KMOD_SHIFT ) {
            map |= (1<<KG_SHIFT);
        }
        if ( modstate & KMOD_CTRL ) {
            map |= (1<<KG_CTRL);
        }
        if ( modstate & KMOD_LALT ) {
            map |= (1<<KG_ALT);
        }
        if ( modstate & KMOD_RALT ) {
            map |= (1<<KG_ALTGR);
        }
        if ( KTYP(vga_keymap[map][scancode]) == KT_LETTER ) {
            if ( modstate & KMOD_CAPS ) {
                map ^= (1<<KG_SHIFT);
            }
        }
        if ( KTYP(vga_keymap[map][scancode]) == KT_PAD ) {
            if ( modstate & KMOD_NUM ) {
                keysym->unicode=KVAL(vga_keymap[map][scancode]);
            }
        } else {
            keysym->unicode = KVAL(vga_keymap[map][scancode]);
        }
    }
    return(keysym);
}
