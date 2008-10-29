/*
 * Copyright © 2008 Red Hat, Inc.
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

/* Creates a device that has REL_X REL_Y BTN_0 BTN_1 BTN_2
 *
 * Moves the device around in a 10px square and clicks after each completed
 * circle.
 */

#include <stdio.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/uinput.h>

#include "fakedev.h"

int btn0_setup(struct uinput_user_dev *dev, int fd)
{
    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) == -1) goto error;
    if (ioctl(fd, UI_SET_EVBIT, EV_REL) == -1) goto error;
    if (ioctl(fd, UI_SET_EVBIT, EV_SYN) == -1) goto error;

    /* buttons */
    if (ioctl(fd, UI_SET_KEYBIT, BTN_0) == -1) goto error;
    if (ioctl(fd, UI_SET_KEYBIT, BTN_1) == -1) goto error;
    if (ioctl(fd, UI_SET_KEYBIT, BTN_2) == -1) goto error;

    /* axes */
    if (ioctl(fd, UI_SET_RELBIT, REL_X) == -1) goto error;
    if (ioctl(fd, UI_SET_RELBIT, REL_Y) == -1) goto error;

    return 0;
error:
    perror("ioctl failed.");
    return -1;
}

int btn0_run(int fd)
{
    move(fd, -10, 0);
    usleep(1000);
    move(fd, 0, -10);
    usleep(1000);
    move(fd, 10, 0);
    usleep(1000);
    move(fd, 0, 10);
    usleep(1000);
    click(fd, BTN_0, 1);
    usleep(1000);
    click(fd, BTN_0, 0);
    return 0;
}

struct test_device btn0_dev = {
    .name  = "BTN_0 test device",
    .setup = btn0_setup,
    .run   = btn0_run,
};


struct test_device* get_device()
{
    return &btn0_dev;
}
