/*
 * Copyright © 2006 Zephaniah E. Hull
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Soft-
 * ware"), to deal in the Software without restriction, including without
 * limitation the rights to use, copy, modify, merge, publish, distribute,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, provided that the above copyright
 * notice(s) and this permission notice appear in all copies of the Soft-
 * ware and that both the above copyright notice(s) and this permission
 * notice appear in supporting documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABIL-
 * ITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY
 * RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN
 * THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSE-
 * QUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFOR-
 * MANCE OF THIS SOFTWARE.
 *
 * Except as contained in this notice, the name of a copyright holder shall
 * not be used in advertising or otherwise to promote the sale, use or
 * other dealings in this Software without prior written authorization of
 * the copyright holder.
 *
 * Author:  Zephaniah E. Hull (warp@aehallh.com)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <X11/extensions/XIproto.h>

/* The libc wrapper just blows... linux/input.h must be included
 * before xf86_ansic.h and xf86_libc.h so we avoid defining ioctl
 * twice. */

#include <linux/input.h>

#include <misc.h>
#include <xf86.h>
#include <xf86str.h>
#include <xf86_OSproc.h>
#include <xf86_ansic.h>
#include <xf86_libc.h>
#include <xf86Xinput.h>
#include <exevents.h>
#include <mipointer.h>

#include <xf86Module.h>

#include "evdev.h"

#define ArrayLength(a) (sizeof(a) / (sizeof((a)[0])))

#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x)  ((x)%BITS_PER_LONG)
#define LONG(x) ((x)/BITS_PER_LONG)
#define TestBit(bit, array)    ((array[LONG(bit)] >> OFF(bit)) & 1)

void
EvdevBtnPostFakeClicks(InputInfoPtr pInfo, int button, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        xf86PostButtonEvent(pInfo->dev, 0, button, 1, 0, 0);
        xf86PostButtonEvent(pInfo->dev, 0, button, 0, 0, 0);
    }
}

int
EvdevBtnInit (DeviceIntPtr device)
{
    InputInfoPtr pInfo = device->public.devicePrivate;
    evdevDevicePtr pEvdev = pInfo->private;
    CARD8 *map;
    int i;

    if (!pEvdev->state.buttons)
	return Success;

    map = Xcalloc (sizeof (CARD8) * pEvdev->state.buttons);

    for (i = 0; i < pEvdev->state.buttons; i++)
        map[i] = i;

    xf86Msg(X_ERROR, "%s (%d): Registering %d buttons.\n", __FILE__, __LINE__,
	    pEvdev->state.buttons);
    if (!InitButtonClassDeviceStruct (device, pEvdev->state.buttons, map)) {
	pEvdev->state.buttons = 0;

        return !Success;
    }

    Xfree (map);

    return Success;
}

int
EvdevBtnOn (DeviceIntPtr device)
{
    InputInfoPtr pInfo = device->public.devicePrivate;
    evdevDevicePtr pEvdev = pInfo->private;
    int i, blocked;

    if (!pEvdev->state.buttons)
	return Success;

    blocked = xf86BlockSIGIO ();
    for (i = 1; i <= pEvdev->state.buttons; i++)
	xf86PostButtonEvent (device, 0, i, 0, 0, 0);
    xf86UnblockSIGIO (blocked);

    return Success;
}

int
EvdevBtnOff (DeviceIntPtr device)
{
    return Success;
}

/*
 * Warning, evil lives here.
 */
static void
EvdevBtnCalcRemap (InputInfoPtr pInfo)
{
    evdevDevicePtr pEvdev = pInfo->private;
    int i, j, base, clear, fake;

    for (i = 0, base = 1, fake = 0; i < pEvdev->state.real_buttons; i++) {
	do {
	    clear = 1;
	    for (j = 0; j < REL_MAX; j++) {
		if (pEvdev->state.relToBtnMap[j][0] == (i + base)) {
		    base++;
		    clear = 0;
		    break;
		}
		if (pEvdev->state.relToBtnMap[j][1] == (i + base)) {
		    base++;
		    clear = 0;
		    break;
		}
	    }
	} while (!clear);

	if (!fake && base != 1)
	    fake = i;

	pEvdev->state.buttons = pEvdev->state.buttonMap[i] = i + base;
    }

    if (pEvdev->state.real_buttons >= 3 && (!fake || fake >= 3)) {
	base = pEvdev->state.buttonMap[1];
	pEvdev->state.buttonMap[1] = pEvdev->state.buttonMap[2];
	pEvdev->state.buttonMap[2] = base;
    }

    for (i = 0; i < REL_MAX; i++) {
	if (pEvdev->state.relToBtnMap[i][0] > pEvdev->state.buttons)
	    pEvdev->state.buttons = pEvdev->state.relToBtnMap[i][0];
	if (pEvdev->state.relToBtnMap[i][1] > pEvdev->state.buttons)
	    pEvdev->state.buttons = pEvdev->state.relToBtnMap[i][1];
    }
}


int
EvdevBtnNew(InputInfoPtr pInfo)
{
    evdevDevicePtr pEvdev = pInfo->private;
    long key_bitmask[NBITS(KEY_MAX)];
    int i;

    if (ioctl(pInfo->fd,
              EVIOCGBIT(EV_KEY, KEY_MAX), key_bitmask) < 0) {
        xf86Msg(X_ERROR, "ioctl EVIOCGBIT failed: %s\n", strerror(errno));
        return !Success;
    }

    for (i = 0; i < (BTN_JOYSTICK - BTN_MOUSE); i++)
	if (TestBit (BTN_MOUSE + i, key_bitmask))
	    pEvdev->state.real_buttons = i + 1;

    if (pEvdev->state.real_buttons)
        xf86Msg(X_INFO, "%s: Found %d mouse buttons\n", pInfo->name, pEvdev->state.real_buttons);

    EvdevBtnCalcRemap (pInfo);

    if (pEvdev->state.buttons)
	xf86Msg(X_INFO, "%s: Configured %d mouse buttons\n", pInfo->name, pEvdev->state.buttons);
    else
	return !Success;

    pInfo->flags |= XI86_SEND_DRAG_EVENTS | XI86_CONFIGURED;
    pInfo->type_name = XI_MOUSE;

    return Success;
}

void
EvdevBtnProcess (InputInfoPtr pInfo, struct input_event *ev)
{
    evdevDevicePtr pEvdev = pInfo->private;
    int button;

    if (!pEvdev->state.buttons)
	return;

    if ((ev->code >= BTN_MOUSE) && (ev->code < BTN_JOYSTICK)) {
	button = ev->code - BTN_MOUSE;
	button = pEvdev->state.buttonMap[button];

	xf86PostButtonEvent (pInfo->dev, 0, button, ev->value, 0, 0);
    } else {
	/* FIXME: Handle the non-mouse case. */
    }
}
