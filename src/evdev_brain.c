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

/**
 * Indicate if evdev brain has been started yet.
 */
static Bool evdev_alive = FALSE;
/**
 * Pointer to the "evdev brain".
 * Note that this is a list, so evdev_pInfo->next will point to other devices
 * (albeit not evdev ones).
 */
static InputInfoPtr evdev_pInfo = NULL;
/**
 * All drivers that are currently active for one or more devices.
 */
static evdevDriverPtr evdev_drivers = NULL;
/**
 * Internal sequence numbering. Increased each time we evdevRescanDevices().
 */
static int evdev_seq;
static int evdev_inotify;

/**
 * Open the file descriptor for the given device in rw, nonblocking mode. 
 *
 * @return The FD on success or -1 otherwise.
 */
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
MatchAll (unsigned long *dev, unsigned long *match, int len)
{
    int i;

    for (i = 0; i < len; i++)
	if ((dev[i] & match[i]) != match[i])
	    return FALSE;

    return TRUE;
}

static Bool
MatchNot (unsigned long *dev, unsigned long *match, int len)
{
    int i;

    for (i = 0; i < len; i++)
	if ((dev[i] & match[i]))
	    return FALSE;

    return TRUE;
}

static Bool
MatchAny (unsigned long *dev, unsigned long *match, int len)
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

/**
 * Compare various fields of the given driver to the matching fields of the
 * info struct. Fields include but are not limited to name, phys string and
 * device.
 * If this function returns TRUE, the given driver is responsible for the
 * device with the given info.
 *
 * @param driver One of the evdev drivers.
 * @param info Information obtained using ioctls on the device file.
 *
 * @return TRUE on match, FALSE otherwise.
 */
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

/**
 * Compare various fields of the device with the given info.
 * If this function returns true, the device is identical to the device the
 * information was obtained from.
 *
 * @param device The device using some driver.
 * @param info Information obtained using ioctls on the device file.
 *
 * @return TRUE on match, FALSE otherwise.
 */
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

/**
 * Figure out if the device has already been initialised previously. If so,
 * switch it on. If not, create a new device and call it's callback to set it
 * up.
 *
 * @param driver The driver responsible for the device.
 * @param info Information obtained using ioctls on the device file.
 * 
 * @return TRUE if device was switched on or newly created. False if driver
 * and info are not matching.
 */
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


/**
 * Use various ioctls to get information about the given device file.
 * This information is used in evdevScanDevice, MatchDriver and MatchDevice.
 *
 * @param dev The device file to use.
 * @param info The struct to fill with the given info.
 */
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

/**
 * Scan all /dev/input/event* devices. If a driver is available for one, try
 * to get a device going on it (either creating a new one or switching it on).
 * After the scan, switch off all devices that didn't get switched on in the
 * previous run through the device files.
 */
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

/**
 * Start up the evdev brain device. 
 * Without the brain device, evdev won't be of a lot of use, so if this method
 * fails you just shot yourself in the foot. 
 * 
 * If the evdev brain was already started up, this method returns TRUE.
 *
 * Returns TRUE on success or FALSE otherwise.
 */
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

/**
 * Prepend a new driver to the global driver list and rescan all devices.
 * evdevStart() must be called before.
 * 
 * @param driver The new driver to be prepended
 *
 * @return TRUE on success or FALSE if the driver's callback wasn't setup.
 */
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

/**
 * Search all drivers for the given device. If it exists, remove the device.
 * Note that this does not remove the driver, so after calling this function
 * with a valid device, there may be a driver without a device floating
 * around.
 *
 * @param pEvdev The device to remove.
 */
void
evdevRemoveDevice (evdevDevicePtr pEvdev)
{
    evdevDriverPtr driver;
    evdevDevicePtr *device;

    for (driver = evdev_drivers; driver; driver = driver->next) {
        for (device = &driver->devices; *device; device = &(*device)->next) {
            if (*device == pEvdev) {
                *device = pEvdev->next;
                xf86DeleteInput(pEvdev->pInfo, 0);
                pEvdev->next = NULL;
                if (!driver->devices)
                return;
            }
        }
    }
}

/**
 * Obtain various information using ioctls on the given socket. This
 * information is used to determine if a device has axis, buttons or keys.
 *
 * @return TRUE on success or FALSE on error.
 */
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

