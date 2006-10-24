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

#include "evdev.h"

#include <xf86.h>

#include <xf86Module.h>
#include <mipointer.h>


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
    int len;

    while (xf86WaitForInput (pInfo->fd, 0) > 0) {
        len = read(pInfo->fd, &ev, sizeof(ev));
        if (len != sizeof(ev)) {
            /* The kernel promises that we always only read a complete
             * event, so len != sizeof ev is an error. */
            xf86Msg(X_ERROR, "Read error: %s (%d, %d != %ld)\n",
		    strerror(errno), errno, len, sizeof (ev));
	    if (len < 0) {
		evdevDevicePtr pEvdev = pInfo->private;
		pEvdev->callback(pEvdev->pInfo->dev, DEVICE_OFF);
		pEvdev->seen--;
	    }
            break;
        }

        switch (ev.type) {
        case EV_REL:
	    EvdevAxesRelProcess (pInfo, &ev);
	    break;

        case EV_ABS:
	    EvdevAxesAbsProcess (pInfo, &ev);
            break;

        case EV_KEY:
	    if ((ev.code >= BTN_MISC) && (ev.code < KEY_OK))
		EvdevBtnProcess (pInfo, &ev);
	    else
		EvdevKeyProcess (pInfo, &ev);
	    break;

        case EV_SYN:
	    if (ev.code == SYN_REPORT) {
		EvdevAxesSynRep (pInfo);
		/* EvdevBtnSynRep (pInfo); */
		/* EvdevKeySynRep (pInfo); */
	    } else if (ev.code == SYN_CONFIG) {
		EvdevAxesSynCfg (pInfo);
		/* EvdevBtnSynCfg (pInfo); */
		/* EvdevKeySynCfg (pInfo); */
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

    if (!pEvdev->device)
	return BadRequest;

    switch (what)
    {
    case DEVICE_INIT:
	if (pEvdev->state.axes)
	    EvdevAxesInit (device);
	if (pEvdev->state.btn)
	    EvdevBtnInit (device);
	if (pEvdev->state.key)
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

	if (pEvdev->state.axes)
	    EvdevAxesOn (device);
	if (pEvdev->state.btn)
	    EvdevBtnOn (device);
	if (pEvdev->state.key)
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
	    pInfo->fd = -1;

	    if (pEvdev->state.axes)
		EvdevAxesOff (device);
	    if (pEvdev->state.btn)
		EvdevBtnOff (device);
	    if (pEvdev->state.key)
		EvdevKeyOff (device);
	}

        if (what == DEVICE_CLOSE)
            evdevRemoveDevice(pEvdev);

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
	    xf86Msg(X_INFO, "%s: Switching mode to %d.\n", pInfo->name, mode);
	    if (state->abs)
		state->mode = mode;
	    else
		return !Success;
	    break;
#if 0
	case SendCoreEvents:
	case DontSendCoreEvents:
	    xf86XInputSetSendCoreEvents (pInfo, (mode == SendCoreEvents));
	    break;
#endif
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
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) == 0
    pInfo->motion_history_proc = xf86GetMotionEvents;
#endif
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

    if (!evdevGetBits (pInfo->fd, &device->bits)) {
	xf86Msg(X_ERROR, "%s: cannot load bits\n", pInfo->name);
	pInfo->private = NULL;
	close (pInfo->fd);
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


    /* XXX: Note, the order of these is (maybe) still important. */
    EvdevAxesNew0 (pInfo);
    EvdevBtnNew0 (pInfo);

    EvdevAxesNew1 (pInfo);
    EvdevBtnNew1 (pInfo);

    if (device->state.can_grab)
	EvdevKeyNew (pInfo);

    close (pInfo->fd);
    pInfo->fd = -1;

    pInfo->flags |= XI86_OPEN_ON_INIT;
    if (!(pInfo->flags & XI86_CONFIGURED)) {
        xf86Msg(X_ERROR, "%s: Don't know how to use device.\n", pInfo->name);
	pInfo->private = NULL;
	close (pInfo->fd);
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

static void
EvdevParseBits (char *in, unsigned long *out, int len)
{
    unsigned long v[2];
    int n, i, max_bits = len * BITS_PER_LONG;

    n = sscanf (in, "%lu-%lu", &v[0], &v[1]);
    if (!n)
	return;

    if (v[0] >= max_bits)
	return;

    if (n == 2) {
	if (v[1] >= max_bits)
	    v[1] = max_bits - 1;

	for (i = v[0]; i <= v[1]; i++)
	    set_bit (i, out);
    } else
	set_bit (v[0], out);
}

static void
EvdevParseBitOption (char *opt, unsigned long *all, unsigned long *not, unsigned long *any, int len)
{
    char *cur, *next;

    next = opt - 1;
    while (next) {
	cur = next + 1;
	if ((next = strchr(cur, ' ')))
	    *next = '\0';

	switch (cur[0]) {
	    case '+':
		EvdevParseBits (cur + 1, all, len);
		break;
	    case '-':
		EvdevParseBits (cur + 1, not, len);
		break;
	    case '~':
		EvdevParseBits (cur + 1, any, len);
		break;
	}
    }
}

static InputInfoPtr
EvdevCorePreInit(InputDriverPtr drv, IDevPtr dev, int flags)
{
    evdevDriverPtr pEvdev;
    char *opt, *tmp;

    if (!(pEvdev = Xcalloc(sizeof(*pEvdev))))
        return NULL;

    pEvdev->name = xf86CheckStrOption(dev->commonOptions, "Name", NULL);
    pEvdev->phys = xf86CheckStrOption(dev->commonOptions, "Phys", NULL);
    pEvdev->device = xf86CheckStrOption(dev->commonOptions, "Device", NULL);

#define bitoption(field)							\
    opt = xf86CheckStrOption(dev->commonOptions, #field "Bits", NULL);		\
    if (opt) {									\
	tmp = strdup(opt);							\
	EvdevParseBitOption (tmp, pEvdev->all_bits.field,			\
		pEvdev->not_bits.field,					\
		pEvdev->any_bits.field,					\
		sizeof(pEvdev->not_bits.field) / sizeof (unsigned long));	\
	free (tmp);								\
    }
    bitoption(ev);
    bitoption(key);
    bitoption(rel);
    bitoption(abs);
    bitoption(msc);
    bitoption(led);
    bitoption(snd);
    bitoption(ff);
#undef bitoption

    pEvdev->id.bustype = xf86CheckIntOption(dev->commonOptions, "bustype", 0);
    pEvdev->id.vendor = xf86CheckIntOption(dev->commonOptions, "vendor", 0);
    pEvdev->id.product = xf86CheckIntOption(dev->commonOptions, "product", 0);
    pEvdev->id.version = xf86CheckIntOption(dev->commonOptions, "version", 0);

    pEvdev->pass = xf86CheckIntOption(dev->commonOptions, "Pass", 0);
    if (pEvdev->pass > 3)
	pEvdev->pass = 3;
    else if (pEvdev->pass < 0)
	pEvdev->pass = 0;


    pEvdev->callback = EvdevNew;

    pEvdev->dev = dev;
    pEvdev->drv = drv;

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
    1, 1, 0,
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
