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

#include "evdev.h"

#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <X11/extensions/XIproto.h>

#include <linux/input.h>

#include <misc.h>
#include <xf86.h>
#include <xf86str.h>
#include <xf86_OSproc.h>
#include <xf86Xinput.h>
#include <exevents.h>
#include <mipointer.h>

#include <xf86Module.h>

static char *button_names[] = {
    "MISC_0",
    "MISC_1",
    "MISC_2",
    "MISC_3",
    "MISC_4",
    "MISC_5",
    "MISC_6",
    "MISC_7",
    "MISC_8",
    "MISC_9",
    "MISC_10",
    "MISC_11",
    "MISC_12",
    "MISC_13",
    "MISC_14",
    "MISC_15",
    "MOUSE_LEFT",
    "MOUSE_RIGHT",
    "MOUSE_MIDDLE",
    "MOUSE_SIDE",
    "MOUSE_EXTRA",
    "MOUSE_FORWARD",
    "MOUSE_BACK",
    "MOUSE_TASK",
    "MOUSE_8",
    "MOUSE_9",
    "MOUSE_10",
    "MOUSE_12",
    "MOUSE_13",
    "MOUSE_14",
    "MOUSE_15",
    "JOY_TRIGGER",
    "JOY_THUMB",
    "JOY_THUMB2",
    "JOY_TOP",
    "JOY_TOP2",
    "JOY_PINKIE",
    "JOY_BASE",
    "JOY_BASE2",
    "JOY_BASE3",
    "JOY_BASE4",
    "JOY_BASE5",
    "JOY_BASE6",
    "JOY_12",
    "JOY_13",
    "JOY_14",
    "JOY_DEAD",
    "GAME_A",
    "GAME_B",
    "GAME_C",
    "GAME_X",
    "GAME_Y",
    "GAME_Z",
    "GAME_TL",
    "GAME_TR",
    "GAME_TL2",
    "GAME_TR2",
    "GAME_SELECT",
    "GAME_START",
    "GAME_MODE",
    "GAME_THUMBL",
    "GAME_THUMBR",
    "GAME_15",
    "DIGI_TOOL_PEN",
    "DIGI_TOOL_RUBBER",
    "DIGI_TOOL_BRUSH",
    "DIGI_TOOL_PENCIL",
    "DIGI_TOOL_AIRBRUSH",
    "DIGI_TOOL_FINGER",
    "DIGI_TOOL_MOUSE",
    "DIGI_TOOL_LENS",
    "DIGI_8",
    "DIGI_9",
    "DIGI_TOUCH",
    "DIGI_STYLUS",
    "DIGI_STYLUS2",
    "DIGI_TOOL_DOUBLETAP",
    "DIGI_TOOL_TRIPLETAP",
    "DIGI_15",
    "WHEEL_GEAR_UP",
    "WHEEL_GEAR_DOWN",
    "WHEEL_2",
    "WHEEL_3",
    "WHEEL_4",
    "WHEEL_5",
    "WHEEL_6",
    "WHEEL_7",
    "WHEEL_8",
    "WHEEL_9",
    "WHEEL_10",
    "WHEEL_11",
    "WHEEL_12",
    "WHEEL_13",
    "WHEEL_14",
    "WHEEL_15",
    NULL
};

void
EvdevBtnPostFakeClicks(InputInfoRec *pInfo, int button, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        xf86PostButtonEvent(pInfo->dev, 0, button, 1, 0, 0);
        xf86PostButtonEvent(pInfo->dev, 0, button, 0, 0, 0);
    }
}

static void
EvdevMapButton (InputInfoRec *pInfo, int value, int mode, void *map_data)
{
    long button = (long) map_data;

    xf86PostButtonEvent (pInfo->dev, 0, button, value, 0, 0);
}

Bool
EvdevParseMapToButton (InputInfoRec *pInfo,
	const char *name,
	evdev_option_token_t *option,
	void **map_data, evdev_map_func_f *map_func)
{
    evdevDeviceRec *pEvdev = pInfo->private;
    evdevStateRec *state = &pEvdev->state;
    evdevBtnRec *btn = state->btn;
    int button;

    errno = 0;
    button = strtol (option->str, NULL, 0);
    if (errno)
	button = EvdevBtnFind (pInfo, option->str);
    if ((button < 0) || (button > BTN_MAX)) {
	xf86Msg (X_ERROR, "%s: %s: Button %d out of range.\n", pInfo->name, name, button);
	return 0;
    }

    if (btn->b_flags[button] & EV_BTN_B_PRESENT) {
	xf86Msg (X_ERROR, "%s: %s: Button %d already claimed.\n", pInfo->name, name, button);
	return 0;
    }

    btn->b_flags[button] = EV_BTN_B_PRESENT;

    *map_data = (void *) button;
    *map_func = EvdevMapButton;

    return 1;
}

typedef struct {
    int button_plus;
    int button_minus;
    int step;
    int count;
} MapButtons_t;

static void
EvdevMapButtons (InputInfoRec *pInfo, int value, int mode, void *map_data)
{
    MapButtons_t *map = map_data;
    int i;

    if (!map)
	return;

    map->count += value;
    i = map->count / map->step;
    if (i) {
	map->count -= i * map->step;
	if (i > 0)
	    EvdevBtnPostFakeClicks (pInfo, map->button_plus, i);
	else
	    EvdevBtnPostFakeClicks (pInfo, map->button_minus, -i);
    }
}

Bool
EvdevParseMapToButtons (InputInfoRec *pInfo,
	const char *name,
	evdev_option_token_t *option,
	void **map_data, evdev_map_func_f *map_func)
{
    evdevDeviceRec *pEvdev = pInfo->private;
    evdevStateRec *state = &pEvdev->state;
    evdevBtnRec *btn = state->btn;
    int btn_plus, btn_minus;
    MapButtons_t *map;

    errno = 0;
    btn_plus = strtol (option->str, NULL, 0);
    if (errno)
	btn_plus = EvdevBtnFind (pInfo, option->str);
    if ((btn_plus < 0) || (btn_plus > BTN_MAX)) {
	xf86Msg (X_ERROR, "%s: %s: Button %d out of range.\n", pInfo->name, name, btn_plus);
	return 0;
    }

    if (btn->b_flags[btn_plus] & EV_BTN_B_PRESENT) {
	xf86Msg (X_ERROR, "%s: %s: Button %d already claimed.\n", pInfo->name, name, btn_plus);
	return 0;
    }

    option = option->next;
    if (!option) {
	xf86Msg (X_ERROR, "%s: %s: No button minus.\n", pInfo->name, name);
	return 0;
    }

    errno = 0;
    btn_minus = strtol (option->str, NULL, 0);
    if (errno)
	btn_minus = EvdevBtnFind (pInfo, option->str);
    if ((btn_minus < 0) || (btn_minus > BTN_MAX)) {
	xf86Msg (X_ERROR, "%s: %s: Button %d out of range.\n", pInfo->name, name, btn_minus);
	return 0;
    }

    if (btn->b_flags[btn_minus] & EV_BTN_B_PRESENT) {
	xf86Msg (X_ERROR, "%s: %s: Button %d already claimed.\n", pInfo->name, name, btn_minus);
	return 0;
    }
    errno = 0;

    btn->b_flags[btn_plus] = EV_BTN_B_PRESENT;
    btn->b_flags[btn_minus] = EV_BTN_B_PRESENT;

    map = calloc(1, sizeof (MapButtons_t));
    map->button_plus = btn_plus;
    map->button_minus = btn_minus;
    map->step = 1;

    *map_data = (void *) map;
    *map_func = EvdevMapButtons;

    return 1;
}

int
EvdevBtnInit (DeviceIntRec *device)
{
    InputInfoRec *pInfo = device->public.devicePrivate;
    evdevDeviceRec *pEvdev = pInfo->private;
    CARD8 *map;
    int i;

    if (!pEvdev->state.btn)
	return Success;

    map = Xcalloc (sizeof (CARD8) * (pEvdev->state.btn->buttons + 1));

    for (i = 0; i <= pEvdev->state.btn->buttons; i++)
        map[i] = i;

    xf86Msg(X_CONFIG, "%s: Registering %d buttons.\n", pInfo->name,
	    pEvdev->state.btn->buttons);
    if (!InitButtonClassDeviceStruct (device, pEvdev->state.btn->buttons, map)) {
	pEvdev->state.btn->buttons = 0;

        return !Success;
    }

    Xfree (map);

    return Success;
}

int
EvdevBtnOn (DeviceIntRec *device)
{
    InputInfoRec *pInfo = device->public.devicePrivate;
    evdevDeviceRec *pEvdev = pInfo->private;
    int i, blocked;

    if (!pEvdev->state.btn)
	return Success;

    blocked = xf86BlockSIGIO ();
    for (i = 1; i <= pEvdev->state.btn->buttons; i++)
	xf86PostButtonEvent (device, 0, i, 0, 0, 0);
    xf86UnblockSIGIO (blocked);

    return Success;
}

int
EvdevBtnOff (DeviceIntRec *device)
{
    return Success;
}


int
EvdevBtnNew0(InputInfoRec *pInfo)
{
    evdevDeviceRec *pEvdev = pInfo->private;
    evdevStateRec *state = &pEvdev->state;

    state->btn = Xcalloc (sizeof (evdevBtnRec));

    return Success;
}

int
EvdevBtnNew1(InputInfoRec *pInfo)
{
    evdevDeviceRec *pEvdev = pInfo->private;
    evdevStateRec *state = &pEvdev->state;
    evdevBtnRec *btn = state->btn;
    char option[128], value[128];
    int i, b, j, target;

    if (!btn)
	return !Success;

    for (i = 0; i < BTN_MAX; i++) {
	b = i + BTN_MISC;
	if (!test_bit (b, pEvdev->bits.key))
	    continue;

	btn->real_buttons++;

	snprintf(option, sizeof(option), "Button%sMapTo", button_names[i]);

	if (b >= BTN_DIGI && b < BTN_WHEEL)
	    target = -1;
	else if (b == BTN_RIGHT)
	    target = 3;
	else if (b == BTN_MIDDLE)
	    target = 2;
	else if (b >= BTN_MOUSE && b < BTN_JOYSTICK)
	    target = 1 + i - (BTN_MOUSE - BTN_MISC);
	else if (b >= BTN_MISC && b < BTN_MOUSE)
	    target = 1 + i + (BTN_MOUSE - BTN_MISC);
	else
	    target = 1 + i;

	if (btn->b_flags[target] & EV_BTN_B_PRESENT) {
	    for (j = target; j < BTN_MAX; j++)
		if (!(btn->b_flags[j] & EV_BTN_B_PRESENT)) {
		    target = j;
		    break;
		}
	}

	if (target > 0)
	    snprintf (value, sizeof (value), "Button %d", target);
	else
	    snprintf (value, sizeof (value), "null");

	EvdevParseMapOption (pInfo, option, value, &btn->b_map_data[i], &btn->b_map[i]);
    }

    if (state->btn->real_buttons)
        xf86Msg(X_INFO, "%s: Found %d mouse buttons\n", pInfo->name, state->btn->real_buttons);

    for (i = 0; i < BTN_MAX; i++)
	if (btn->b_flags[i] & EV_BTN_B_PRESENT)
	    btn->buttons = i + 1;

    if (state->btn->buttons)
	xf86Msg(X_INFO, "%s: Configured %d mouse buttons.\n", pInfo->name, state->btn->buttons);
    else {
	Xfree (state->btn);
	state->btn = NULL;
	return !Success;
    }

    pInfo->flags |= XI86_SEND_DRAG_EVENTS | XI86_CONFIGURED;
    /*
     * FIXME: Mouse may not be accurate.
     * Check buttons to see if we're actually a joystick or something.
     */
    pInfo->type_name = XI_MOUSE;

    return Success;
}

void
EvdevBtnProcess (InputInfoRec *pInfo, struct input_event *ev)
{
    evdevDeviceRec *pEvdev = pInfo->private;
    evdevStateRec *state = &pEvdev->state;
    int button;

    if (!state->btn)
	return;

    button = ev->code;

    button -= BTN_MISC;

    if (state->btn->callback[button])
	state->btn->callback[button](pInfo, button, ev->value);

    if (state->btn->b_map[button])
	state->btn->b_map[button](pInfo, ev->value, -1, state->btn->b_map_data[button]);
}

int
EvdevBtnFind (InputInfoRec *pInfo, const char *button)
{
    int i;

    for (i = 0; button_names[i]; i++)
	if (!strcasecmp(button, button_names[i]))
	    return i + 1;

    return -1;
}

int
EvdevBtnExists (InputInfoRec *pInfo, int button)
{
    evdevDeviceRec *pEvdev = pInfo->private;

    xf86Msg(X_INFO, "%s: Checking button %s (%d)\n", pInfo->name, button_names[button], button);

    button += BTN_MISC;

    xf86Msg(X_INFO, "%s: Checking bit %d\n", pInfo->name, button);
    return test_bit(button, pEvdev->bits.key);
}
