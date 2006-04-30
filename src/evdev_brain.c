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

#include "evdev.h"

#include "xf86_OSlib.h"

#include <xf86.h>
#include <fnmatch.h>
#include <sys/poll.h>

#include "inotify.h"
#include "inotify-syscalls.h"

#ifndef SYSCALL
#define SYSCALL(call) while(((call) == -1) && (errno == EINTR))
#endif

static Bool evdev_alive = FALSE;
static InputInfoPtr evdev_pInfo = NULL;
static evdevDriverPtr evdev_drivers = NULL;
static int evdev_seq;
static int evdev_inotify;

int
evdevGetFDForDevice (evdevDevicePtr device)
{
    int fd;

    if (!device)
	return -1;


    if (device->device) {
	SYSCALL(fd = open (device->device, O_RDWR | O_NONBLOCK));
	if (fd == -1)
	    xf86Msg(X_ERROR, "%s (%d): Open failed: %s\n", __FILE__, __LINE__, strerror(errno));
	return fd;
    } else
	return -1;
}

#define device_add(driver,device) do {					\
    device->next = driver->devices;					\
    driver->devices = device;						\
} while (0)

typedef struct {
    evdevBitsRec	bits;
    char		name[256];
    char		phys[256];
    char		dev[256];
    struct input_id	id;
} evdevDevInfoRec, *evdevDevInfoPtr;

static Bool
MatchAll (long *dev, long *match, int len)
{
    int i;

    for (i = 0; i < len; i++)
	if ((dev[i] & match[i]) != match[i])
	    return FALSE;

    return TRUE;
}

static Bool
MatchNot (long *dev, long *match, int len)
{
    int i;

    for (i = 0; i < len; i++)
	if ((dev[i] & match[i]))
	    return FALSE;

    return TRUE;
}

static Bool
MatchAny (long *dev, long *match, int len)
{
    int i, found = 0;

    for (i = 0; i < len; i++)
	if (match[i]) {
	    found = 1;
	    if ((dev[i] & match[i]))
		return TRUE;
	}

    if (found)
	return FALSE;
    else
	return TRUE;
}

static Bool
MatchDriver (evdevDriverPtr driver, evdevDevInfoPtr info)
{
    if (driver->name && fnmatch(driver->name, info->name, 0))
	return FALSE;
    if (driver->phys && fnmatch(driver->phys, info->phys, 0))
	return FALSE;
    if (driver->device && fnmatch(driver->device, info->dev, 0))
	return FALSE;

    if (driver->id.bustype && driver->id.bustype != info->id.bustype)
	return FALSE;
    if (driver->id.vendor && driver->id.vendor != info->id.vendor)
	return FALSE;
    if (driver->id.product && driver->id.product != info->id.product)
	return FALSE;
    if (driver->id.version && driver->id.version != info->id.version)
	return FALSE;

#define match(which)	\
    if (!MatchAll(info->bits.which, driver->all_bits.which,	\
		sizeof(driver->all_bits.which) /		\
		sizeof(driver->all_bits.which[0])))		\
	return FALSE;						\
    if (!MatchNot(info->bits.which, driver->not_bits.which,	\
		sizeof(driver->not_bits.which) /		\
		sizeof(driver->not_bits.which[0])))		\
	return FALSE;						\
    if (!MatchAny(info->bits.which, driver->any_bits.which,	\
		sizeof(driver->any_bits.which) /		\
		sizeof(driver->any_bits.which[0])))		\
	return FALSE;

    match(ev)
    match(key)
    match(rel)
    match(abs)
    match(msc)
    match(led)
    match(snd)
    match(ff)

#undef match

    return TRUE;
}

static Bool
MatchDevice (evdevDevicePtr device, evdevDevInfoPtr info)
{
    int i, len;

    if (device->id.bustype != info->id.bustype)
	return FALSE;
    if (device->id.vendor != info->id.vendor)
	return FALSE;
    if (device->id.product != info->id.product)
	return FALSE;
    if (device->id.version != info->id.version)
	return FALSE;

    if (strcmp(device->name, info->name))
	return FALSE;

    len = sizeof(info->bits.ev) / sizeof(info->bits.ev[0]);
    for (i = 0; i < len; i++)
	if (device->bits.ev[i] != info->bits.ev[i])
	    return FALSE;

    return TRUE;
}

static Bool
evdevScanDevice (evdevDriverPtr driver, evdevDevInfoPtr info)
{
    evdevDevicePtr device;
    int found;

    if (!MatchDriver (driver, info))
	return FALSE;

    found = 0;
    for (device = driver->devices; device; device = device->next) {
	if (MatchDevice (device, info)) {
	    if (device->seen != (evdev_seq - 1)) {
		device->device = xstrdup(info->dev);
		device->phys = xstrdup(info->phys);
		device->callback(device->pInfo->dev, DEVICE_ON);
	    }

	    device->seen = evdev_seq;

	    return TRUE;
	}
    }

    device = Xcalloc (sizeof (evdevDeviceRec));

    device->device = xstrdup(info->dev);
    device->name = xstrdup(info->name);
    device->phys = xstrdup(info->phys);
    device->id.bustype = info->id.bustype;
    device->id.vendor = info->id.vendor;
    device->id.product = info->id.product;
    device->id.version = info->id.version;
    device->seen = evdev_seq;
    device_add(driver, device);
    driver->callback(driver, device);

    return TRUE;
}


static Bool
FillDevInfo (char *dev, evdevDevInfoPtr info)
{
    int fd;

    SYSCALL(fd = open (dev, O_RDWR | O_NONBLOCK));
    if (fd == -1)
	return FALSE;

    if (ioctl(fd, EVIOCGNAME(sizeof(info->name)), info->name) == -1)
	info->name[0] = '\0';
    if (ioctl(fd, EVIOCGPHYS(sizeof(info->phys)), info->phys) == -1)
	info->phys[0] = '\0';
    if (ioctl(fd, EVIOCGID, &info->id) == -1) {
	close (fd);
	return FALSE;
    }
    if (!evdevGetBits (fd, &info->bits)) {
	close (fd);
	return FALSE;
    }

    strncpy (info->dev, dev, sizeof(info->dev));
    close (fd);

    return TRUE;
}

static void
evdevRescanDevices (InputInfoPtr pInfo)
{
    char dev[20];
    int i, j, found;
    evdevDriverPtr driver;
    evdevDevicePtr device;
    evdevDevInfoRec info;

    evdev_seq++;
    xf86Msg(X_INFO, "%s: Rescanning devices (%d).\n", pInfo->name, evdev_seq);

    for (i = 0; i < 32; i++) {
	snprintf(dev, sizeof(dev), "/dev/input/event%d", i);

	if (!FillDevInfo (dev, &info))
	    continue;

	found = 0;

	for (j = 0; j <= 3 && !found; j++) {
	    for (driver = evdev_drivers; driver && !found; driver = driver->next) {
		if ((driver->pass == j) && (found = evdevScanDevice (driver, &info)))
		    break;
	    }
	}
    }

    for (driver = evdev_drivers; driver; driver = driver->next)
	for (device = driver->devices; device; device = device->next)
	    if (device->seen == (evdev_seq - 1)) {
		device->callback(device->pInfo->dev, DEVICE_OFF);

		if (device->device)
		    xfree(device->device);
		device->device = NULL;

		if (device->phys)
		    xfree(device->phys);
		device->phys = NULL;
	    }
}

static void
evdevReadInput (InputInfoPtr pInfo)
{
    int scan = 0, i, len;
    char buf[4096];
    struct inotify_event *event;

    if (evdev_inotify) {
	while ((len = read (pInfo->fd, buf, sizeof(buf))) >= 0) {
	    for (i = 0; i < len; i += sizeof (struct inotify_event) + event->len) {
		event = (struct inotify_event *) &buf[i];
		if (!event->len)
		    continue;
		if (event->mask & IN_ISDIR)
		    continue;
		if (strncmp("event", event->name, 5))
		    continue;
		scan = 1;
	    }
	}

	if (scan)
	    evdevRescanDevices (pInfo);
    } else {
	/*
	 * XXX: Freezing the server for a moment is not really friendly.
	 * But we need to wait until udev has actually created the device.
	 */
	usleep (500000);
	evdevRescanDevices (pInfo);
    }
}

static int
evdevControl(DeviceIntPtr pPointer, int what)
{
    InputInfoPtr pInfo;
    int i, flags;

    pInfo = pPointer->public.devicePrivate;

    switch (what) {
    case DEVICE_INIT:
	pPointer->public.on = FALSE;
	break;

    case DEVICE_ON:
	/*
	 * XXX: We do /proc/bus/usb/devices instead of /proc/bus/input/devices
	 * because the only hotplug input devices at the moment are USB...
	 * And because the latter is useless to poll/select against.
	 * FIXME: Get a patch in the kernel which fixes the latter.
	 */
	evdev_inotify = 1;
	SYSCALL(pInfo->fd = inotify_init());
	if (pInfo->fd < 0) {
	    xf86Msg(X_ERROR, "%s: Unable to initialize inotify, using fallback. (errno: %d)\n", pInfo->name, errno);
	    evdev_inotify = 0;
	}
	SYSCALL (i = inotify_add_watch (pInfo->fd, "/dev/input/", IN_CREATE | IN_DELETE));
	if (i < 0) {
	    xf86Msg(X_ERROR, "%s: Unable to initialize inotify, using fallback. (errno: %d)\n", pInfo->name, errno);
	    evdev_inotify = 0;
	    SYSCALL (close (pInfo->fd));
	    pInfo->fd = -1;
	}
	if ((flags = fcntl(pInfo->fd, F_GETFL)) < 0) {
	    xf86Msg(X_ERROR, "%s: Unable to NONBLOCK inotify, using fallback. "
		    "(errno: %d)\n", pInfo->name, errno);
	    evdev_inotify = 0;
	    SYSCALL (close (pInfo->fd));
	    pInfo->fd = -1;
	} else if (fcntl(pInfo->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
	    xf86Msg(X_ERROR, "%s: Unable to NONBLOCK inotify, using fallback. "
		    "(errno: %d)\n", pInfo->name, errno);
	    evdev_inotify = 0;
	    SYSCALL (close (pInfo->fd));
	    pInfo->fd = -1;
	}

	if (!evdev_inotify) {
	    SYSCALL (pInfo->fd = open ("/proc/bus/usb/devices", O_RDONLY));
	    if (pInfo->fd < 0) {
		xf86Msg(X_ERROR, "%s: cannot open /proc/bus/usb/devices.\n", pInfo->name);
		return BadRequest;
	    }
	}
	xf86FlushInput(pInfo->fd);
	AddEnabledDevice(pInfo->fd);
	pPointer->public.on = TRUE;
	evdevRescanDevices (pInfo);
	break;

    case DEVICE_OFF:
    case DEVICE_CLOSE:
	if (pInfo->fd != -1) {
	    RemoveEnabledDevice(pInfo->fd);
	    SYSCALL (close (pInfo->fd));
	    pInfo->fd = -1;
	}
	pPointer->public.on = FALSE;
	break;
    }
    return Success;
}

Bool
evdevStart (InputDriverPtr drv)
{
    InputInfoRec *pInfo;

    if (evdev_alive)
	return TRUE;

    if (!(pInfo = xf86AllocateInput(drv, 0)))
	return FALSE;

    evdev_alive = TRUE;

    pInfo->name = "evdev brain";
    pInfo->type_name = "evdev brain";
    pInfo->device_control = evdevControl;
    pInfo->read_input = evdevReadInput;
    pInfo->fd = -1;
    pInfo->flags = XI86_CONFIGURED | XI86_OPEN_ON_INIT;

    evdev_pInfo = pInfo;
    return TRUE;
}

Bool
evdevNewDriver (evdevDriverPtr driver)
{
    if (!evdev_alive)
	return FALSE;
    /* FIXME: Make this check valid given all the ways to look. */
#if 0
    if (!(driver->name || driver->phys || driver->device))
	return FALSE;
#endif
    if (!driver->callback)
	return FALSE;

    driver->next = evdev_drivers;
    evdev_drivers = driver;

    evdevRescanDevices (evdev_pInfo);
    driver->configured = TRUE;
    return TRUE;
}

Bool
evdevGetBits (int fd, evdevBitsPtr bits)
{
#define get_bitmask(fd, which, where) \
    if (ioctl(fd, EVIOCGBIT(which, sizeof (where)), where) < 0) {			\
        xf86Msg(X_ERROR, "ioctl EVIOCGBIT %s failed: %s\n", #which, strerror(errno));	\
        return FALSE;									\
    }

    get_bitmask (fd, 0, bits->ev);
    get_bitmask (fd, EV_KEY, bits->key);
    get_bitmask (fd, EV_REL, bits->rel);
    get_bitmask (fd, EV_ABS, bits->abs);
    get_bitmask (fd, EV_MSC, bits->msc);
    get_bitmask (fd, EV_LED, bits->led);
    get_bitmask (fd, EV_SND, bits->snd);
    get_bitmask (fd, EV_FF, bits->ff);

#undef get_bitmask

    return TRUE;
}

