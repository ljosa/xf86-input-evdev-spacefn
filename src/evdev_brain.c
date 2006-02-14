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

#define HAVE_WRAPPER_DECLS
#include "xf86_OSlib.h"

#include "evdev.h"

#include <xf86.h>

#ifndef SYSCALL
#define SYSCALL(call) while(((call) == -1) && (errno == EINTR))
#endif

static Bool evdev_alive = FALSE;
static InputInfoPtr evdev_pInfo = NULL;
static evdevDriverPtr evdev_drivers = NULL;
static int evdev_seq;

static int
glob_match(const char *pattern, const char *matchp)
{
    int i, j = 0, ret = 0;
    if (!(pattern && matchp))
	return (strlen(pattern) == strlen(matchp));

    for (i = 0; matchp[i]; i++) {
	if (pattern[j] == '\\')
	    j++;
	else if (pattern[j] == '*') {
	    if (pattern[j + 1]) {
		if (!glob_match(pattern+j+1,matchp+i))
		    return 0;
	    } else
		return 0;
	    continue;
	} else if (pattern[j] == '?') {
	    j++;
	    continue;
	}

	if ((ret = (pattern[j] - matchp[i])))
	    return ret;

	j++;
    }
    if (!pattern[j] || ((pattern[j] == '*') && !pattern[j + 1]))
	return 0;
    else
	return 1;
}


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

static void
evdevRescanDevices (InputInfoPtr pInfo)
{
    char dev[20];
    char name[256], phys[256];
    int fd, i;
    int	old_seq = evdev_seq;
    evdevDriverPtr driver;
    evdevDevicePtr device;
    Bool found;

    evdev_seq++;
    xf86Msg(X_INFO, "%s: Rescanning devices (%d).\n", pInfo->name, evdev_seq);

    for (i = 0; i < 32; i++) {
	snprintf(dev, sizeof(dev), "/dev/input/event%d", i);
	SYSCALL(fd = open (dev, O_RDWR | O_NONBLOCK));
	if (fd == -1)
	    continue;

	if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) == -1)
	    name[0] = '\0';
	if (ioctl(fd, EVIOCGPHYS(sizeof(phys)), phys) == -1)
	    phys[0] = '\0';

	for (driver = evdev_drivers; driver; driver = driver->next) {
	    if (driver->name && glob_match(driver->name, name))
		continue;
	    if (driver->phys && glob_match(driver->phys, phys))
		continue;
	    if (driver->device && glob_match(driver->device, dev))
		continue;

	    found = 0;
	    for (device = driver->devices; device; device = device->next) {
		xf86Msg(X_INFO, "%s: %s %d -> %s %d.\n", pInfo->name, name, evdev_seq, device->name, device->seen);
		if (!strcmp(device->name, name)) {
		    if (device->seen != old_seq) {
			device->device = xstrdup(dev);
			device->phys = xstrdup(phys);
			device->callback(device->pInfo->dev, DEVICE_ON);
		    }

		    device->seen = evdev_seq;
		    found = 1;
		    break;
		}
	    }

	    if (!found) {
		device = Xcalloc (sizeof (evdevDeviceRec));

		device->device = xstrdup(dev);
		device->name = xstrdup(name);
		device->phys = xstrdup(phys);
		device->seen = evdev_seq;
		device_add(driver, device);
		driver->callback(driver, device);
	    }

	    device->seen = evdev_seq;
	    break;
	}
	close (fd);
    }

    for (driver = evdev_drivers; driver; driver = driver->next)
	for (device = driver->devices; device; device = device->next)
	    if (device->seen == old_seq) {
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
    /*
     * XXX: Freezing the server for a moment is not really friendly.
     * But we need to wait until udev has actually created the device.
     */
    usleep (500000);
    evdevRescanDevices (pInfo);
}

static int
evdevControl(DeviceIntPtr pPointer, int what)
{
    InputInfoPtr pInfo;

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
	pInfo->fd = open ("/proc/bus/usb/devices", O_RDONLY);
	if (pInfo->fd == -1) {
	    xf86Msg(X_ERROR, "%s: cannot open /proc/bus/usb/devices.\n", pInfo->name);
	    return BadRequest;
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
	    close (pInfo->fd);
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
    if (!(driver->name || driver->phys || driver->device))
	return FALSE;
    if (!driver->callback)
	return FALSE;

    driver->next = evdev_drivers;
    evdev_drivers = driver;

    evdevRescanDevices (evdev_pInfo);
    driver->configured = TRUE;
    return TRUE;
}
