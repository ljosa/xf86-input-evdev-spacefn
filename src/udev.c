/*
 * Copyright © 2011 Red Hat, Inc.
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
 * THE AUTHORS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:
 *	Peter Hutterer (peter.hutterer@redhat.com)
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "evdev.h"

#include <libudev.h>
#include <sys/types.h>
#include <sys/stat.h>

Bool
udev_device_is_virtual(const char* devicenode)
{
    struct udev *udev = NULL;
    struct udev_device *device = NULL;
    struct stat st;
    int rc = FALSE;
    const char *devpath;

    udev = udev_new();
    if (!udev)
        goto out;

    stat(devicenode, &st);
    device = udev_device_new_from_devnum(udev, 'c', st.st_rdev);

    if (!device)
        goto out;


    devpath = udev_device_get_devpath(device);
    if (!devpath)
        goto out;

    if (strstr(devpath, "LNXSYSTM"))
        rc = TRUE;

out:
    udev_device_unref(device);
    udev_unref(udev);
    return rc;
}
