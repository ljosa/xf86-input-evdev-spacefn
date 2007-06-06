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
EvdevBtnOn (DeviceIntPtr device)
{
    InputInfoPtr pInfo = device->public.devicePrivate;
    evdevDevicePtr pEvdev = pInfo->private;
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
EvdevBtnOff (DeviceIntPtr device)
{
    return Success;
}

#if 0
/*
 * Warning, evil lives here.
 */
static void
EvdevBtnCalcRemap (InputInfoPtr pInfo)
{
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;
    evdevBtnPtr btn = state->btn;
    int i, j, base, clear, fake, bit;

    for (i = 0, base = 1, fake = 0; i < pEvdev->state.btn->real_buttons; i++) {
	if (state->rel) {
	    do {
		clear = 1;
		for (j = 0; j < REL_MAX; j++) {
		    if (state->rel->btnMap[j][0] == (i + base)) {
			base++;
			clear = 0;
			break;
		    }
		    if (state->rel->btnMap[j][1] == (i + base)) {
			base++;
			clear = 0;
			break;
		    }
		}
	    } while (!clear);
	}

	if (!fake && base != 1)
	    fake = i;

	/*
	 * See if the button is ignored for mapping purposes.
	 */
	if (btn->ignore[i] & EV_BTN_IGNORE_MAP)
	    continue;

	/*
	 * See if the button actually exists, otherwise don't bother.
	 */
	bit = i;
	bit += BTN_MISC;
	if ((bit >= BTN_MOUSE) && (bit < BTN_JOYSTICK)) {
	    bit -= BTN_MOUSE - BTN_MISC;
	} else if ((bit >= BTN_MISC) && (bit < BTN_MOUSE)) {
	    bit += BTN_MOUSE - BTN_MISC;
	}
	if (!test_bit (bit, pEvdev->bits.key))
	    continue;

	btn->buttons = btn->map[i] = i + base;
    }

    if ((!fake || fake >= 3) &&
	    test_bit(BTN_RIGHT, pEvdev->bits.key) &&
	    test_bit(BTN_MIDDLE, pEvdev->bits.key)) {
	base = btn->map[1];
	btn->map[1] = btn->map[2];
	btn->map[2] = base;
    }

    if (state->rel) {
	for (i = 0; i < REL_MAX; i++) {
	    if (state->rel->btnMap[i][0] > btn->buttons)
		btn->buttons = state->rel->btnMap[i][0];
	    if (state->rel->btnMap[i][1] > btn->buttons)
		btn->buttons = state->rel->btnMap[i][1];
	}
    }
}
#endif


int
EvdevBtnNew0(InputInfoPtr pInfo)
{
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;
    char option[64];
    int i, j, btn;

    state->btn = Xcalloc (sizeof (evdevBtnRec));

    /*
     * XXX: This is evil.
     * For reasons related to handling pathological remapping cases, and
     * differences between HID and X, pretend a middle button exists
     * whenever a right button exists.
     */
    if (test_bit (BTN_RIGHT, pEvdev->bits.key))
	set_bit (BTN_MIDDLE, pEvdev->bits.key);

    for (i = BTN_MISC; i < (KEY_OK - 1); i++) {
	btn = i;
	if ((btn >= BTN_MOUSE) && (btn < BTN_JOYSTICK)) {
	    btn -= BTN_MOUSE - BTN_MISC;
	} else if ((btn >= BTN_MISC) && (btn < BTN_MOUSE)) {
	    btn += BTN_MOUSE - BTN_MISC;
	}
	btn -= BTN_MISC;

	snprintf(option, sizeof(option), "%sIgnoreX", button_names[btn]);
	if (i >= BTN_DIGI && i < BTN_WHEEL)
	    j = xf86SetIntOption(pInfo->options, option, 1);
	else
	    j = xf86SetIntOption(pInfo->options, option, 0);
	if (j)
	    state->btn->ignore[btn] |= EV_BTN_IGNORE_X;

	snprintf(option, sizeof(option), "%sIgnoreEvdev", button_names[btn]);
	j = xf86SetIntOption(pInfo->options, option, 0);
	if (j) {
	    state->btn->ignore[btn] |= EV_BTN_IGNORE_EVDEV;
	    continue;
	}

	if (test_bit (i, pEvdev->bits.key))
	    state->btn->real_buttons = btn + 1;
    }

    if (state->btn->real_buttons)
        xf86Msg(X_INFO, "%s: Found %d mouse buttons\n", pInfo->name, state->btn->real_buttons);

    return Success;
}

int
EvdevBtnNew1(InputInfoPtr pInfo)
{
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;

    if (!state->btn)
	return !Success;

#if 0
    EvdevBtnCalcRemap (pInfo);
#else
    state->btn->buttons = state->btn->real_buttons;
#endif

    if (state->btn->buttons)
	xf86Msg(X_INFO, "%s: Configured %d mouse buttons\n", pInfo->name, state->btn->buttons);
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
EvdevBtnProcess (InputInfoPtr pInfo, struct input_event *ev)
{
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;
    int button;

    if (!state->btn)
	return;

    button = ev->code;

    if ((ev->code >= BTN_MOUSE) && (ev->code < BTN_JOYSTICK)) {
	button -= BTN_MOUSE - BTN_MISC;
    } else if ((ev->code >= BTN_MISC) && (ev->code < BTN_MOUSE)) {
	button += BTN_MOUSE - BTN_MISC;
    }

    button -= BTN_MISC;

    if (state->btn->ignore[button] & EV_BTN_IGNORE_EVDEV)
	return;

    if (state->btn->callback[button])
	state->btn->callback[button](pInfo, button, ev->value);

    if (state->btn->ignore[button] & EV_BTN_IGNORE_X)
	return;

#if 0
    button = state->btn->map[button];
#endif
    xf86PostButtonEvent (pInfo->dev, 0, button, ev->value, 0, 0);
}

int
EvdevBtnFind (InputInfoPtr pInfo, const char *button)
{
    int i;

    for (i = 0; button_names[i]; i++)
	if (!strcasecmp(button, button_names[i]))
	    return i + 1;

    return -1;
}

int
EvdevBtnExists (InputInfoPtr pInfo, int button)
{
    evdevDevicePtr pEvdev = pInfo->private;

    button += BTN_MISC;

    xf86Msg(X_INFO, "%s: Checking button %s (%d)\n", pInfo->name, button_names[button - BTN_MISC], button);

    if ((button >= BTN_MOUSE) && (button < BTN_JOYSTICK)) {
	button -= BTN_MOUSE - BTN_MISC;
    } else if ((button >= BTN_MISC) && (button < BTN_MOUSE)) {
	button += BTN_MOUSE - BTN_MISC;
    }

    xf86Msg(X_INFO, "%s: Checking bit %d\n", pInfo->name, button);
    return test_bit(button, pEvdev->bits.key);
}
