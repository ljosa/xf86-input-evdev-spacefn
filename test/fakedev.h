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

#ifndef _EVDEV_TEST_H
#define _EVDEV_TEST_H
#include <linux/uinput.h>

struct test_device {
    char *name; /* device name */
    /**
     * Called to setup the device. Call ioctls to set your EVBITs, KEYBITs,
     * etc. here. Return 0 on success or non-zero to exit.
     */
    int (*setup)(struct uinput_user_dev* dev, int fd);

    /**
     * Called during each run of the main loop. Generate events by calling
     * move(), click(), etc.
     * Return 0 on success, or non-zero to stop the main loop.
     */
    int (*run)(int fd);
};

extern void move (int fd, int x, int y);
extern void absmove (int fd, int x, int y);
extern void click (int fd, int btn, int down);

#endif
