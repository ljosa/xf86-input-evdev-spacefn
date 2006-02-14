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

static char *axis_names[] = {
    "X",
    "Y",
    "Z",
    "RX",
    "RY",
    "RZ",
    "HWHEEL",
    "DIAL",
    "WHEEL",
    "MISC",
    "10",
    "11",
    "12",
    "13",
    "14",
    "15",
    NULL
};

static void
EvdevPtrCtrlProc(DeviceIntPtr device, PtrCtrl *ctrl)
{
    /* Nothing to do, dix handles all settings */
}

static Bool
EvdevConvert(InputInfoPtr pInfo, int first, int num, int v0, int v1, int v2,
	     int v3, int v4, int v5, int *x, int *y)
{
    if (first == 0) {
        *x = v0;
        *y = v1;
        return TRUE;
    } else
        return FALSE;
}

/*
 * FIXME: Verify that the C standard lets us pass more variable arguments
 * then we specify.
 */
void
EvdevRelSyn (InputInfoPtr pInfo)
{
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;
    int i, btn;

    if (!state->rel_axes || state->mode != Relative)
	return;

    for (i = 0; i < state->rel_axes; i++) {
	if ((state->rel_v[i] > 0) && (btn = state->relToBtnMap[i][0]))
	    EvdevBtnPostFakeClicks (pInfo, btn, state->rel_v[i]);
	else if ((state->rel_v[i] < 0) && (btn = state->relToBtnMap[i][1]))
	    EvdevBtnPostFakeClicks (pInfo, btn, -state->rel_v[i]);
    }

    xf86PostMotionEvent(pInfo->dev, 0, 0, state->rel_axes,
	state->rel_v[0],  state->rel_v[1],  state->rel_v[2],  state->rel_v[3],
	state->rel_v[4],  state->rel_v[5],  state->rel_v[6],  state->rel_v[7],
	state->rel_v[8],  state->rel_v[9],  state->rel_v[10], state->rel_v[11],
	state->rel_v[12], state->rel_v[13], state->rel_v[14], state->rel_v[15]);

    for (i = 0; i < REL_MAX; i++)
	state->rel_v[i] = 0;
}

void
EvdevRelProcess (InputInfoPtr pInfo, struct input_event *ev)
{
    evdevDevicePtr pEvdev = pInfo->private;
    int map;

    if (ev->code >= REL_MAX)
	return;

    map = pEvdev->state.relMap[ev->code];
    if (map >= 0)
	pEvdev->state.rel_v[map] += ev->value;
    else
	pEvdev->state.rel_v[-map] -= ev->value;

    if (!pEvdev->state.sync)
	EvdevRelSyn (pInfo);
}

int
EvdevRelInit (DeviceIntPtr device)
{
    InputInfoPtr pInfo = device->public.devicePrivate;
    evdevDevicePtr pEvdev = pInfo->private;
    int i;

    if (!InitValuatorClassDeviceStruct(device, pEvdev->state.rel_axes,
                                       miPointerGetMotionEvents,
                                       miPointerGetMotionBufferSize(), 0))
        return !Success;

    for (i = 0; i < pEvdev->state.rel_axes; i++) {
	xf86InitValuatorAxisStruct(device, i, 0, 0, 0, 0, 1);
	xf86InitValuatorDefaults(device, i);
    }

    if (!InitPtrFeedbackClassDeviceStruct(device, EvdevPtrCtrlProc))
        return !Success;

    return Success;
}

int
EvdevRelOn (DeviceIntPtr device)
{
    return Success;
}

int
EvdevRelOff (DeviceIntPtr device)
{
    return Success;
}

int
EvdevRelNew(InputInfoPtr pInfo)
{
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;
    long rel_bitmask[NBITS(KEY_MAX)];
    char *s, option[64];
    int i, j, k = 0, real_axes;

    if (ioctl(pInfo->fd,
              EVIOCGBIT(EV_REL, REL_MAX), rel_bitmask) < 0) {
        xf86Msg(X_ERROR, "ioctl EVIOCGBIT failed: %s\n", strerror(errno));
        return !Success;
    }

    real_axes = 0;
    for (i = 0; i < REL_MAX; i++)
	if (TestBit (i, rel_bitmask))
	    real_axes++;

    if (!real_axes && (state->abs_axes < 2))
	return !Success;

    xf86Msg(X_INFO, "%s: Found %d relative axes.\n", pInfo->name,
	    real_axes);
    xf86Msg(X_INFO, "%s: Configuring as pointer.\n", pInfo->name);
    pInfo->flags |= XI86_POINTER_CAPABLE | XI86_SEND_DRAG_EVENTS |
	XI86_CONFIGURED;
    pInfo->type_name = XI_MOUSE;
    pInfo->conversion_proc = EvdevConvert;

    for (i = 0, j = 0; i < REL_MAX; i++) {
	if (!TestBit (i, rel_bitmask))
	    continue;

	snprintf(option, sizeof(option), "%sRelativeAxisMap", axis_names[i]);
	s = xf86SetStrOption(pInfo->options, option, "0");
	if (s && (k = strtol(s, NULL, 0)))
	    state->relMap[i] = k;
	else
	    state->relMap[i] = j;

	if (s && k)
	    xf86Msg(X_CONFIG, "%s: %s: %d.\n", pInfo->name, option, k);


	snprintf(option, sizeof(option), "%sRelativeAxisButtons", axis_names[i]);
	if (i == REL_WHEEL || i == REL_Z)
	    s = xf86SetStrOption(pInfo->options, option, "4 5");
	else if (i == REL_HWHEEL)
	    s = xf86SetStrOption(pInfo->options, option, "6 7");
	else
	    s = xf86SetStrOption(pInfo->options, option, "0 0");

	k = state->relMap[i];

	if (!s || (sscanf(s, "%d %d", &state->relToBtnMap[k][0],
			&state->relToBtnMap[k][1]) != 2))
	    state->relToBtnMap[k][0] = state->relToBtnMap[k][1] = 0;

	if (state->relToBtnMap[k][0] || state->relToBtnMap[k][1])
	    xf86Msg(X_CONFIG, "%s: %s: %d %d.\n", pInfo->name, option,
		    state->relToBtnMap[k][0], state->relToBtnMap[k][1]);

	j++;
    }

    state->rel_axes = real_axes;
    for (i = 0; i < REL_MAX; i++)
	if (state->relMap[i] > state->rel_axes)
	    state->rel_axes = state->relMap[i];

    if ((state->abs_axes >= 2) && (state->rel_axes < 2))
	state->rel_axes = 2;

    if (state->rel_axes != real_axes)
	xf86Msg(X_CONFIG, "%s: Configuring %d relative axes.\n", pInfo->name,
		state->rel_axes);

    return Success;
}
