/*
 * Copyright © 2004-2008 Red Hat, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of Red Hat
 * not be used in advertising or publicity pertaining to distribution
 * of the software without specific, written prior permission.  Red
 * Hat makes no representations about the suitability of this software
 * for any purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * THE AUTHORS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:
 *	Kristian Høgsberg (krh@redhat.com)
 *	Adam Jackson (ajax@redhat.com)
 *	Peter Hutterer (peter.hutterer@redhat.com)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <X11/keysym.h>

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <xf86.h>
#include <xf86Xinput.h>
#include <exevents.h>
#include <xorgVersion.h>
#ifdef XKB
#include <xkbsrv.h>
#endif

#include "evdev.h"

#ifdef HAVE_PROPERTIES
#include <X11/Xatom.h>
#include <evdev-properties.h>
#endif


/* 2.4 compatibility */
#ifndef EVIOCGRAB
#define EVIOCGRAB _IOW('E', 0x90, int)
#endif

#ifndef BTN_TASK
#define BTN_TASK 0x117
#endif

#ifndef EV_SYN
#define EV_SYN EV_RST
#endif
/* end compat */

#define ArrayLength(a) (sizeof(a) / (sizeof((a)[0])))

/* evdev flags */
#define EVDEV_KEYBOARD_EVENTS	(1 << 0)
#define EVDEV_BUTTON_EVENTS	(1 << 1)
#define EVDEV_RELATIVE_EVENTS	(1 << 2)
#define EVDEV_ABSOLUTE_EVENTS	(1 << 3)
#define EVDEV_TOUCHPAD		(1 << 4)
#define EVDEV_INITIALIZED	(1 << 5) /* WheelInit etc. called already? */
#define EVDEV_TOUCHSCREEN	(1 << 6)
#define EVDEV_CALIBRATED	(1 << 7) /* run-time calibrated? */

#define MIN_KEYCODE 8
#define GLYPHS_PER_KEY 2
#define AltMask		Mod1Mask
#define NumLockMask	Mod2Mask
#define AltLangMask	Mod3Mask
#define KanaMask	Mod4Mask
#define ScrollLockMask	Mod5Mask

#define CAPSFLAG	1
#define NUMFLAG		2
#define SCROLLFLAG	4
#define MODEFLAG	8
#define COMPOSEFLAG	16

static const char *evdevDefaults[] = {
    "XkbRules",     "evdev",
    "XkbModel",     "evdev",
    "XkbLayout",    "us",
    NULL
};

static int EvdevOn(DeviceIntPtr);
static int EvdevCacheCompare(InputInfoPtr pInfo, BOOL compare);

#ifdef HAVE_PROPERTIES
static void EvdevInitProperty(DeviceIntPtr dev);
static int EvdevSetProperty(DeviceIntPtr dev, Atom atom,
                            XIPropertyValuePtr val, BOOL checkonly);
static Atom prop_invert = 0;
static Atom prop_reopen = 0;
static Atom prop_calibration = 0;
static Atom prop_swap = 0;
#endif


static void
SetXkbOption(InputInfoPtr pInfo, char *name, char **option)
{
    char *s;

    if ((s = xf86SetStrOption(pInfo->options, name, NULL))) {
        if (!s[0]) {
            xfree(s);
            *option = NULL;
        } else {
            *option = s;
            xf86Msg(X_CONFIG, "%s: %s: \"%s\"\n", pInfo->name, name, s);
        }
    }
}

static int wheel_up_button = 4;
static int wheel_down_button = 5;
static int wheel_left_button = 6;
static int wheel_right_button = 7;

static void
PostButtonClicks(InputInfoPtr pInfo, int button, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        xf86PostButtonEvent(pInfo->dev, 0, button, 1, 0, 0);
        xf86PostButtonEvent(pInfo->dev, 0, button, 0, 0, 0);
    }
}

static void
PostKbdEvent(InputInfoPtr pInfo, struct input_event *ev, int value)
{
    int code = ev->code + MIN_KEYCODE;
    static char warned[KEY_MAX];

    /* filter repeat events for chording keys */
    if (value == 2 &&
        (ev->code == KEY_LEFTCTRL || ev->code == KEY_RIGHTCTRL ||
         ev->code == KEY_LEFTSHIFT || ev->code == KEY_RIGHTSHIFT ||
         ev->code == KEY_LEFTALT || ev->code == KEY_RIGHTALT ||
         ev->code == KEY_LEFTMETA || ev->code == KEY_RIGHTMETA ||
         ev->code == KEY_CAPSLOCK || ev->code == KEY_NUMLOCK ||
         ev->code == KEY_SCROLLLOCK)) /* XXX windows keys? */
        return;

    if (code > 255 && ev->code < KEY_MAX) {
	if (!warned[ev->code])
	    xf86Msg(X_WARNING, "%s: unable to handle keycode %d\n",
		    pInfo->name, ev->code);
	warned[ev->code] = 1;
    }

    /* The X server can't handle keycodes > 255 anyway, just drop them.  */
    if (code > 255)
        return;

    xf86PostKeyboardEvent(pInfo->dev, code, value);
}

/**
 * Coming back from resume may leave us with a file descriptor that can be
 * opened but fails on the first read (ENODEV).
 * In this case, try to open the device until it becomes available or until
 * the predefined count expires.
 */
static CARD32
EvdevReopenTimer(OsTimerPtr timer, CARD32 time, pointer arg)
{
    InputInfoPtr pInfo = (InputInfoPtr)arg;
    EvdevPtr pEvdev = pInfo->private;

    do {
        pInfo->fd = open(pEvdev->device, O_RDWR, 0);
    } while (pInfo->fd < 0 && errno == EINTR);

    if (pInfo->fd != -1)
    {
        if (EvdevCacheCompare(pInfo, TRUE) == Success)
        {
            xf86Msg(X_INFO, "%s: Device reopened after %d attempts.\n", pInfo->name,
                    pEvdev->reopen_attempts - pEvdev->reopen_left + 1);
            EvdevOn(pInfo->dev);
        } else
        {
            xf86Msg(X_ERROR, "%s: Device has changed - disabling.\n",
                    pInfo->name);
            DisableDevice(pInfo->dev);
            close(pInfo->fd);
            pInfo->fd = -1;
        }
        pEvdev->reopen_left = 0;
        return 0;
    }

    pEvdev->reopen_left--;

    if (!pEvdev->reopen_left)
    {
        xf86Msg(X_ERROR, "%s: Failed to reopen device after %d attempts.\n",
                pInfo->name, pEvdev->reopen_attempts);
        DisableDevice(pInfo->dev);
        return 0;
    }

    return 100; /* come back in 100 ms */
}

static void
EvdevReadInput(InputInfoPtr pInfo)
{
    struct input_event ev;
    int len, value;
    int dx, dy, tmp;
    unsigned int abs;
    unsigned int button;
    EvdevPtr pEvdev = pInfo->private;

    dx = 0;
    dy = 0;
    tmp = 0;
    abs = 0;

    while (xf86WaitForInput (pInfo->fd, 0) > 0) {
        len = read(pInfo->fd, &ev, sizeof ev);
        if (len != sizeof ev) {
            /* The kernel promises that we always only read a complete
             * event, so len != sizeof ev is an error. */
            xf86Msg(X_ERROR, "%s: Read error: %s\n", pInfo->name, strerror(errno));

            if (errno == ENODEV) /* May happen after resume */
            {
                xf86RemoveEnabledDevice(pInfo);
                close(pInfo->fd);
                pInfo->fd = -1;
                pEvdev->reopen_left = pEvdev->reopen_attempts;
                pEvdev->reopen_timer = TimerSet(NULL, 0, 100, EvdevReopenTimer, pInfo);
            }
            break;
        }


        /* Get the signed value, earlier kernels had this as unsigned */
        value = ev.value;

        switch (ev.type) {
	case EV_REL:
	    /* Handle mouse wheel emulation */
	    if (EvdevWheelEmuFilterMotion(pInfo, &ev))
		break;

            switch (ev.code) {
            case REL_X:
                dx += value;
                break;

            case REL_Y:
                dy += value;
                break;

            case REL_WHEEL:
                if (value > 0)
                    PostButtonClicks(pInfo, wheel_up_button, value);
                else if (value < 0)
                    PostButtonClicks(pInfo, wheel_down_button, -value);
                break;

	    case REL_DIAL:
            case REL_HWHEEL:
                if (value > 0)
                    PostButtonClicks(pInfo, wheel_right_button, value);
                else if (value < 0)
                    PostButtonClicks(pInfo, wheel_left_button, -value);
                break;
            }
            break;

	case EV_ABS:
	    switch (ev.code) {
	    case ABS_X:
		pEvdev->abs_x = value;
		abs = 1;
		break;
	    case ABS_Y:
		pEvdev->abs_y = value;
		abs = 1;
		break;
	    }
	    break;

        case EV_KEY:
	    /* don't repeat mouse buttons */
	    if (ev.code >= BTN_MOUSE && ev.code < KEY_OK)
		if (value == 2)
		    break;

            switch (ev.code) {
	    case BTN_TOUCH:
	    case BTN_TOOL_PEN:
	    case BTN_TOOL_RUBBER:
	    case BTN_TOOL_BRUSH:
	    case BTN_TOOL_PENCIL:
	    case BTN_TOOL_AIRBRUSH:
	    case BTN_TOOL_FINGER:
	    case BTN_TOOL_MOUSE:
	    case BTN_TOOL_LENS:
		pEvdev->tool = value ? ev.code : 0;
		if (!(pEvdev->flags & EVDEV_TOUCHSCREEN))
		    break;
		/* Treat BTN_TOUCH from devices that only have BTN_TOUCH as
                 * BTN_LEFT. */
		ev.code = BTN_LEFT;
                /* Intentional fallthrough! */

            default:
		button = EvdevUtilButtonEventToButtonNumber(pEvdev, ev.code);

		/* Handle drag lock */
		if (EvdevDragLockFilterEvent(pInfo, button, value))
		    break;

		if (EvdevWheelEmuFilterButton(pInfo, button, value))
		   break;

		if (EvdevMBEmuFilterEvent(pInfo, button, value))
		   break;

		if (button)
		    xf86PostButtonEvent(pInfo->dev, 0, button, value, 0, 0);
		else
		    PostKbdEvent(pInfo, &ev, value);
		break;
            }
            break;

        case EV_SYN:
            break;
        }
    }

    /* convert to relative motion for touchpads */
    if (pEvdev->flags & EVDEV_TOUCHPAD) {
	abs = 0;
	if (pEvdev->tool) { /* meaning, touch is active */
	    if (pEvdev->old_x != -1)
		dx = pEvdev->abs_x - pEvdev->old_x;
	    if (pEvdev->old_y != -1)
		dy = pEvdev->abs_y - pEvdev->old_y;
	    pEvdev->old_x = pEvdev->abs_x;
	    pEvdev->old_y = pEvdev->abs_y;
	} else {
	    pEvdev->old_x = pEvdev->old_y = -1;
	}
    }

    if (dx != 0 || dy != 0) {
        if (pEvdev->swap_axes) {
            tmp = dx;
            dx = dy;
            dy = tmp;
        }
        if (pEvdev->invert_x)
            dx *= -1;
        if (pEvdev->invert_y)
            dy *= -1;
        xf86PostMotionEvent(pInfo->dev, FALSE, 0, 2, dx, dy);
    }

    /*
     * Some devices only generate valid abs coords when BTN_DIGI is
     * pressed.  On wacom tablets, this means that the pen is in
     * proximity of the tablet.  After the pen is removed, BTN_DIGI is
     * released, and a (0, 0) absolute event is generated.  Checking
     * pEvdev->digi here, lets us ignore that event.  pEvdev is
     * initialized to 1 so devices that doesn't use this scheme still
     * just works.
     */
    if (abs && pEvdev->tool) {
        int abs_x, abs_y;
        abs_x = (pEvdev->swap_axes) ? pEvdev->abs_y : pEvdev->abs_x;
        abs_y = (pEvdev->swap_axes) ? pEvdev->abs_x : pEvdev->abs_y;

        if (pEvdev->flags & EVDEV_CALIBRATED)
        {
            abs_x = xf86ScaleAxis(abs_x,
                    pEvdev->max_x, pEvdev->min_x,
                    pEvdev->calibration.max_x, pEvdev->calibration.min_x);
            abs_y = xf86ScaleAxis(abs_y,
                    pEvdev->max_y, pEvdev->min_y,
                    pEvdev->calibration.max_y, pEvdev->calibration.min_y);
        }

        if (pEvdev->invert_x)
            abs_x = pEvdev->max_x - (abs_x - pEvdev->min_x);
        if (pEvdev->invert_y)
            abs_y = pEvdev->max_y - (abs_y - pEvdev->min_y);

	xf86PostMotionEvent(pInfo->dev, TRUE, 0, 2, abs_x, abs_y);
    }
}

#define TestBit(bit, array) (array[(bit) / LONG_BITS]) & (1L << ((bit) % LONG_BITS))

static void
EvdevPtrCtrlProc(DeviceIntPtr device, PtrCtrl *ctrl)
{
    /* Nothing to do, dix handles all settings */
}

static KeySym map[] = {
    /* 0x00 */  NoSymbol,       NoSymbol,
    /* 0x01 */  XK_Escape,      NoSymbol,
    /* 0x02 */  XK_1,           XK_exclam,
    /* 0x03 */  XK_2,           XK_at,
    /* 0x04 */  XK_3,           XK_numbersign,
    /* 0x05 */  XK_4,           XK_dollar,
    /* 0x06 */  XK_5,           XK_percent,
    /* 0x07 */  XK_6,           XK_asciicircum,
    /* 0x08 */  XK_7,           XK_ampersand,
    /* 0x09 */  XK_8,           XK_asterisk,
    /* 0x0a */  XK_9,           XK_parenleft,
    /* 0x0b */  XK_0,           XK_parenright,
    /* 0x0c */  XK_minus,       XK_underscore,
    /* 0x0d */  XK_equal,       XK_plus,
    /* 0x0e */  XK_BackSpace,   NoSymbol,
    /* 0x0f */  XK_Tab,         XK_ISO_Left_Tab,
    /* 0x10 */  XK_Q,           NoSymbol,
    /* 0x11 */  XK_W,           NoSymbol,
    /* 0x12 */  XK_E,           NoSymbol,
    /* 0x13 */  XK_R,           NoSymbol,
    /* 0x14 */  XK_T,           NoSymbol,
    /* 0x15 */  XK_Y,           NoSymbol,
    /* 0x16 */  XK_U,           NoSymbol,
    /* 0x17 */  XK_I,           NoSymbol,
    /* 0x18 */  XK_O,           NoSymbol,
    /* 0x19 */  XK_P,           NoSymbol,
    /* 0x1a */  XK_bracketleft, XK_braceleft,
    /* 0x1b */  XK_bracketright,XK_braceright,
    /* 0x1c */  XK_Return,      NoSymbol,
    /* 0x1d */  XK_Control_L,   NoSymbol,
    /* 0x1e */  XK_A,           NoSymbol,
    /* 0x1f */  XK_S,           NoSymbol,
    /* 0x20 */  XK_D,           NoSymbol,
    /* 0x21 */  XK_F,           NoSymbol,
    /* 0x22 */  XK_G,           NoSymbol,
    /* 0x23 */  XK_H,           NoSymbol,
    /* 0x24 */  XK_J,           NoSymbol,
    /* 0x25 */  XK_K,           NoSymbol,
    /* 0x26 */  XK_L,           NoSymbol,
    /* 0x27 */  XK_semicolon,   XK_colon,
    /* 0x28 */  XK_quoteright,  XK_quotedbl,
    /* 0x29 */  XK_quoteleft,	XK_asciitilde,
    /* 0x2a */  XK_Shift_L,     NoSymbol,
    /* 0x2b */  XK_backslash,   XK_bar,
    /* 0x2c */  XK_Z,           NoSymbol,
    /* 0x2d */  XK_X,           NoSymbol,
    /* 0x2e */  XK_C,           NoSymbol,
    /* 0x2f */  XK_V,           NoSymbol,
    /* 0x30 */  XK_B,           NoSymbol,
    /* 0x31 */  XK_N,           NoSymbol,
    /* 0x32 */  XK_M,           NoSymbol,
    /* 0x33 */  XK_comma,       XK_less,
    /* 0x34 */  XK_period,      XK_greater,
    /* 0x35 */  XK_slash,       XK_question,
    /* 0x36 */  XK_Shift_R,     NoSymbol,
    /* 0x37 */  XK_KP_Multiply, NoSymbol,
    /* 0x38 */  XK_Alt_L,	XK_Meta_L,
    /* 0x39 */  XK_space,       NoSymbol,
    /* 0x3a */  XK_Caps_Lock,   NoSymbol,
    /* 0x3b */  XK_F1,          NoSymbol,
    /* 0x3c */  XK_F2,          NoSymbol,
    /* 0x3d */  XK_F3,          NoSymbol,
    /* 0x3e */  XK_F4,          NoSymbol,
    /* 0x3f */  XK_F5,          NoSymbol,
    /* 0x40 */  XK_F6,          NoSymbol,
    /* 0x41 */  XK_F7,          NoSymbol,
    /* 0x42 */  XK_F8,          NoSymbol,
    /* 0x43 */  XK_F9,          NoSymbol,
    /* 0x44 */  XK_F10,         NoSymbol,
    /* 0x45 */  XK_Num_Lock,    NoSymbol,
    /* 0x46 */  XK_Scroll_Lock,	NoSymbol,
    /* These KP keys should have the KP_7 keysyms in the numlock
     * modifer... ? */
    /* 0x47 */  XK_KP_Home,	XK_KP_7,
    /* 0x48 */  XK_KP_Up,	XK_KP_8,
    /* 0x49 */  XK_KP_Prior,	XK_KP_9,
    /* 0x4a */  XK_KP_Subtract, NoSymbol,
    /* 0x4b */  XK_KP_Left,	XK_KP_4,
    /* 0x4c */  XK_KP_Begin,	XK_KP_5,
    /* 0x4d */  XK_KP_Right,	XK_KP_6,
    /* 0x4e */  XK_KP_Add,      NoSymbol,
    /* 0x4f */  XK_KP_End,	XK_KP_1,
    /* 0x50 */  XK_KP_Down,	XK_KP_2,
    /* 0x51 */  XK_KP_Next,	XK_KP_3,
    /* 0x52 */  XK_KP_Insert,	XK_KP_0,
    /* 0x53 */  XK_KP_Delete,	XK_KP_Decimal,
    /* 0x54 */  NoSymbol,	NoSymbol,
    /* 0x55 */  XK_F13,		NoSymbol,
    /* 0x56 */  XK_less,	XK_greater,
    /* 0x57 */  XK_F11,		NoSymbol,
    /* 0x58 */  XK_F12,		NoSymbol,
    /* 0x59 */  XK_F14,		NoSymbol,
    /* 0x5a */  XK_F15,		NoSymbol,
    /* 0x5b */  XK_F16,		NoSymbol,
    /* 0x5c */  XK_F17,		NoSymbol,
    /* 0x5d */  XK_F18,		NoSymbol,
    /* 0x5e */  XK_F19,		NoSymbol,
    /* 0x5f */  XK_F20,		NoSymbol,
    /* 0x60 */  XK_KP_Enter,	NoSymbol,
    /* 0x61 */  XK_Control_R,	NoSymbol,
    /* 0x62 */  XK_KP_Divide,	NoSymbol,
    /* 0x63 */  XK_Print,	XK_Sys_Req,
    /* 0x64 */  XK_Alt_R,	XK_Meta_R,
    /* 0x65 */  NoSymbol,	NoSymbol, /* KEY_LINEFEED */
    /* 0x66 */  XK_Home,	NoSymbol,
    /* 0x67 */  XK_Up,		NoSymbol,
    /* 0x68 */  XK_Prior,	NoSymbol,
    /* 0x69 */  XK_Left,	NoSymbol,
    /* 0x6a */  XK_Right,	NoSymbol,
    /* 0x6b */  XK_End,		NoSymbol,
    /* 0x6c */  XK_Down,	NoSymbol,
    /* 0x6d */  XK_Next,	NoSymbol,
    /* 0x6e */  XK_Insert,	NoSymbol,
    /* 0x6f */  XK_Delete,	NoSymbol,
    /* 0x70 */  NoSymbol,	NoSymbol, /* KEY_MACRO */
    /* 0x71 */  NoSymbol,	NoSymbol,
    /* 0x72 */  NoSymbol,	NoSymbol,
    /* 0x73 */  NoSymbol,	NoSymbol,
    /* 0x74 */  NoSymbol,	NoSymbol,
    /* 0x75 */  XK_KP_Equal,	NoSymbol,
    /* 0x76 */  NoSymbol,	NoSymbol,
    /* 0x77 */  NoSymbol,	NoSymbol,
    /* 0x78 */  XK_F21,		NoSymbol,
    /* 0x79 */  XK_F22,		NoSymbol,
    /* 0x7a */  XK_F23,		NoSymbol,
    /* 0x7b */  XK_F24,		NoSymbol,
    /* 0x7c */  XK_KP_Separator, NoSymbol,
    /* 0x7d */  XK_Meta_L,	NoSymbol,
    /* 0x7e */  XK_Meta_R,	NoSymbol,
    /* 0x7f */  XK_Multi_key,	NoSymbol,
    /* 0x80 */  NoSymbol,	NoSymbol,
    /* 0x81 */  NoSymbol,	NoSymbol,
    /* 0x82 */  NoSymbol,	NoSymbol,
    /* 0x83 */  NoSymbol,	NoSymbol,
    /* 0x84 */  NoSymbol,	NoSymbol,
    /* 0x85 */  NoSymbol,	NoSymbol,
    /* 0x86 */  NoSymbol,	NoSymbol,
    /* 0x87 */  NoSymbol,	NoSymbol,
    /* 0x88 */  NoSymbol,	NoSymbol,
    /* 0x89 */  NoSymbol,	NoSymbol,
    /* 0x8a */  NoSymbol,	NoSymbol,
    /* 0x8b */  NoSymbol,	NoSymbol,
    /* 0x8c */  NoSymbol,	NoSymbol,
    /* 0x8d */  NoSymbol,	NoSymbol,
    /* 0x8e */  NoSymbol,	NoSymbol,
    /* 0x8f */  NoSymbol,	NoSymbol,
    /* 0x90 */  NoSymbol,	NoSymbol,
    /* 0x91 */  NoSymbol,	NoSymbol,
    /* 0x92 */  NoSymbol,	NoSymbol,
    /* 0x93 */  NoSymbol,	NoSymbol,
    /* 0x94 */  NoSymbol,	NoSymbol,
    /* 0x95 */  NoSymbol,	NoSymbol,
    /* 0x96 */  NoSymbol,	NoSymbol,
    /* 0x97 */  NoSymbol,	NoSymbol,
    /* 0x98 */  NoSymbol,	NoSymbol,
    /* 0x99 */  NoSymbol,	NoSymbol,
    /* 0x9a */  NoSymbol,	NoSymbol,
    /* 0x9b */  NoSymbol,	NoSymbol,
    /* 0x9c */  NoSymbol,	NoSymbol,
    /* 0x9d */  NoSymbol,	NoSymbol,
    /* 0x9e */  NoSymbol,	NoSymbol,
    /* 0x9f */  NoSymbol,	NoSymbol,
    /* 0xa0 */  NoSymbol,	NoSymbol,
    /* 0xa1 */  NoSymbol,	NoSymbol,
    /* 0xa2 */  NoSymbol,	NoSymbol,
    /* 0xa3 */  NoSymbol,	NoSymbol,
    /* 0xa4 */  NoSymbol,	NoSymbol,
    /* 0xa5 */  NoSymbol,	NoSymbol,
    /* 0xa6 */  NoSymbol,	NoSymbol,
    /* 0xa7 */  NoSymbol,	NoSymbol,
    /* 0xa8 */  NoSymbol,	NoSymbol,
    /* 0xa9 */  NoSymbol,	NoSymbol,
    /* 0xaa */  NoSymbol,	NoSymbol,
    /* 0xab */  NoSymbol,	NoSymbol,
    /* 0xac */  NoSymbol,	NoSymbol,
    /* 0xad */  NoSymbol,	NoSymbol,
    /* 0xae */  NoSymbol,	NoSymbol,
    /* 0xaf */  NoSymbol,	NoSymbol,
    /* 0xb0 */  NoSymbol,	NoSymbol,
    /* 0xb1 */  NoSymbol,	NoSymbol,
    /* 0xb2 */  NoSymbol,	NoSymbol,
    /* 0xb3 */  NoSymbol,	NoSymbol,
    /* 0xb4 */  NoSymbol,	NoSymbol,
    /* 0xb5 */  NoSymbol,	NoSymbol,
    /* 0xb6 */  NoSymbol,	NoSymbol,
    /* 0xb7 */  NoSymbol,	NoSymbol,
    /* 0xb8 */  NoSymbol,	NoSymbol,
    /* 0xb9 */  NoSymbol,	NoSymbol,
    /* 0xba */  NoSymbol,	NoSymbol,
    /* 0xbb */  NoSymbol,	NoSymbol,
    /* 0xbc */  NoSymbol,	NoSymbol,
    /* 0xbd */  NoSymbol,	NoSymbol,
    /* 0xbe */  NoSymbol,	NoSymbol,
    /* 0xbf */  NoSymbol,	NoSymbol,
    /* 0xc0 */  NoSymbol,	NoSymbol,
    /* 0xc1 */  NoSymbol,	NoSymbol,
    /* 0xc2 */  NoSymbol,	NoSymbol,
    /* 0xc3 */  NoSymbol,	NoSymbol,
    /* 0xc4 */  NoSymbol,	NoSymbol,
    /* 0xc5 */  NoSymbol,	NoSymbol,
    /* 0xc6 */  NoSymbol,	NoSymbol,
    /* 0xc7 */  NoSymbol,	NoSymbol,
    /* 0xc8 */  NoSymbol,	NoSymbol,
    /* 0xc9 */  NoSymbol,	NoSymbol,
    /* 0xca */  NoSymbol,	NoSymbol,
    /* 0xcb */  NoSymbol,	NoSymbol,
    /* 0xcc */  NoSymbol,	NoSymbol,
    /* 0xcd */  NoSymbol,	NoSymbol,
    /* 0xce */  NoSymbol,	NoSymbol,
    /* 0xcf */  NoSymbol,	NoSymbol,
    /* 0xd0 */  NoSymbol,	NoSymbol,
    /* 0xd1 */  NoSymbol,	NoSymbol,
    /* 0xd2 */  NoSymbol,	NoSymbol,
    /* 0xd3 */  NoSymbol,	NoSymbol,
    /* 0xd4 */  NoSymbol,	NoSymbol,
    /* 0xd5 */  NoSymbol,	NoSymbol,
    /* 0xd6 */  NoSymbol,	NoSymbol,
    /* 0xd7 */  NoSymbol,	NoSymbol,
    /* 0xd8 */  NoSymbol,	NoSymbol,
    /* 0xd9 */  NoSymbol,	NoSymbol,
    /* 0xda */  NoSymbol,	NoSymbol,
    /* 0xdb */  NoSymbol,	NoSymbol,
    /* 0xdc */  NoSymbol,	NoSymbol,
    /* 0xdd */  NoSymbol,	NoSymbol,
    /* 0xde */  NoSymbol,	NoSymbol,
    /* 0xdf */  NoSymbol,	NoSymbol,
    /* 0xe0 */  NoSymbol,	NoSymbol,
    /* 0xe1 */  NoSymbol,	NoSymbol,
    /* 0xe2 */  NoSymbol,	NoSymbol,
    /* 0xe3 */  NoSymbol,	NoSymbol,
    /* 0xe4 */  NoSymbol,	NoSymbol,
    /* 0xe5 */  NoSymbol,	NoSymbol,
    /* 0xe6 */  NoSymbol,	NoSymbol,
    /* 0xe7 */  NoSymbol,	NoSymbol,
    /* 0xe8 */  NoSymbol,	NoSymbol,
    /* 0xe9 */  NoSymbol,	NoSymbol,
    /* 0xea */  NoSymbol,	NoSymbol,
    /* 0xeb */  NoSymbol,	NoSymbol,
    /* 0xec */  NoSymbol,	NoSymbol,
    /* 0xed */  NoSymbol,	NoSymbol,
    /* 0xee */  NoSymbol,	NoSymbol,
    /* 0xef */  NoSymbol,	NoSymbol,
    /* 0xf0 */  NoSymbol,	NoSymbol,
    /* 0xf1 */  NoSymbol,	NoSymbol,
    /* 0xf2 */  NoSymbol,	NoSymbol,
    /* 0xf3 */  NoSymbol,	NoSymbol,
    /* 0xf4 */  NoSymbol,	NoSymbol,
    /* 0xf5 */  NoSymbol,	NoSymbol,
    /* 0xf6 */  NoSymbol,	NoSymbol,
    /* 0xf7 */  NoSymbol,	NoSymbol,
};

static void
EvdevKbdCtrl(DeviceIntPtr device, KeybdCtrl *ctrl)
{
    static struct { int xbit, code; } bits[] = {
        { CAPSFLAG,	LED_CAPSL },
        { NUMFLAG,	LED_NUML },
        { SCROLLFLAG,	LED_SCROLLL },
        { MODEFLAG,	LED_KANA },
        { COMPOSEFLAG,	LED_COMPOSE }
    };

    InputInfoPtr pInfo;
    struct input_event ev[ArrayLength(bits)];
    int i;

    memset(ev, 0, sizeof(ev));

    pInfo = device->public.devicePrivate;
    for (i = 0; i < ArrayLength(bits); i++) {
        ev[i].type = EV_LED;
        ev[i].code = bits[i].code;
        ev[i].value = (ctrl->leds & bits[i].xbit) > 0;
    }

    write(pInfo->fd, ev, sizeof ev);
}

static int
EvdevAddKeyClass(DeviceIntPtr device)
{
    InputInfoPtr pInfo;
    KeySymsRec keySyms;
    CARD8 modMap[MAP_LENGTH];
    KeySym sym;
    int i, j;
    EvdevPtr pEvdev;

    static struct { KeySym keysym; CARD8 mask; } modifiers[] = {
        { XK_Shift_L,		ShiftMask },
        { XK_Shift_R,		ShiftMask },
        { XK_Control_L,		ControlMask },
        { XK_Control_R,		ControlMask },
        { XK_Caps_Lock,		LockMask },
        { XK_Alt_L,		AltMask },
        { XK_Alt_R,		AltMask },
	{ XK_Meta_L,		Mod4Mask },
	{ XK_Meta_R,		Mod4Mask },
        { XK_Num_Lock,		NumLockMask },
        { XK_Scroll_Lock,	ScrollLockMask },
        { XK_Mode_switch,	AltLangMask }
    };

    pInfo = device->public.devicePrivate;
    pEvdev = pInfo->private;

     /* Compute the modifier map */
    memset(modMap, 0, sizeof modMap);

    for (i = 0; i < ArrayLength(map) / GLYPHS_PER_KEY; i++) {
        sym = map[i * GLYPHS_PER_KEY];
        for (j = 0; j < ArrayLength(modifiers); j++) {
            if (modifiers[j].keysym == sym)
                modMap[i + MIN_KEYCODE] = modifiers[j].mask;
        }
    }

    keySyms.map        = map;
    keySyms.mapWidth   = GLYPHS_PER_KEY;
    keySyms.minKeyCode = MIN_KEYCODE;
    keySyms.maxKeyCode = MIN_KEYCODE + ArrayLength(map) / GLYPHS_PER_KEY - 1;

#ifdef XKB
    if (pEvdev->noXkb)
#endif
    {
        xf86Msg(X_CONFIG, "XKB: disabled\n");
        if (!InitKeyboardDeviceStruct((DevicePtr)device, &keySyms, modMap,
                                      NULL, EvdevKbdCtrl))
            return !Success;
    }
#ifdef XKB
    else
    {
	/* sorry, no rules change allowed for you */
	xf86ReplaceStrOption(pInfo->options, "xkb_rules", "evdev");
        SetXkbOption(pInfo, "xkb_rules", &pEvdev->xkb_rules);
        SetXkbOption(pInfo, "xkb_model", &pEvdev->xkb_model);
	if (!pEvdev->xkb_model)
	    SetXkbOption(pInfo, "XkbModel", &pEvdev->xkb_rules);
        SetXkbOption(pInfo, "xkb_layout", &pEvdev->xkb_layout);
	if (!pEvdev->xkb_layout)
	    SetXkbOption(pInfo, "XkbLayout", &pEvdev->xkb_layout);
        SetXkbOption(pInfo, "xkb_variant", &pEvdev->xkb_variant);
	if (!pEvdev->xkb_variant)
	    SetXkbOption(pInfo, "XkbVariant", &pEvdev->xkb_variant);
        SetXkbOption(pInfo, "xkb_options", &pEvdev->xkb_options);
	if (!pEvdev->xkb_options)
	    SetXkbOption(pInfo, "XkbOptions", &pEvdev->xkb_options);

        XkbSetRulesDflts(pEvdev->xkb_rules, pEvdev->xkb_model,
                         pEvdev->xkb_layout, pEvdev->xkb_variant,
                         pEvdev->xkb_options);
        if (!XkbInitKeyboardDeviceStruct(device, &pEvdev->xkbnames,
                                         &keySyms, modMap, NULL,
                                         EvdevKbdCtrl))
            return !Success;
    }
#endif

    pInfo->flags |= XI86_KEYBOARD_CAPABLE;

    return Success;
}

static int
EvdevAddAbsClass(DeviceIntPtr device)
{
    InputInfoPtr pInfo;
    EvdevPtr pEvdev;
    struct input_absinfo absinfo_x, absinfo_y;

    pInfo = device->public.devicePrivate;
    pEvdev = pInfo->private;

    if (ioctl(pInfo->fd,
	      EVIOCGABS(ABS_X), &absinfo_x) < 0) {
	xf86Msg(X_ERROR, "ioctl EVIOCGABS failed: %s\n", strerror(errno));
	return !Success;
    }

    if (ioctl(pInfo->fd,
	      EVIOCGABS(ABS_Y), &absinfo_y) < 0) {
	xf86Msg(X_ERROR, "ioctl EVIOCGABS failed: %s\n", strerror(errno));
	return !Success;
    }

    pEvdev->min_x = absinfo_x.minimum;
    pEvdev->max_x = absinfo_x.maximum;
    pEvdev->min_y = absinfo_y.minimum;
    pEvdev->max_y = absinfo_y.maximum;

    if (!InitValuatorClassDeviceStruct(device, 2,
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) < 3
                                       GetMotionHistory,
#endif
                                       GetMotionHistorySize(), Absolute))
        return !Success;

    /* X valuator */
    xf86InitValuatorAxisStruct(device, 0, pEvdev->min_x, pEvdev->max_x,
			       10000, 0, 10000);
    xf86InitValuatorDefaults(device, 0);

    /* Y valuator */
    xf86InitValuatorAxisStruct(device, 1, pEvdev->min_y, pEvdev->max_y,
			       10000, 0, 10000);
    xf86InitValuatorDefaults(device, 1);
    xf86MotionHistoryAllocate(pInfo);

    if (!InitPtrFeedbackClassDeviceStruct(device, EvdevPtrCtrlProc))
        return !Success;

    pInfo->flags |= XI86_POINTER_CAPABLE;

    return Success;
}

static int
EvdevAddRelClass(DeviceIntPtr device)
{
    InputInfoPtr pInfo;

    pInfo = device->public.devicePrivate;

    if (!InitValuatorClassDeviceStruct(device, 2,
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) < 3
                                       GetMotionHistory,
#endif
                                       GetMotionHistorySize(), Relative))
        return !Success;

    /* X valuator */
    xf86InitValuatorAxisStruct(device, 0, -1, -1, 1, 0, 1);
    xf86InitValuatorDefaults(device, 0);

    /* Y valuator */
    xf86InitValuatorAxisStruct(device, 1, -1, -1, 1, 0, 1);
    xf86InitValuatorDefaults(device, 1);
    xf86MotionHistoryAllocate(pInfo);

    if (!InitPtrFeedbackClassDeviceStruct(device, EvdevPtrCtrlProc))
        return !Success;

    pInfo->flags |= XI86_POINTER_CAPABLE;

    xf86MotionHistoryAllocate(pInfo);

    return Success;
}

static int
EvdevAddButtonClass(DeviceIntPtr device)
{
    InputInfoPtr pInfo;
    EvdevPtr pEvdev;

    pInfo = device->public.devicePrivate;
    pEvdev = pInfo->private;

    /* FIXME: count number of actual buttons */
    if (!InitButtonClassDeviceStruct(device, ArrayLength(pEvdev->btnmap),
                                     pEvdev->btnmap))
        return !Success;

    return Success;
}

/**
 * Init the button mapping for the device. By default, this is a 1:1 mapping,
 * i.e. Button 1 maps to Button 1, Button 2 to 2, etc.
 *
 * If a mapping has been specified, the mapping is the default, with the
 * user-defined ones overwriting the defaults.
 * i.e. a user-defined mapping of "3 2 1" results in a mapping of 3 2 1 4 5 6 ...
 *
 * Invalid button mappings revert to the default.
 *
 * Note that index 0 is unused, button 0 does not exist.
 * This mapping is initialised for all devices, but only applied if the device
 * has buttons (in EvdevAddButtonClass).
 */
static void
EvdevInitButtonMapping(InputInfoPtr pInfo)
{
    int         i, nbuttons     = 1;
    char       *mapping         = NULL;
    EvdevPtr    pEvdev          = pInfo->private;

    /* Check for user-defined button mapping */
    if ((mapping = xf86CheckStrOption(pInfo->options, "ButtonMapping", NULL)))
    {
        char    *s  = " ";
        int     btn = 0;

        xf86Msg(X_CONFIG, "%s: ButtonMapping '%s'\n", pInfo->name, mapping);
        while (s && *s != '\0' && nbuttons < EVDEV_MAXBUTTONS)
        {
            btn = strtol(mapping, &s, 10);

            if (s == mapping || btn < 0 || btn > EVDEV_MAXBUTTONS)
            {
                xf86Msg(X_ERROR,
                        "%s: ... Invalid button mapping. Using defaults\n",
                        pInfo->name);
                nbuttons = 1; /* ensure defaults start at 1 */
                break;
            }

            pEvdev->btnmap[nbuttons++] = btn;
            mapping = s;
        }
    }

    for (i = nbuttons; i < ArrayLength(pEvdev->btnmap); i++)
        pEvdev->btnmap[i] = i;

}

static int
EvdevInit(DeviceIntPtr device)
{
    InputInfoPtr pInfo;
    EvdevPtr pEvdev;

    pInfo = device->public.devicePrivate;
    pEvdev = pInfo->private;

    /* FIXME: This doesn't add buttons for keyboards with scrollwheels. */

    if (pEvdev->flags & EVDEV_KEYBOARD_EVENTS)
	EvdevAddKeyClass(device);
    if (pEvdev->flags & EVDEV_BUTTON_EVENTS)
	EvdevAddButtonClass(device);
    /* We don't allow relative and absolute axes on the same device. Reason
       Reason being that some devices (MS Optical Desktop 2000) register both
       rel and abs axes for x/y.
       The abs axes register min/max, this min/max then also applies to the
       relative device (the mouse) and caps it at 0..255 for both axis.
       So unless you have a small screen, you won't be enjoying it much.

        FIXME: somebody volunteer to fix this.
     */
    if (pEvdev->flags & EVDEV_RELATIVE_EVENTS)
	EvdevAddRelClass(device);
    else if (pEvdev->flags & EVDEV_ABSOLUTE_EVENTS)
        EvdevAddAbsClass(device);

#ifdef HAVE_PROPERTIES
    /* We drop the return value, the only time we ever want the handlers to
     * unregister is when the device dies. In which case we don't have to
     * unregister anyway */
    XIRegisterPropertyHandler(device, EvdevSetProperty, NULL, NULL);
    EvdevInitProperty(device);
    EvdevMBEmuInitProperty(device);
    EvdevWheelEmuInitProperty(device);
    EvdevDragLockInitProperty(device);
#endif

    return Success;
}

/**
 * Init all extras (wheel emulation, etc.) and grab the device.
 *
 * Coming from a resume, the grab may fail with ENODEV. In this case, we set a
 * timer to wake up and try to reopen the device later.
 */
static int
EvdevOn(DeviceIntPtr device)
{
    InputInfoPtr pInfo;
    EvdevPtr pEvdev;
    int rc = 0;

    pInfo = device->public.devicePrivate;
    pEvdev = pInfo->private;

    if (pInfo->fd != -1 && pEvdev->grabDevice &&
        (rc = ioctl(pInfo->fd, EVIOCGRAB, (void *)1)))
    {
        xf86Msg(X_WARNING, "%s: Grab failed (%s)\n", pInfo->name,
                strerror(errno));

        /* ENODEV - device has disappeared after resume */
        if (rc && errno == ENODEV)
        {
            close(pInfo->fd);
            pInfo->fd = -1;
        }
    }

    if (pInfo->fd == -1)
    {
        pEvdev->reopen_left = pEvdev->reopen_attempts;
        pEvdev->reopen_timer = TimerSet(NULL, 0, 100, EvdevReopenTimer, pInfo);
    } else
    {
        xf86FlushInput(pInfo->fd);
        xf86AddEnabledDevice(pInfo);
        EvdevMBEmuOn(pInfo);
        pEvdev->flags |= EVDEV_INITIALIZED;
        device->public.on = TRUE;
    }

    return Success;
}


static int
EvdevProc(DeviceIntPtr device, int what)
{
    InputInfoPtr pInfo;
    EvdevPtr pEvdev;

    pInfo = device->public.devicePrivate;
    pEvdev = pInfo->private;

    switch (what)
    {
    case DEVICE_INIT:
	return EvdevInit(device);

    case DEVICE_ON:
        return EvdevOn(device);

    case DEVICE_OFF:
        if (pInfo->fd != -1)
        {
            if (pEvdev->grabDevice && ioctl(pInfo->fd, EVIOCGRAB, (void *)0))
                xf86Msg(X_WARNING, "%s: Release failed (%s)\n", pInfo->name,
                        strerror(errno));
            xf86RemoveEnabledDevice(pInfo);
            close(pInfo->fd);
            pInfo->fd = -1;
        }
        if (pEvdev->flags & EVDEV_INITIALIZED)
            EvdevMBEmuFinalize(pInfo);
        pEvdev->flags &= ~EVDEV_INITIALIZED;
	device->public.on = FALSE;
        if (pEvdev->reopen_timer)
        {
            TimerFree(pEvdev->reopen_timer);
            pEvdev->reopen_timer = NULL;
        }
	break;

    case DEVICE_CLOSE:
	xf86Msg(X_INFO, "%s: Close\n", pInfo->name);
        if (pInfo->fd != -1) {
            close(pInfo->fd);
            pInfo->fd = -1;
        }
	break;
    }

    return Success;
}

/**
 * Get as much information as we can from the fd and cache it.
 * If compare is True, then the information retrieved will be compared to the
 * one already cached. If the information does not match, then this function
 * returns an error.
 *
 * @return Success if the information was cached, or !Success otherwise.
 */
static int
EvdevCacheCompare(InputInfoPtr pInfo, BOOL compare)
{
    EvdevPtr pEvdev = pInfo->private;
    int i;

    char name[1024]                  = {0};
    long bitmask[NBITS(EV_MAX)]      = {0};
    long key_bitmask[NBITS(KEY_MAX)] = {0};
    long rel_bitmask[NBITS(REL_MAX)] = {0};
    long abs_bitmask[NBITS(ABS_MAX)] = {0};
    long led_bitmask[NBITS(LED_MAX)] = {0};
    struct input_absinfo absinfo[ABS_MAX];

    if (ioctl(pInfo->fd,
              EVIOCGNAME(sizeof(name) - 1), name) < 0) {
        xf86Msg(X_ERROR, "ioctl EVIOCGNAME failed: %s\n", strerror(errno));
        goto error;
    }

    if (compare && strcmp(pEvdev->name, name))
        goto error;

    if (ioctl(pInfo->fd,
              EVIOCGBIT(0, sizeof(bitmask)), bitmask) < 0) {
        xf86Msg(X_ERROR, "ioctl EVIOCGNAME failed: %s\n", strerror(errno));
        goto error;
    }

    if (compare && memcmp(pEvdev->bitmask, bitmask, sizeof(bitmask)))
        goto error;


    if (ioctl(pInfo->fd,
              EVIOCGBIT(EV_REL, sizeof(rel_bitmask)), rel_bitmask) < 0) {
        xf86Msg(X_ERROR, "ioctl EVIOCGBIT failed: %s\n", strerror(errno));
        goto error;
    }

    if (compare && memcmp(pEvdev->rel_bitmask, rel_bitmask, sizeof(rel_bitmask)))
        goto error;

    if (ioctl(pInfo->fd,
              EVIOCGBIT(EV_ABS, sizeof(abs_bitmask)), abs_bitmask) < 0) {
        xf86Msg(X_ERROR, "ioctl EVIOCGBIT failed: %s\n", strerror(errno));
        goto error;
    }

    if (compare && memcmp(pEvdev->abs_bitmask, abs_bitmask, sizeof(abs_bitmask)))
        goto error;

    if (ioctl(pInfo->fd,
              EVIOCGBIT(EV_KEY, sizeof(key_bitmask)), key_bitmask) < 0) {
        xf86Msg(X_ERROR, "ioctl EVIOCGBIT failed: %s\n", strerror(errno));
        goto error;
    }

    if (compare && memcmp(pEvdev->key_bitmask, key_bitmask, sizeof(key_bitmask)))
        goto error;

    if (ioctl(pInfo->fd,
              EVIOCGBIT(EV_LED, sizeof(led_bitmask)), led_bitmask) < 0) {
        xf86Msg(X_ERROR, "ioctl EVIOCGBIT failed: %s\n", strerror(errno));
        goto error;
    }

    if (compare && memcmp(pEvdev->led_bitmask, led_bitmask, sizeof(led_bitmask)))
        goto error;

    memset(absinfo, 0, sizeof(absinfo));

    for (i = 0; i < ABS_MAX; i++)
    {
        if (TestBit(i, abs_bitmask))
        {
            if (ioctl(pInfo->fd, EVIOCGABS(i), &absinfo[i]) < 0) {
                xf86Msg(X_ERROR, "ioctl EVIOCGABS failed: %s\n", strerror(errno));
                goto error;
            }
        }
    }

    if (compare && memcmp(pEvdev->absinfo, absinfo, sizeof(absinfo)))
            goto error;

    /* cache info */
    if (!compare)
    {
        strcpy(pEvdev->name, name);
        memcpy(pEvdev->bitmask, bitmask, sizeof(bitmask));
        memcpy(pEvdev->key_bitmask, key_bitmask, sizeof(key_bitmask));
        memcpy(pEvdev->rel_bitmask, rel_bitmask, sizeof(rel_bitmask));
        memcpy(pEvdev->abs_bitmask, abs_bitmask, sizeof(abs_bitmask));
        memcpy(pEvdev->led_bitmask, led_bitmask, sizeof(led_bitmask));
        memcpy(pEvdev->absinfo, absinfo, sizeof(absinfo));
    }

    return Success;

error:
    return !Success;

}

static int
EvdevProbe(InputInfoPtr pInfo)
{
    long key_bitmask[NBITS(KEY_MAX)] = {0};
    long rel_bitmask[NBITS(REL_MAX)] = {0};
    long abs_bitmask[NBITS(ABS_MAX)] = {0};
    int i, has_axes, has_keys, num_buttons;
    int kernel24 = 0;
    EvdevPtr pEvdev = pInfo->private;

    if (pEvdev->grabDevice && ioctl(pInfo->fd, EVIOCGRAB, (void *)1)) {
        if (errno == EINVAL) {
            /* keyboards are unsafe in 2.4 */
            kernel24 = 1;
            pEvdev->grabDevice = 0;
        } else {
            xf86Msg(X_ERROR, "Grab failed. Device already configured?\n");
            return 1;
        }
    } else if (pEvdev->grabDevice) {
        ioctl(pInfo->fd, EVIOCGRAB, (void *)0);
    }

    if (ioctl(pInfo->fd,
              EVIOCGBIT(EV_REL, sizeof(rel_bitmask)), rel_bitmask) < 0) {
        xf86Msg(X_ERROR, "ioctl EVIOCGBIT failed: %s\n", strerror(errno));
        return 1;
    }

    if (ioctl(pInfo->fd,
              EVIOCGBIT(EV_ABS, sizeof(abs_bitmask)), abs_bitmask) < 0) {
        xf86Msg(X_ERROR, "ioctl EVIOCGBIT failed: %s\n", strerror(errno));
        return 1;
    }

    if (ioctl(pInfo->fd,
              EVIOCGBIT(EV_KEY, sizeof(key_bitmask)), key_bitmask) < 0) {
        xf86Msg(X_ERROR, "ioctl EVIOCGBIT failed: %s\n", strerror(errno));
        return 1;
    }

    has_axes = FALSE;
    has_keys = FALSE;
    num_buttons = 0;

    /* count all buttons */
    for (i = BTN_MISC; i < BTN_JOYSTICK; i++)
    {
        if (TestBit(i, key_bitmask))
            num_buttons++;
    }

    if (num_buttons)
    {
        pEvdev->flags |= EVDEV_BUTTON_EVENTS;
        pEvdev->buttons = num_buttons;
        xf86Msg(X_INFO, "%s: Found %d mouse buttons\n", pInfo->name,
                num_buttons);
    }

    if (TestBit(REL_X, rel_bitmask) && TestBit(REL_Y, rel_bitmask)) {
        xf86Msg(X_INFO, "%s: Found x and y relative axes\n", pInfo->name);
	pEvdev->flags |= EVDEV_RELATIVE_EVENTS;
	has_axes = TRUE;
    }

    if (TestBit(ABS_X, abs_bitmask) && TestBit(ABS_Y, abs_bitmask)) {
        xf86Msg(X_INFO, "%s: Found x and y absolute axes\n", pInfo->name);
	pEvdev->flags |= EVDEV_ABSOLUTE_EVENTS;
	if (TestBit(BTN_TOUCH, key_bitmask)) {
            if (num_buttons) {
                xf86Msg(X_INFO, "%s: Found absolute touchpad\n", pInfo->name);
                pEvdev->flags |= EVDEV_TOUCHPAD;
                pEvdev->old_x = pEvdev->old_y = -1;
            } else {
                xf86Msg(X_INFO, "%s: Found absolute touchscreen\n", pInfo->name);
                pEvdev->flags |= EVDEV_TOUCHSCREEN;
                pEvdev->flags |= EVDEV_BUTTON_EVENTS;
            }
	}
	has_axes = TRUE;
    }

    for (i = 0; i < BTN_MISC; i++)
        if (TestBit(i, key_bitmask))
            break;

    if (i < BTN_MISC) {
        xf86Msg(X_INFO, "%s: Found keys\n", pInfo->name);
	pEvdev->flags |= EVDEV_KEYBOARD_EVENTS;
	has_keys = TRUE;
    }

    if (has_axes && num_buttons) {
        xf86Msg(X_INFO, "%s: Configuring as mouse\n", pInfo->name);
	pInfo->flags |= XI86_POINTER_CAPABLE | XI86_SEND_DRAG_EVENTS |
	    XI86_CONFIGURED;
	pInfo->type_name = XI_MOUSE;
    }

    if (pEvdev->flags & EVDEV_TOUCHSCREEN) {
        xf86Msg(X_INFO, "%s: Configuring as touchscreen\n", pInfo->name);
        pInfo->type_name = XI_TOUCHSCREEN;
        pInfo->flags |= XI86_POINTER_CAPABLE | XI86_SEND_DRAG_EVENTS |
                        XI86_CONFIGURED;
    }

    if (has_keys) {
        if (kernel24) {
            xf86Msg(X_INFO, "%s: Kernel < 2.6 is too old, ignoring keyboard\n",
                    pInfo->name);
        } else {
            xf86Msg(X_INFO, "%s: Configuring as keyboard\n", pInfo->name);
            pInfo->flags |= XI86_KEYBOARD_CAPABLE | XI86_CONFIGURED;
	    pInfo->type_name = XI_KEYBOARD;
        }
    }

    if ((pInfo->flags & XI86_CONFIGURED) == 0) {
        xf86Msg(X_WARNING, "%s: Don't know how to use device\n",
		pInfo->name);
        return 1;
    }

    return 0;
}


static InputInfoPtr
EvdevPreInit(InputDriverPtr drv, IDevPtr dev, int flags)
{
    InputInfoPtr pInfo;
    const char *device;
    EvdevPtr pEvdev;

    if (!(pInfo = xf86AllocateInput(drv, 0)))
	return NULL;

    /* Initialise the InputInfoRec. */
    pInfo->name = dev->identifier;
    pInfo->flags = 0;
    pInfo->type_name = "UNKNOWN";
    pInfo->device_control = EvdevProc;
    pInfo->read_input = EvdevReadInput;
    pInfo->history_size = 0;
    pInfo->control_proc = NULL;
    pInfo->close_proc = NULL;
    pInfo->switch_mode = NULL;
    pInfo->conversion_proc = NULL;
    pInfo->reverse_conversion_proc = NULL;
    pInfo->dev = NULL;
    pInfo->private_flags = 0;
    pInfo->always_core_feedback = 0;
    pInfo->conf_idev = dev;

    if (!(pEvdev = xcalloc(sizeof(EvdevRec), 1)))
        return pInfo;

    pInfo->private = pEvdev;

    xf86CollectInputOptions(pInfo, evdevDefaults, NULL);
    xf86ProcessCommonOptions(pInfo, pInfo->options);

    /*
     * We initialize pEvdev->tool to 1 so that device that doesn't use
     * proximity will still report events.
     */
    pEvdev->tool = 1;

    device = xf86CheckStrOption(dev->commonOptions, "Device", NULL);
    if (!device) {
        xf86Msg(X_ERROR, "%s: No device specified.\n", pInfo->name);
	xf86DeleteInput(pInfo, 0);
        return NULL;
    }

    pEvdev->device = device;

    xf86Msg(X_CONFIG, "%s: Device: \"%s\"\n", pInfo->name, device);
    do {
        pInfo->fd = open(device, O_RDWR, 0);
    } while (pInfo->fd < 0 && errno == EINTR);

    if (pInfo->fd < 0) {
        xf86Msg(X_ERROR, "Unable to open evdev device \"%s\".\n", device);
	xf86DeleteInput(pInfo, 0);
        return NULL;
    }

    pEvdev->reopen_attempts = xf86SetIntOption(pInfo->options, "ReopenAttempts", 10);
    pEvdev->invert_x = xf86SetBoolOption(pInfo->options, "InvertX", FALSE);
    pEvdev->invert_y = xf86SetBoolOption(pInfo->options, "InvertY", FALSE);
    pEvdev->swap_axes = xf86SetBoolOption(pInfo->options, "SwapAxes", FALSE);

    /* Grabbing the event device stops in-kernel event forwarding. In other
       words, it disables rfkill and the "Macintosh mouse button emulation".
       Note that this needs a server that sets the console to RAW mode. */
    pEvdev->grabDevice = xf86CheckBoolOption(dev->commonOptions, "GrabDevice", 0);

    pEvdev->noXkb = noXkbExtension; /* parse the XKB options during kbd setup */

    EvdevInitButtonMapping(pInfo);

    if (EvdevProbe(pInfo)) {
	close(pInfo->fd);
	xf86DeleteInput(pInfo, 0);
        return NULL;
    }

    EvdevCacheCompare(pInfo, FALSE); /* cache device data */

    if (pEvdev->flags & EVDEV_BUTTON_EVENTS)
    {
        EvdevMBEmuPreInit(pInfo);
        EvdevWheelEmuPreInit(pInfo);
        EvdevDragLockPreInit(pInfo);
    }

    return pInfo;
}

_X_EXPORT InputDriverRec EVDEV = {
    1,
    "evdev",
    NULL,
    EvdevPreInit,
    NULL,
    NULL,
    0
};

static void
EvdevUnplug(pointer	p)
{
}

static pointer
EvdevPlug(pointer	module,
          pointer	options,
          int		*errmaj,
          int		*errmin)
{
    xf86AddInputDriver(&EVDEV, module, 0);
    return module;
}

static XF86ModuleVersionInfo EvdevVersionRec =
{
    "evdev",
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR, PACKAGE_VERSION_PATCHLEVEL,
    ABI_CLASS_XINPUT,
    ABI_XINPUT_VERSION,
    MOD_CLASS_XINPUT,
    {0, 0, 0, 0}
};

_X_EXPORT XF86ModuleData evdevModuleData =
{
    &EvdevVersionRec,
    EvdevPlug,
    EvdevUnplug
};


/* Return an index value for a given button event code
 * returns 0 on non-button event.
 */
unsigned int
EvdevUtilButtonEventToButtonNumber(EvdevPtr pEvdev, int code)
{
    unsigned int button = 0;

    switch(code) {
    case BTN_LEFT:
	button = 1;
	break;

    case BTN_RIGHT:
	button = 3;
	break;

    case BTN_MIDDLE:
	button = 2;
	break;

        /* Treat BTN_[0-2] as LMR buttons on devices that do not advertise
           BTN_LEFT, BTN_MIDDLE, BTN_RIGHT.
           Otherwise, treat BTN_[0+n] as button 5+n.
           XXX: This causes duplicate mappings for BTN_0 + n and BTN_SIDE + n
         */
    case BTN_0:
        button = (TestBit(BTN_LEFT, pEvdev->key_bitmask)) ?  8 : 1;
        break;
    case BTN_1:
        button = (TestBit(BTN_MIDDLE, pEvdev->key_bitmask)) ?  9 : 2;
        break;
    case BTN_2:
        button = (TestBit(BTN_RIGHT, pEvdev->key_bitmask)) ?  10 : 3;
        break;

    case BTN_SIDE:
    case BTN_EXTRA:
    case BTN_FORWARD:
    case BTN_BACK:
    case BTN_TASK:
	button = (code - BTN_LEFT + 5);
	break;

    default:
	if ((code > BTN_TASK) && (code < KEY_OK)) {
	    if (code < BTN_JOYSTICK) {
                if (code < BTN_MOUSE)
                    button = (code - BTN_0 + 5);
                else
                    button = (code - BTN_LEFT + 5);
            }
	}
    }

    if (button > EVDEV_MAXBUTTONS)
	return 0;

    return button;
}

#ifdef HAVE_PROPERTIES
static void
EvdevInitProperty(DeviceIntPtr dev)
{
    InputInfoPtr pInfo  = dev->public.devicePrivate;
    EvdevPtr     pEvdev = pInfo->private;
    int          rc;
    BOOL         invert[2];
    char         reopen;


    if (pEvdev->flags & (EVDEV_RELATIVE_EVENTS | EVDEV_ABSOLUTE_EVENTS))
    {
        invert[0] = pEvdev->invert_x;
        invert[1] = pEvdev->invert_y;

        prop_invert = MakeAtom(EVDEV_PROP_INVERT_AXES, strlen(EVDEV_PROP_INVERT_AXES), TRUE);

        rc = XIChangeDeviceProperty(dev, prop_invert, XA_INTEGER, 8,
                PropModeReplace, 2,
                invert, FALSE);
        if (rc != Success)
            return;

        XISetDevicePropertyDeletable(dev, prop_invert, FALSE);
    }

    prop_reopen = MakeAtom(EVDEV_PROP_REOPEN, strlen(EVDEV_PROP_REOPEN),
            TRUE);

    reopen = pEvdev->reopen_attempts;
    rc = XIChangeDeviceProperty(dev, prop_reopen, XA_INTEGER, 8,
                                PropModeReplace, 1, &reopen, FALSE);
    if (rc != Success)
        return;

    XISetDevicePropertyDeletable(dev, prop_reopen, FALSE);


    prop_calibration = MakeAtom(EVDEV_PROP_CALIBRATION,
                                strlen(EVDEV_PROP_CALIBRATION), TRUE);
    rc = XIChangeDeviceProperty(dev, prop_calibration, XA_INTEGER, 32,
                                PropModeReplace, 0, NULL, FALSE);
    if (rc != Success)
        return;

    XISetDevicePropertyDeletable(dev, prop_calibration, FALSE);

    prop_swap = MakeAtom(EVDEV_PROP_SWAP_AXES,
                         strlen(EVDEV_PROP_SWAP_AXES), TRUE);

    rc = XIChangeDeviceProperty(dev, prop_swap, XA_INTEGER, 8,
                                PropModeReplace, 1, &pEvdev->swap_axes, FALSE);
    if (rc != Success)
        return;

    XISetDevicePropertyDeletable(dev, prop_swap, FALSE);
}

static int
EvdevSetProperty(DeviceIntPtr dev, Atom atom, XIPropertyValuePtr val,
                 BOOL checkonly)
{
    InputInfoPtr pInfo  = dev->public.devicePrivate;
    EvdevPtr     pEvdev = pInfo->private;

    if (atom == prop_invert)
    {
        BOOL* data;
        if (val->format != 8 || val->size != 2 || val->type != XA_INTEGER)
            return BadMatch;

        if (!checkonly)
        {
            data = (BOOL*)val->data;
            pEvdev->invert_x = data[0];
            pEvdev->invert_y = data[1];
        }
    } else if (atom == prop_reopen)
    {
        if (val->format != 8 || val->size != 1 || val->type != XA_INTEGER)
            return BadMatch;

        if (!checkonly)
            pEvdev->reopen_attempts = *((CARD8*)val->data);
    } else if (atom == prop_calibration)
    {
        if (val->format != 32 || val->type != XA_INTEGER)
            return BadMatch;
        if (val->size != 4 && val->size != 0)
            return BadMatch;

        if (!checkonly)
        {
            if (val->size == 0)
            {
                pEvdev->flags &= ~EVDEV_CALIBRATED;
                pEvdev->calibration.min_x = 0;
                pEvdev->calibration.max_x = 0;
                pEvdev->calibration.min_y = 0;
                pEvdev->calibration.max_y = 0;
            } else if (val->size == 4)
            {
                CARD32 *vals = (CARD32*)val->data;

                pEvdev->flags |= EVDEV_CALIBRATED;
                pEvdev->calibration.min_x = vals[0];
                pEvdev->calibration.max_x = vals[1];
                pEvdev->calibration.min_y = vals[2];
                pEvdev->calibration.max_y = vals[3];
            }
        }
    } else if (atom == prop_swap)
    {
        if (val->format != 8 || val->type != XA_INTEGER || val->size != 1)
            return BadMatch;

        if (!checkonly)
            pEvdev->swap_axes = *((BOOL*)val->data);
    }

    return Success;
}
#endif
