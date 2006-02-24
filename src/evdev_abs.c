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
    "THROTTLE",
    "RUDDER",
    "WHEEL",
    "GAS",
    "BRAKE",
    "11",
    "12",
    "13",
    "14",
    "15",
    "HAT0X",
    "HAT0Y",
    "HAT1X",
    "HAT1Y",
    "HAT2X",
    "HAT2Y",
    "HAT3X",
    "HAT3Y",
    "PRESSURE",
    "TILT_X",
    "TILT_Y",
    "TOOL_WIDTH",
    "VOLUME",
    "29",
    "30",
    "31",
    "32",
    "33",
    "34",
    "35",
    "36",
    "37",
    "38",
    "39",
    "MISC",
    "41",
    "42",
    "43",
    "44",
    "45",
    "46",
    "47",
    "48",
    "49",
    "50",
    "51",
    "52",
    "53",
    "54",
    "55",
    "56",
    "57",
    "58",
    "59",
    "60",
    "61",
    "62",
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
EvdevAbsSyn (InputInfoPtr pInfo)
{
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;
    int i;
    int n;

    if (!state->abs)
	return;

    n = state->abs->n & 1;

    if (state->mode == Absolute) {
	if ((state->abs->screen >= 0) && state->abs->axes >= 2) {
	    int conv_x, conv_y;

	    for (i = 0; i < 2; i++)
		state->abs->v[n][i] = xf86ScaleAxis (state->abs->v[n][i], 0,
			state->abs->scale_x,
			state->abs->min[i], state->abs->max[i]);


	    EvdevConvert (pInfo, 0, 2, state->abs->v[n][0], state->abs->v[n][1],
		    0, 0, 0, 0, &conv_x, &conv_y);
	    xf86XInputSetScreen (pInfo, state->abs->screen, conv_x, conv_y);
	}


	xf86PostMotionEvent(pInfo->dev, 1, 0, state->abs->axes,
	    state->abs->v[n][0],
	    state->abs->v[n][1], state->abs->v[n][2], state->abs->v[n][3],
	    state->abs->v[n][4], state->abs->v[n][5], state->abs->v[n][6],
	    state->abs->v[n][7], state->abs->v[n][8], state->abs->v[n][9],
	    state->abs->v[n][10], state->abs->v[n][11], state->abs->v[n][12],
	    state->abs->v[n][13], state->abs->v[n][14], state->abs->v[n][15]);
    } else {
	for (i = 0; i < 2; i++)
	    state->rel->v[i] = state->abs->v[n][i] - state->abs->v[!n][i];
    }

    state->abs->n++;
}

void
EvdevAbsProcess (InputInfoPtr pInfo, struct input_event *ev)
{
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;
    int n = state->abs->n & 1;
    int map;

    if (ev->code >= ABS_MAX)
	return;

    map = pEvdev->state.abs->map[ev->code];
    if (map >= 0)
	pEvdev->state.abs->v[n][map] += ev->value;
    else
	pEvdev->state.abs->v[n][-map] -= ev->value;

    if (!pEvdev->state.sync)
	EvdevAbsSyn (pInfo);
}

int
EvdevAbsInit (DeviceIntPtr device)
{
    InputInfoPtr pInfo = device->public.devicePrivate;
    evdevDevicePtr pEvdev = pInfo->private;
    int i;

    if (!InitValuatorClassDeviceStruct(device, pEvdev->state.abs->axes,
                                       miPointerGetMotionEvents,
                                       miPointerGetMotionBufferSize(), 0))
        return !Success;

    for (i = 0; i < pEvdev->state.abs->axes; i++) {
	xf86InitValuatorAxisStruct(device, i, 0, 0, 0, 0, 1);
	xf86InitValuatorDefaults(device, i);
    }

    if (!InitPtrFeedbackClassDeviceStruct(device, EvdevPtrCtrlProc))
        return !Success;

    return Success;
}

int
EvdevAbsOn (DeviceIntPtr device)
{
    return Success;
}

int
EvdevAbsOff (DeviceIntPtr device)
{
    return Success;
}

int
EvdevAbsNew(InputInfoPtr pInfo)
{
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;
    struct input_absinfo absinfo;
    char *s, option[64];
    int i, j, k = 0, real_axes;

    real_axes = 0;
    for (i = 0; i < ABS_MAX; i++)
	if (TestBit (i, pEvdev->bits.abs))
	    real_axes++;

    if (!real_axes)
	return !Success;

    state->abs = Xcalloc (sizeof (evdevAbsRec));

    xf86Msg(X_INFO, "%s: Found %d absolute axes.\n", pInfo->name, real_axes);
    xf86Msg(X_INFO, "%s: Configuring as pointer.\n", pInfo->name);
    pInfo->flags |= XI86_POINTER_CAPABLE | XI86_SEND_DRAG_EVENTS |
	XI86_CONFIGURED;
    pInfo->type_name = XI_MOUSE;
    pInfo->conversion_proc = EvdevConvert;

    for (i = 0, j = 0; i < ABS_MAX; i++) {
	if (!TestBit (i, pEvdev->bits.abs))
	    continue;

	snprintf(option, sizeof(option), "%sAbsoluteAxisMap", axis_names[i]);
	k = xf86SetIntOption(pInfo->options, option, -1);
	if (k != -1)
	    state->abs->map[i] = k;
	else
	    state->abs->map[i] = j;

	if (k != -1)
	    xf86Msg(X_CONFIG, "%s: %s: %d.\n", pInfo->name, option, k);

	if (ioctl (pInfo->fd, EVIOCGABS(i), &absinfo) < 0) {
	    xf86Msg(X_ERROR, "ioctl EVIOCGABS failed: %s\n", strerror(errno));
	    return !Success;
	}
	state->abs->min[state->abs->map[i]] = absinfo.minimum;
	state->abs->max[state->abs->map[i]] = absinfo.maximum;

	j++;
    }

    state->abs->axes = real_axes;
    for (i = 0; i < ABS_MAX; i++) {
	if (state->abs->map[i] > state->abs->axes)
	    state->abs->axes = state->abs->map[i];
    }

    if (state->abs->axes != real_axes)
	xf86Msg(X_CONFIG, "%s: Configuring %d absolute axes.\n", pInfo->name,
		state->abs->axes);

    s = xf86SetStrOption(pInfo->options, "Mode", "Absolute");
    if (!strcasecmp(s, "Absolute")) {
	state->mode = Absolute;
	xf86Msg(X_CONFIG, "%s: Configuring in %s mode.\n", pInfo->name, s);
    } else if (!strcasecmp(s, "Relative")) {
	state->mode = Relative;
	xf86Msg(X_CONFIG, "%s: Configuring in %s mode.\n", pInfo->name, s);
    } else {
	state->mode = Absolute;
	xf86Msg(X_CONFIG, "%s: Unknown Mode: %s.\n", pInfo->name, s);
    }

    if (TestBit (ABS_X, pEvdev->bits.abs) && TestBit (ABS_Y, pEvdev->bits.abs))
	k = xf86SetIntOption(pInfo->options, "AbsoluteScreen", 0);
    else
	k = xf86SetIntOption(pInfo->options, "AbsoluteScreen", -1);
    if (k < screenInfo.numScreens) {
	state->abs->screen = k;
	xf86Msg(X_CONFIG, "%s: AbsoluteScreen: %d.\n", pInfo->name, k);
    } else {
	state->abs->screen = 0;
	xf86Msg(X_CONFIG, "%s: AbsoluteScreen: %d is not a valid screen.\n", pInfo->name, k);
    }

    state->abs->scale_x = screenInfo.screens[state->abs->screen]->width;
    state->abs->scale_y = screenInfo.screens[state->abs->screen]->height;

    return Success;
}
