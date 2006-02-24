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
/*
 * Copyright © 2004 Red Hat, Inc.
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
 * RED HAT DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL RED HAT BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author:  Kristian Høgsberg (krh@redhat.com)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <X11/extensions/XIproto.h>

#include <string.h>
#include <errno.h>

/* The libc wrapper just blows... linux/input.h must be included
 * before xf86_ansic.h and xf86_libc.h so we avoid defining ioctl
 * twice. */
#include "evdev.h"

#include <xf86.h>

#include <xf86Module.h>


#include <xf86_OSproc.h>

/*
 * FIXME: This should most definitely not be here.
 * But I need it, even if it _is_ private.
 */

void xf86ActivateDevice(InputInfoPtr pInfo);

static void
EvdevReadInput(InputInfoPtr pInfo)
{
    struct input_event ev;
    int len, value;

    while (xf86WaitForInput (pInfo->fd, 0) > 0) {
        len = read(pInfo->fd, &ev, sizeof ev);
        if (len != sizeof ev) {
            /* The kernel promises that we always only read a complete
             * event, so len != sizeof ev is an error. */
            xf86Msg(X_ERROR, "Read error: %s\n", strerror(errno));
            break;
        }

        /* Get the signed value, earlier kernels had this as unsigned */
        value = ev.value;

        switch (ev.type) {
        case EV_REL:
	    EvdevRelProcess (pInfo, &ev);
	    break;

        case EV_ABS:
	    EvdevAbsProcess (pInfo, &ev);
            break;

        case EV_KEY:
	    if ((ev.code >= BTN_MISC) && (ev.code < KEY_OK))
		EvdevBtnProcess (pInfo, &ev);
	    else
		EvdevKeyProcess (pInfo, &ev);
	    break;

        case EV_SYN:
	    if (ev.code == SYN_REPORT) {
		EvdevRelSyn (pInfo);
		EvdevAbsSyn (pInfo);
		/* EvdevBtnSyn (pInfo); */
		/* EvdevKeySyn (pInfo); */
	    }
            break;
        }
    }
}

static void
EvdevSigioReadInput (int fd, void *data)
{
    EvdevReadInput ((InputInfoPtr) data);
}

static int
EvdevProc(DeviceIntPtr device, int what)
{
    InputInfoPtr pInfo = device->public.devicePrivate;
    evdevDevicePtr pEvdev = pInfo->private;

    switch (what)
    {
    case DEVICE_INIT:
	if (pEvdev->state.abs_axes)
	    EvdevAbsInit (device);
	if (pEvdev->state.rel_axes)
	    EvdevRelInit (device);
	if (pEvdev->state.buttons)
	    EvdevBtnInit (device);
	if (pEvdev->state.keys)
	    EvdevKeyInit (device);
	xf86Msg(X_INFO, "%s: Init\n", pInfo->name);
	break;

    case DEVICE_ON:
	xf86Msg(X_INFO, "%s: On\n", pInfo->name);
	if (device->public.on)
	    break;

	if ((pInfo->fd = evdevGetFDForDevice (pEvdev)) == -1) {
	    xf86Msg(X_ERROR, "%s: cannot open input device.\n", pInfo->name);

	    if (pEvdev->phys)
		xfree(pEvdev->phys);
	    pEvdev->phys = NULL;

	    if (pEvdev->device)
		xfree(pEvdev->device);
	    pEvdev->device = NULL;

	    return BadRequest;
	}

	if (pEvdev->state.can_grab)
	    if (ioctl(pInfo->fd, EVIOCGRAB, (void *)1))
		xf86Msg(X_ERROR, "%s: Unable to grab device (%s).\n", pInfo->name, strerror(errno));

	xf86FlushInput (pInfo->fd);
	if (!xf86InstallSIGIOHandler (pInfo->fd, EvdevSigioReadInput, pInfo))
	    AddEnabledDevice (pInfo->fd);

	device->public.on = TRUE;

	if (pEvdev->state.abs_axes)
	    EvdevAbsOn (device);
	if (pEvdev->state.rel_axes)
	    EvdevRelOn (device);
	if (pEvdev->state.buttons)
	    EvdevBtnOn (device);
	if (pEvdev->state.keys)
	    EvdevKeyOn (device);
	break;

    case DEVICE_CLOSE:
    case DEVICE_OFF:
	xf86Msg(X_INFO, "%s: Off\n", pInfo->name);
	if (pInfo->fd != -1) {
	    if (pEvdev->state.can_grab)
		ioctl(pInfo->fd, EVIOCGRAB, (void *)0);

	    RemoveEnabledDevice (pInfo->fd);
	    xf86RemoveSIGIOHandler (pInfo->fd);
	    close (pInfo->fd);

	    if (pEvdev->state.abs_axes)
		EvdevAbsOff (device);
	    if (pEvdev->state.rel_axes)
		EvdevRelOff (device);
	    if (pEvdev->state.buttons)
		EvdevBtnOff (device);
	    if (pEvdev->state.keys)
		EvdevKeyOff (device);
	}

	device->public.on = FALSE;
	break;
    }

    return Success;
}

static int
EvdevSwitchMode (ClientPtr client, DeviceIntPtr device, int mode)
{
    InputInfoPtr pInfo = device->public.devicePrivate;
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;

    switch (mode)
    {
	case Absolute:
	case Relative:
	    if (state->abs_axes)
		state->mode = mode;
	    else
		return !Success;
	    break;
	case SendCoreEvents:
	case DontSendCoreEvents:
	    xf86XInputSetSendCoreEvents (pInfo, (mode == SendCoreEvents));
	    break;
	default:
	    return !Success;
    }

    return Success;
}

static Bool
EvdevNew(evdevDriverPtr driver, evdevDevicePtr device)
{
    InputInfoPtr pInfo;
    char name[512] = {0};

    if (!(pInfo = xf86AllocateInput(driver->drv, 0)))
	return 0;

    /* Initialise the InputInfoRec. */
    strncat (name, driver->dev->identifier, sizeof(name));
    strncat (name, "-", sizeof(name));
    strncat (name, device->phys, sizeof(name));
    pInfo->name = xstrdup(name);
    pInfo->flags = 0;
    pInfo->type_name = "UNKNOWN";
    pInfo->device_control = EvdevProc;
    pInfo->read_input = EvdevReadInput;
    pInfo->switch_mode = EvdevSwitchMode;
    pInfo->motion_history_proc = xf86GetMotionEvents;
    pInfo->conf_idev = driver->dev;

    pInfo->private = device;

    device->callback = EvdevProc;
    device->pInfo = pInfo;

    xf86CollectInputOptions(pInfo, NULL, NULL);
    xf86ProcessCommonOptions(pInfo, pInfo->options);

    if ((pInfo->fd = evdevGetFDForDevice (device)) == -1) {
	xf86Msg(X_ERROR, "%s: cannot open input device\n", pInfo->name);
	pInfo->private = NULL;
	xf86DeleteInput (pInfo, 0);
	return 0;
    }

    if (ioctl(pInfo->fd, EVIOCGRAB, (void *)1)) {
	xf86Msg(X_INFO, "%s: Unable to grab device (%s).  Cowardly refusing to check use as keyboard.\n", pInfo->name, strerror(errno));
	device->state.can_grab = 0;
    } else {
	device->state.can_grab = 1;
        ioctl(pInfo->fd, EVIOCGRAB, (void *)0);
    }

    /* XXX: Note, the order of these is important. */
    EvdevAbsNew (pInfo);
    EvdevRelNew (pInfo);
    EvdevBtnNew (pInfo);
    if (device->state.can_grab)
	EvdevKeyNew (pInfo);

    close (pInfo->fd);
    pInfo->fd = -1;

    pInfo->flags |= XI86_OPEN_ON_INIT;
    if (!(pInfo->flags & XI86_CONFIGURED)) {
        xf86Msg(X_ERROR, "%s: Don't know how to use device.\n", pInfo->name);
	pInfo->private = NULL;
	xf86DeleteInput (pInfo, 0);
        return 0;
    }

    if (driver->configured) {
	xf86ActivateDevice (pInfo);

	pInfo->dev->inited = (device->callback(device->pInfo->dev, DEVICE_INIT) == Success);
	EnableDevice (pInfo->dev);
    }

    return 1;
}

static InputInfoPtr
EvdevCorePreInit(InputDriverPtr drv, IDevPtr dev, int flags)
{
    evdevDriverPtr pEvdev;

    if (!(pEvdev = Xcalloc(sizeof(*pEvdev))))
        return NULL;

    pEvdev->name = xf86CheckStrOption(dev->commonOptions, "Name", NULL);
    pEvdev->phys = xf86CheckStrOption(dev->commonOptions, "Phys", NULL);
    pEvdev->device = xf86CheckStrOption(dev->commonOptions, "Device", NULL);
    xf86Msg(X_ERROR, "%s: name: %s, phys: %s, device: %s.\n", dev->identifier,
	    pEvdev->name, pEvdev->phys, pEvdev->device);

    pEvdev->callback = EvdevNew;

    pEvdev->dev = dev;
    pEvdev->drv = drv;

    if (!pEvdev->name && !pEvdev->phys && !pEvdev->device) {
        xf86Msg(X_ERROR, "%s: No device identifiers specified.\n", dev->identifier);
        xfree(pEvdev);
        return NULL;
    }

    if (!evdevStart (drv)) {
	xf86Msg(X_ERROR, "%s: cannot start evdev brain.\n", dev->identifier);
        xfree(pEvdev);
	return NULL;
    }

    evdevNewDriver (pEvdev);

    if (pEvdev->devices && pEvdev->devices->pInfo)
	return pEvdev->devices->pInfo;

    return NULL;
}

_X_EXPORT InputDriverRec EVDEV = {
    1,
    "evdev",
    NULL,
    EvdevCorePreInit,
    NULL,
    NULL,
    0
};

#ifdef XFree86LOADER

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
    0, /* Missing from SDK: XORG_VERSION_CURRENT, */
    1, 0, 0,
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
#endif /* XFree86LOADER */
