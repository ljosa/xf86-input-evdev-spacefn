/*
 * Copyright © 2006-2007 Zephaniah E. Hull
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

#include <string.h>

#include "evdev.h"

#include <xf86.h>

#include <xf86Module.h>
#include <mipointer.h>


#include <xf86_OSproc.h>

#undef DEBUG

static char *rel_axis_names[] = {
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

static char *abs_axis_names[] = {
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

static void EvdevAxesTouchCallback (InputInfoPtr pInfo, int button, int value);

void
EvdevAxesMapAxis (InputInfoPtr pInfo, int value, int mode, void *map_data)
{
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;
    evdevAxesPtr axes = state->axes;
    long map = (long) map_data;

    if (map >= AXES_MAX || !axes || !(axes->v_flags[map] & (EV_AXES_V_M_ABS | EV_AXES_V_M_REL)))
	return;

    axes->v[map] = value;
    if (mode == 0) {
	axes->v_flags[map] &= ~EV_AXES_V_M_ABS;
	axes->v_flags[map] |= EV_AXES_V_M_REL;
    } else if (mode == 1) {
	axes->v_flags[map] &= ~EV_AXES_V_M_REL;
	axes->v_flags[map] |= EV_AXES_V_M_ABS;
    }
    axes->v_flags[map] |= EV_AXES_V_UPDATED;
    axes->flags |= EV_AXES_UPDATED;
}

static Bool
EvdevParseRelOptions (InputInfoPtr pInfo, const char *name, evdev_option_token_t *option, int *flags)
{
    if (!option)
	return 0;

    for (; option; option = option->next) {
	if (!strcasecmp (option->str, "invert"))
	    *flags |= EV_REL_V_INVERT;
	else
	    xf86Msg(X_ERROR, "%s: %s unknown relative option '%s'.\n", pInfo->name, name, option->str);

    }
    *flags |= EV_REL_V_PRESENT;

    return 1;
}

static Bool
EvdevParseAbsOptions (InputInfoPtr pInfo, const char *name, evdev_option_token_t *option, int *flags)
{
    if (!option)
	return 0;

    for (; option; option = option->next) {
	if (!strcasecmp (option->str, "invert"))
	    *flags |= EV_ABS_V_INVERT;
	else if (!strcasecmp (option->str, "use_touch"))
	    *flags |= EV_ABS_V_USE_TOUCH;
	else if (!strcasecmp (option->str, "mode_auto"))
	    *flags |= EV_ABS_V_M_AUTO;
	else if (!strcasecmp (option->str, "mode_rel"))
	    *flags |= EV_ABS_V_M_REL;
	else
	    xf86Msg(X_ERROR, "%s: %s unknown absolute option '%s'.\n", pInfo->name, name, option->str);

    }
    *flags |= EV_ABS_V_PRESENT;

    return 1;
}

Bool
EvdevParseMapToRelAxis (InputInfoPtr pInfo,
	const char *name,
	evdev_option_token_t *option,
	void **map_data, evdev_map_func_f *map_func)
{
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;
    evdevAxesPtr axes = state->axes;
    long i;

    errno = 0;
    i = strtol (option->str, NULL, 0);
    if (errno) {
	for (i = 0; rel_axis_names[i]; i++) {
	    if (!strcmp (option->str, rel_axis_names[i]))
		break;
	}
	if (!rel_axis_names[i])
	    return 0;
    }
    if ((i < 0) || (i > AXES_MAX))
	return 0;

    if (axes->v_flags[i] & EV_AXES_V_PRESENT)
	return 0;

    axes->v_flags[i] = EV_AXES_V_M_REL | EV_AXES_V_PRESENT;

    *map_data = (void *) i;
    *map_func = EvdevAxesMapAxis;

    return 1;
}

Bool
EvdevParseMapToAbsAxis (InputInfoPtr pInfo,
	const char *name,
	evdev_option_token_t *option,
	void **map_data, evdev_map_func_f *map_func)
{
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;
    evdevAxesPtr axes = state->axes;
    long i;

    errno = 0;
    i = strtol (option->str, NULL, 0);
    if (errno) {
	for (i = 0; abs_axis_names[i]; i++) {
	    if (!strcmp (option->str, abs_axis_names[i]))
		break;
	}
	if (!abs_axis_names[i]) {
	    xf86Msg (X_ERROR, "%s: %s: No axis named '%s'.\n", pInfo->name, name, option->str);
	    return 0;
	}
    }
    if ((i < 0) || (i > AXES_MAX)) {
	xf86Msg (X_ERROR, "%s: %s: Axis %ld out of range.\n", pInfo->name, name, i);
	return 0;
    }

    if (axes->v_flags[i] & EV_AXES_V_PRESENT) {
	xf86Msg (X_ERROR, "%s: %s: Axis %ld already claimed.\n", pInfo->name, name, i);
	return 0;
    }

    option = option->next;
    if (!option) {
	xf86Msg (X_ERROR, "%s: %s: No min.\n", pInfo->name, name);
	return 0;
    }

    errno = 0;
    axes->v_min[i] = strtol (option->str, NULL, 0);
    if (errno) {
	xf86Msg (X_ERROR, "%s: %s: Unable to parse '%s' as min. (%s)\n", pInfo->name, name, option->str, strerror(errno));
	return 0;
    }

    option = option->next;
    if (!option) {
	xf86Msg (X_ERROR, "%s: %s: No max.\n", pInfo->name, name);
	return 0;
    }

    errno = 0;
    axes->v_max[i] = strtol (option->str, NULL, 0);
    if (errno) {
	xf86Msg (X_ERROR, "%s: %s: Unable to parse '%s' as max. (%s)\n", pInfo->name, name, option->str, strerror(errno));
	return 0;
    }

    axes->v_flags[i] = EV_AXES_V_M_ABS | EV_AXES_V_PRESENT;

    *map_data = (void *) i;
    *map_func = EvdevAxesMapAxis;

    return 1;
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
 * Rotation and rep code, this is a mess and much of it needs to live in mi/
 * after a cleanup.
 */
static void
EvdevAxesDoRotation (InputInfoPtr pInfo)
{
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;
    evdevAxesPtr axes = state->axes;
    DeviceIntPtr dev = pInfo->dev;
    AbsoluteClassRec *dabs = dev->absolute;

    /*
     * Rotation.
     * Cache the sine and cosine results so we're not doing it every time.
     */
    if (dabs->rotation != axes->rotation || (axes->rot_cos == axes->rot_sin)) {
	axes->rotation = dabs->rotation % 360;
	axes->rot_cos = cos ( ((float) axes->rotation) * (M_PI/180));
	axes->rot_sin = sin ( ((float) axes->rotation) * (M_PI/180));
    }

    if (axes->rotation) {
	float x = axes->v[0], y = axes->v[1];
	axes->v[0] = (x * axes->rot_cos) - (y * axes->rot_sin);
	axes->v[1] = (y * axes->rot_cos) + (x * axes->rot_sin);

	axes->v_flags[0] |= EV_AXES_V_UPDATED;
	axes->v_flags[1] |= EV_AXES_V_UPDATED;
#if DEBUG
	xf86Msg(X_ERROR, "%s %d (%s): rotation=%d, cos=%f, sin=%f, x=%f, y=%f, v[0]=%d, v[1]=%d\n", __FILE__, __LINE__, __FUNCTION__,
		axes->rotation, axes->rot_cos, axes->rot_sin, x, y, axes->v[0], axes->v[1]);
#endif
    }
}

/* 
 * Cx     - raw data from touch screen
 * Sxhigh - scaled highest dimension
 *          (remember, this is of rows - 1 because of 0 origin)
 * Sxlow  - scaled lowest dimension
 * Rxhigh - highest raw value from touch screen calibration
 * Rxlow  - lowest raw value from touch screen calibration
 *
 * This function is the same for X or Y coordinates.
 * You may have to reverse the high and low values to compensate for
 * different orgins on the touch screen vs X.
 */

_X_EXPORT int
EvdevScaleAxis(int	Cx,
              int	Sxlow,
              int	Sxhigh,
              int	Rxlow,
              int	Rxhigh)
{
    int X;
    int dSx = Sxhigh - Sxlow;
    int dRx = Rxhigh - Rxlow;

    /* This is +, because Cx is negitive, so we're really subtracting. */
    if (Cx < 0)
	Cx = Rxhigh + Cx;

    dSx = Sxhigh - Sxlow;
    if (dRx) {
	X = ((dSx * (Cx - Rxlow)) / dRx) + Sxlow;
    }
    else {
	X = 0;
	ErrorF ("Divide by Zero in evdevScaleAxis");
    }
    
    if (X < Sxlow)
	X = Sxlow;
    if (X > Sxhigh)
	X = Sxhigh;

    return (X);
}

void
EvdevAxesSynRep (InputInfoPtr pInfo)
{
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;
    evdevAxesPtr axes = state->axes;
    DeviceIntPtr dev = pInfo->dev;
    AbsoluteClassRec *dabs = dev->absolute;

    int i, start, run, mode;

    if (!axes || !(axes->flags & EV_AXES_UPDATED))
	return;

    start = 0;
    mode = 0;
    run = 0;

    /*
     * This handles most, but not all, of the ABS_CALIB and ABS_AREA
     * additions to XInput 1.0.
     *
     * Note, we do this if both X and Y are set to absolute, or a more
     * limited subset if both X and Y are relative, we don't do anything
     * if we lack X or Y, or if they are not both set to both be ABS or REL.
     */
    if (axes->axes >= 2 && dabs) {
	if ((axes->v_flags[0] & EV_AXES_V_M_ABS) &&
	    (axes->v_flags[1] & EV_AXES_V_M_ABS) &&
	    ((axes->v_flags[0] & EV_AXES_V_UPDATED) ||
	     (axes->v_flags[1] & EV_AXES_V_UPDATED))
	    ) {
	    int width, height, min_x, max_x, min_y, max_y;

	    if (axes->v_flags[0] & EV_AXES_V_UPDATED) axes->x = axes->v[0];
	    else axes->v[0] = axes->x;
	    if (axes->v_flags[1] & EV_AXES_V_UPDATED) axes->y = axes->v[1];
	    else axes->v[1] = axes->y;

	    if (dabs->width > 0)
		width = dabs->width;
	    else
		width = screenInfo.screens[dabs->screen]->width;

	    if (dabs->height > 0)
		height = dabs->height;
	    else
		height = screenInfo.screens[dabs->screen]->height;

	    if (dabs->flip_x)
		axes->v[0] = dabs->max_x - axes->v[0];
	    if (dabs->flip_y)
		axes->v[1] = dabs->max_y - axes->v[1];

	    /*
	     * In some cases we need to swap width and height.
	     * This depends on the rotation.
	     */
	    if ( (axes->rotation >= 45  && axes->rotation < 135) ||
		    (axes->rotation >= 225 && axes->rotation < 315)) {
		min_x = dabs->min_y;
		max_x = dabs->max_y;
		min_y = dabs->min_x;
		max_y = dabs->max_x;
	    } else {
		min_x = dabs->min_x;
		max_x = dabs->max_x;
		min_y = dabs->min_y;
		max_y = dabs->max_y;
	    }

	    EvdevAxesDoRotation (pInfo);

	    axes->v[0] = EvdevScaleAxis (axes->v[0], 0, width, min_x, max_x);
	    axes->v[1] = EvdevScaleAxis (axes->v[1], 0, height, min_y, max_y);

	    axes->v[0] += dabs->offset_x;
	    axes->v[1] += dabs->offset_y;

	    xf86XInputSetScreen (pInfo, dabs->screen, axes->v[0], axes->v[1]);
	} else if ((axes->v_flags[0] & EV_AXES_V_M_REL) &&
		(axes->v_flags[1] & EV_AXES_V_M_REL) &&
		((axes->v_flags[0] & EV_AXES_V_UPDATED) ||
		 (axes->v_flags[1] & EV_AXES_V_UPDATED))
		) {

	    if (axes->v_flags[0] & EV_AXES_V_UPDATED) axes->x = axes->v[0];
	    else axes->v[0] = axes->x;
	    if (axes->v_flags[1] & EV_AXES_V_UPDATED) axes->y = axes->v[1];
	    else axes->v[1] = axes->y;

	    if (dabs->flip_x)
		axes->v[0] = -axes->v[0];
	    if (dabs->flip_y)
		axes->v[1] = -axes->v[1];

	    EvdevAxesDoRotation (pInfo);
	}
    }

#if DEBUG
    xf86Msg(X_ERROR, "%s %d (%s): v[0]=%d%s%s, v[1]=%d%s%s, v[2]=%d%s%s\n", __FILE__, __LINE__, __FUNCTION__,
	    axes->v[0],
	    axes->v_flags[0] & EV_AXES_V_M_ABS ? "!" : "",
	    axes->v_flags[0] & EV_AXES_V_UPDATED ? "*" : "",
	    axes->v[1],
	    axes->v_flags[1] & EV_AXES_V_M_ABS ? "!" : "",
	    axes->v_flags[1] & EV_AXES_V_UPDATED ? "*" : "",
	    axes->v[2],
	    axes->v_flags[2] & EV_AXES_V_M_ABS ? "!" : "",
	    axes->v_flags[2] & EV_AXES_V_UPDATED ? "*" : "");
#endif
    for (i = 0; i < axes->axes; i++) {
	if (axes->v_flags[i] & EV_AXES_V_UPDATED) {
	    if (run) {
		if (mode != (axes->v_flags[i] & EV_AXES_V_M_MASK)) {
		    mode = (mode == EV_AXES_V_M_ABS);
#if DEBUG
    xf86Msg(X_ERROR, "%s %d (%s): mode=%d, start=%d, i - start=%d\n", __FILE__, __LINE__, __FUNCTION__,
	    mode, start, i - start);
#endif
		    xf86PostMotionEventP (pInfo->dev, mode, start, i - start, axes->v + start);
		    start = i;
		    mode = axes->v_flags[i] & EV_AXES_V_M_MASK;
		}
	    } else {
		start = i;
		mode = axes->v_flags[i] & EV_AXES_V_M_MASK;
	    }
	    run = 1;
	    axes->v_flags[i] &= ~EV_AXES_V_UPDATED;
	} else if (run) {
	    mode = (mode == EV_AXES_V_M_ABS);
	    xf86PostMotionEventP (pInfo->dev, mode, start, i - start, axes->v + start);
	    run = 0;
	}
    }
    if (run) {
	mode = (mode == EV_AXES_V_M_ABS);
	xf86PostMotionEventP (pInfo->dev, mode, start, i - start, axes->v + start);
    }
}
/*
 * End rotation and rep code, this is a mess and much of it needs to live in mi/
 * after a cleanup.
 */


void
EvdevAxesSynCfg (InputInfoPtr pInfo)
{
/*    EvdevAxesAbsSynCfg (pInfo);*/
/*    EvdevAxesRelSynCfg (pInfo);*/
}

void
EvdevAxesAbsProcess (InputInfoPtr pInfo, struct input_event *ev)
{
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;
    evdevAbsRec *abs = state->abs;
    int value, v_flags, is_rel;

    if (ev->code >= ABS_MAX || !abs->v_map[ev->code])
	return;

    value = ev->value;
    v_flags = abs->v_flags[ev->code];

    if ((v_flags & EV_ABS_V_USE_TOUCH) && !(state->abs->flags & EV_ABS_TOUCH))
	return;

#if 0
    if (v_flags & EV_ABS_V_INVERT)
	value = -value;
#endif

    if (v_flags & EV_ABS_V_M_REL)
	is_rel = 1;
    else if ((v_flags & EV_ABS_V_M_AUTO) && pInfo->dev->valuator->mode == Relative)
	is_rel = 1;
    else
	is_rel = 0;

    if (is_rel) {
	if ((v_flags & EV_ABS_V_RESET) && value != abs->v[ev->code]) {
	    abs->v_flags[ev->code] &= ~EV_ABS_V_RESET;
	} else
	    abs->v_map[ev->code](pInfo, value - abs->v[ev->code], 0, abs->v_map_data[ev->code]);

	abs->v[ev->code] = value;
    } else
	abs->v_map[ev->code](pInfo, value, 1, abs->v_map_data[ev->code]);

}

void
EvdevAxesRelProcess (InputInfoPtr pInfo, struct input_event *ev)
{
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;
    evdevRelRec *rel = state->rel;
    int value, v_flags;

    if (ev->code >= REL_MAX || !rel->v_map[ev->code])
	return;

    value = ev->value;
    v_flags = rel->v_flags[ev->code];

    if (v_flags & EV_REL_V_INVERT)
	value = -value;

    rel->v_map[ev->code](pInfo, value, 0, rel->v_map_data[ev->code]);
}

int
EvdevAxesOn (DeviceIntPtr device)
{
    return Success;
}

int
EvdevAxesOff (DeviceIntPtr device)
{
    return Success;
}

static int
EvdevAxisAbsNew(InputInfoPtr pInfo)
{
    evdevDeviceRec *pEvdev = pInfo->private;
    evdevStateRec *state = &pEvdev->state;
    evdevAbsRec *abs;
    struct input_absinfo absinfo;
    char option[128], value[128];
    const char *s;
    int i, j, real_axes;
    evdev_option_token_t *tokens;

    real_axes = 0;
    for (i = 0; i < ABS_MAX; i++)
	if (test_bit (i, pEvdev->bits.abs))
	    real_axes++;

    if (!real_axes)
	return !Success;

    state->abs = abs = Xcalloc (sizeof (evdevAbsRec));

    xf86Msg(X_INFO, "%s: Found %d absolute axes.\n", pInfo->name, real_axes);
    xf86Msg(X_INFO, "%s: Configuring as pointer.\n", pInfo->name);
    pInfo->flags |= XI86_POINTER_CAPABLE | XI86_SEND_DRAG_EVENTS |
	XI86_CONFIGURED;
    pInfo->type_name = XI_MOUSE;
    pInfo->conversion_proc = EvdevConvert;

    for (i = 0, j = 0; i < ABS_MAX; i++) {
	if (!test_bit (i, pEvdev->bits.abs))
	    continue;

	if (ioctl (pInfo->fd, EVIOCGABS(i), &absinfo) < 0) {
	    xf86Msg(X_ERROR, "ioctl EVIOCGABS failed: %s\n", strerror(errno));
	    return !Success;
	}

	snprintf(option, sizeof(option), "Abs%sMapTo", abs_axis_names[i]);
	snprintf(value, sizeof(value), "AbsAxis %d %d %d", j, absinfo.minimum, absinfo.maximum);

	EvdevParseMapOption (pInfo, option, value, &abs->v_map_data[i], &abs->v_map[i]);

	snprintf(option, sizeof(option), "Abs%sOptions", abs_axis_names[i]);
	if (i == ABS_X || i == ABS_Y)
	    s = xf86SetStrOption(pInfo->options, option, "use_touch mode_auto");
	else
	    s = xf86SetStrOption(pInfo->options, option, "");
	if (s[0]) {
	    tokens = EvdevTokenize (s, " "); 
	    if (!EvdevParseAbsOptions (pInfo, option, tokens, &abs->v_flags[i]))
		xf86Msg (X_ERROR, "%s: Unable to parse '%s' as absolute options.\n", pInfo->name, s);
	    EvdevFreeTokens (tokens);
	}
	abs->v_flags[i] |= EV_ABS_V_PRESENT;

	j++;
    }

    state->abs->axes = real_axes;

    return Success;
}

static int
EvdevAxisAbsNew1(InputInfoPtr pInfo)
{
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;
    char *s;

    if (!state->abs)
	return !Success;

    xf86Msg(X_CONFIG, "%s: Configuring %d absolute axes.\n", pInfo->name,
	    state->abs->axes);

    {
	int btn;

	s = xf86SetStrOption(pInfo->options, "AbsoluteTouch", "DIGI_Touch");
	btn = EvdevBtnFind (pInfo, s);
	if (btn != -1) {
	    if (EvdevBtnExists (pInfo, btn)) {
		state->abs->flags |= EV_ABS_USE_TOUCH;
		xf86Msg(X_ERROR, "%s: Button: %d.\n", pInfo->name, btn);
		xf86Msg(X_ERROR, "%s: state->btn: %p.\n", pInfo->name, state->btn);
		state->btn->callback[btn] = &EvdevAxesTouchCallback;
	    } else {
		xf86Msg(X_ERROR, "%s: AbsoluteTouch: '%s' does not exist.\n", pInfo->name, s);
	    }
	} else {
	    xf86Msg(X_ERROR, "%s: AbsoluteTouch: '%s' is not a valid button name.\n", pInfo->name, s);
	}
    }

    return Success;
}

static int
EvdevAxisRelNew(InputInfoPtr pInfo)
{
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;
    evdevRelPtr rel;
    char *s, option[128], value[128];
    int i, j, real_axes;
    evdev_option_token_t *tokens;

    real_axes = 0;
    for (i = 0; i < REL_MAX; i++)
	if (test_bit (i, pEvdev->bits.rel))
	    real_axes++;

    if (!real_axes && (!state->abs || state->abs->axes < 2))
	return !Success;

    state->rel = rel = Xcalloc (sizeof (evdevRelRec));

    xf86Msg(X_INFO, "%s: Found %d relative axes.\n", pInfo->name,
	    real_axes);
    xf86Msg(X_INFO, "%s: Configuring as pointer.\n", pInfo->name);
    pInfo->flags |= XI86_POINTER_CAPABLE | XI86_SEND_DRAG_EVENTS |
	XI86_CONFIGURED;
    pInfo->type_name = XI_MOUSE;
    pInfo->conversion_proc = EvdevConvert;

    for (i = 0, j = 0; i < REL_MAX; i++) {
	if (!test_bit (i, pEvdev->bits.rel))
	    continue;

	snprintf(option, sizeof(option), "Rel%sMapTo", rel_axis_names[i]);
	if (i == REL_WHEEL || i == REL_Z)
	    snprintf(value, sizeof(value), "Buttons 4 5 1");
	else if (i == REL_HWHEEL)
	    snprintf(value, sizeof(value), "Buttons 6 7 1");
	else
	    snprintf(value, sizeof(value), "RelAxis %d", j);

	EvdevParseMapOption (pInfo, option, value, &rel->v_map_data[i], &rel->v_map[i]);

	snprintf(option, sizeof(option), "Rel%sOptions", rel_axis_names[i]);
	s = xf86SetStrOption(pInfo->options, option, "");
	if (s[0]) {
	    tokens = EvdevTokenize (s, " "); 
	    if (!EvdevParseRelOptions (pInfo, option, tokens, &rel->v_flags[i]))
		xf86Msg (X_ERROR, "%s: Unable to parse '%s' as relative options.\n", pInfo->name, s);
	    EvdevFreeTokens (tokens);
	}
	rel->v_flags[i] |= EV_REL_V_PRESENT;


	j++;
    }

    return Success;
}

int
EvdevAxesNew0 (InputInfoPtr pInfo)
{
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;
    int ret = Success;

    state->axes = Xcalloc (sizeof (evdevAxesRec));
    if (EvdevAxisAbsNew(pInfo) != Success)
	ret = !Success;
    if (EvdevAxisRelNew(pInfo) != Success)
	ret = !Success;
    if (!state->abs && !state->rel) {
	Xfree (state->axes);
	state->axes = NULL;
    }

    return ret;
}

int
EvdevAxesNew1 (InputInfoPtr pInfo)
{
    evdevDeviceRec *pEvdev = pInfo->private;
    evdevStateRec *state = &pEvdev->state;
    evdevAxesRec *axes = state->axes;
    int i, ret = Success;

    if (!state->axes)
	return ret;

    for (i = 0; i < AXES_MAX; i++)
	if (axes->v_flags[i] & EV_AXES_V_PRESENT)
	    axes->axes = i + 1;

    if (EvdevAxisAbsNew1(pInfo) != Success)
	ret = !Success;
    if (!state->abs && !state->rel) {
	Xfree (state->axes);
	state->axes = NULL;
    }

    return ret;
}


static void
EvdevPtrCtrlProc(DeviceIntPtr device, PtrCtrl *ctrl)
{
    /* Nothing to do, dix handles all settings */
}

int
EvdevAxesInit (DeviceIntPtr device)
{
    InputInfoPtr pInfo = device->public.devicePrivate;
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;
    evdevAxesRec *axes = state->axes;
    AbsoluteClassRec *dev_abs;
    int i;
    const char *s;

    if (!axes || !axes->axes)
	return Success;

    xf86Msg(X_CONFIG, "%s: %d valuators.\n", pInfo->name,
	    axes->axes);

    if (!InitValuatorClassDeviceStruct(device, axes->axes,
                                       GetMotionHistory,
                                       GetMotionHistorySize(),
                                       0))
        return !Success;


    /*
     * This has to go in Init, because until now there is no valuator struct
     * allocated.
     */
    s = xf86SetStrOption(pInfo->options, "Mode", "Absolute");
    if (!strcasecmp(s, "Absolute")) {
	pInfo->dev->valuator->mode = Absolute;
	xf86Msg(X_CONFIG, "%s: Configuring in %s mode.\n", pInfo->name, s);
    } else if (!strcasecmp(s, "Relative")) {
	pInfo->dev->valuator->mode = Relative;
	xf86Msg(X_CONFIG, "%s: Configuring in %s mode.\n", pInfo->name, s);
    } else {
	pInfo->dev->valuator->mode = Absolute;
	xf86Msg(X_CONFIG, "%s: Unknown Mode: %s.\n", pInfo->name, s);
    }

    /*
     * Yes, we want to do this for relative devices too.
     * Some of the settings are useful for both.
     */
    if ((axes->v_flags[0] & EV_AXES_V_PRESENT) &&
	    (axes->v_flags[1] & EV_AXES_V_PRESENT) &&
	    InitAbsoluteClassDeviceStruct (device)) {
	dev_abs = device->absolute;
	if (axes->v_min[0] != axes->v_max[1] && axes->v_min[1] != axes->v_max[1]) {
	    device->absolute->min_x = axes->v_min[0];
	    device->absolute->max_x = axes->v_max[0];
	    device->absolute->min_y = axes->v_min[1];
	    device->absolute->max_y = axes->v_max[1];
	}
    }

    for (i = 0; i < axes->axes; i++) {
	xf86InitValuatorAxisStruct(device, i, -1, -1, 1, 1, 1);

	xf86InitValuatorDefaults(device, i);
    }

    if (!InitPtrFeedbackClassDeviceStruct(device, EvdevPtrCtrlProc))
        return !Success;

    return Success;
}

static void
EvdevAxesTouchCallback (InputInfoPtr pInfo, int button, int value)
{
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;
    int i;

#if DEBUG
    xf86Msg(X_INFO, "%s: Touch callback; %d.\n", pInfo->name, value);
#endif
    if (state->abs->flags & EV_ABS_USE_TOUCH) {
	if (value) {
	    state->abs->flags |= EV_ABS_TOUCH;
	    for (i = 0; i < ABS_MAX; i++)
		if (state->abs->v_flags[i] & EV_ABS_V_USE_TOUCH)
		    state->abs->v_flags[i] |= EV_ABS_V_RESET;
	} else
	    state->abs->flags &= ~EV_ABS_TOUCH;
    }
}
