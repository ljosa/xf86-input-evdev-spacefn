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

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <dlfcn.h>

#include "fakedev.h"

/* "public interfaces" */

void send_event(int fd, int type, int code, int value)
{
    struct input_event event;

    event.type  = type;
    event.code  = code;
    event.value = value;
    gettimeofday(&event.time, NULL);

    if (write(fd, &event, sizeof(event)) < sizeof(event))
        perror("Send event failed.");
}


void move(int fd, int x, int y)
{
    if (!x && !y)
        return;

    send_event(fd, EV_REL, REL_X, x);
    send_event(fd, EV_REL, REL_Y, y);
    send_event(fd, EV_SYN, SYN_REPORT, 0);
}

void absmove(int fd, int x, int y)
{
    send_event(fd, EV_ABS, ABS_X, x);
    send_event(fd, EV_ABS, ABS_Y, y);
    send_event(fd, EV_SYN, SYN_REPORT, 0);
}

void click(int fd, int btn, int down)
{
    send_event(fd, EV_KEY, btn, down);
    send_event(fd, EV_SYN, SYN_REPORT, 0);
}



/* end public interfaces */

static int fd   = -1;
static int stop = 0;

static void sighandler(int signum)
{
    printf("Stopping.\n");
    stop = 1;
}

static void init_signal(void)
{
    struct sigaction action;
    sigset_t mask;

    sigfillset(&mask);

    action.sa_handler = sighandler;
    action.sa_mask    = mask;
    action.sa_flags   = 0;

    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    sigprocmask(SIG_UNBLOCK, &mask, 0);
}


static int init_uinput(struct test_device* test_dev)
{
    struct uinput_user_dev dev;

    fd = open("/dev/input/uinput", O_RDWR);
    if (fd < 0)
        goto error;

    memset(&dev, 0, sizeof(dev));
    strcpy(dev.name, test_dev->name);
    dev.id.bustype = 0;
    dev.id.vendor  = 0x1F;
    dev.id.product = 0x1F;
    dev.id.version = 0;


    test_dev->setup(&dev, fd);

    if (write(fd, &dev, sizeof(dev)) < sizeof(dev))
        goto error;
    if (ioctl(fd, UI_DEV_CREATE, NULL) == -1) goto error;

    return 0;

error:
    fprintf(stderr, "Error: %s\n", strerror(errno));

    if (fd != -1)
        close(fd);

    return -1;
}

static void cleanup_uinput(void)
{
    if (fd == -1)
        return;

    ioctl(fd, UI_DEV_DESTROY, NULL);
    close(fd);
    fd = -1;
}


int main (int argc, char **argv)
{
    struct test_device *dev;
    void *dlhandle = NULL;
    struct test_device* (*get_device)(void);

    if (argc <= 1)
    {
        fprintf(stderr, "Usage: %s test_dev\n", argv[0]);
        return -1;
    }

    printf("Loading %s.\n", argv[1]);

    dlhandle = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (!dlhandle)
    {
        fprintf(stderr, "Error: %s\n", dlerror());
        return -1;
    }

    *(void**)(&get_device) = dlsym(dlhandle, "get_device");
    if (!get_device)
    {
        fprintf(stderr, "Error getting the symbol: %s.\n", dlerror());
        return -1;
    }

    dev = (*get_device)();

    if (init_uinput(dev) < 0) {
        fprintf(stderr, "Failed to initialize /dev/uinput. Exiting.\n");
        return -1;
    }

    init_signal();

    printf("Device created. Press CTRL+C to terminate.\n");
    while (!stop) {
        if (dev->run(fd))
            break;
    }

    cleanup_uinput();

    return 0;
}

