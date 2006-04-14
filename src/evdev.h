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

#ifndef EVDEV_BRAIN_H_
#define EVDEV_BRAIN_H_

#define _XF86_ANSIC_H
#define XF86_LIBC_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <linux/input.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <xf86Xinput.h>

#define BITS_PER_LONG		(sizeof(long) * 8)
#define NBITS(x)		((((x)-1)/BITS_PER_LONG)+1)
#define OFF(x) 			((x)%BITS_PER_LONG)
#define LONG(x)			((x)/BITS_PER_LONG)
#define BIT(x)			(1UL<<((x)%BITS_PER_LONG))
#define test_bit(bit, array)    ((array[LONG(bit)] >> OFF(bit)) & 1)

/* 2.4 compatibility */
#ifndef EVIOCGSW

#include <sys/time.h>
#include <sys/ioctl.h>
#include <asm/types.h>
#include <asm/bitops.h>

#define EVIOCGSW(len)		_IOC(_IOC_READ, 'E', 0x1b, len)		/* get all switch states */

#define EV_SW			0x05
#endif

#ifndef EVIOCGRAB
#define EVIOCGRAB _IOW('E', 0x90, int)
#endif

#ifndef BTN_TASK
#define BTN_TASK 0x117
#endif

#ifndef EV_SYN
#define EV_SYN EV_RST
#endif
/* end compat */

#include <X11/extensions/XKB.h>
#include <X11/extensions/XKBstr.h>


/*
 * Switch events
 */

#define EV_SW_0		0x00
#define EV_SW_1		0x01
#define EV_SW_2		0x02
#define EV_SW_3		0x03
#define EV_SW_4		0x04
#define EV_SW_5		0x05
#define EV_SW_6		0x06
#define EV_SW_7		0x07
#define EV_SW_MAX	0x0f

#define EV_BUS_GSC		0x1A

#define EVDEV_MAXBUTTONS	96

typedef struct {
    long	ev[NBITS(EV_MAX)];
    long	key[NBITS(KEY_MAX)];
    long	rel[NBITS(REL_MAX)];
    long	abs[NBITS(ABS_MAX)];
    long	msc[NBITS(MSC_MAX)];
    long	led[NBITS(LED_MAX)];
    long	snd[NBITS(SND_MAX)];
    long	ff[NBITS(FF_MAX)];
} evdevBitsRec, *evdevBitsPtr;

typedef struct {
    int		real_buttons;
    int		buttons;
    CARD8	map[EVDEV_MAXBUTTONS];
    int		*state[EVDEV_MAXBUTTONS];
} evdevBtnRec, *evdevBtnPtr;

typedef struct {
    int		axes;
    int		n; /* Which abs_v is current, and which is previous. */
    int		v[2][ABS_MAX];
    int		count;
    int		min[ABS_MAX];
    int		max[ABS_MAX];
    int		map[ABS_MAX];
    int		scale[2];
    int		screen; /* Screen number for this device. */
} evdevAbsRec, *evdevAbsPtr;

typedef struct {
    int		axes;
    int		v[REL_MAX];
    int		count;
    int		map[REL_MAX];
} evdevRelRec, *evdevRelPtr;

typedef struct {
    int		axes;
    int		v[ABS_MAX];
    int		btnMap[ABS_MAX][2];
} evdevAxesRec, *evdevAxesPtr;

typedef struct {
    char	*xkb_rules;
    char	*xkb_model;
    char	*xkb_layout;
    char	*xkb_variant;
    char	*xkb_options;
    XkbComponentNamesRec xkbnames;
} evdevKeyRec, *evdevKeyPtr;

typedef struct _evdevState {
    Bool	can_grab;
    Bool	sync;
    int		mode;	/* Either Absolute or Relative. */

    evdevBtnPtr	btn;
    evdevAbsPtr	abs;
    evdevRelPtr	rel;
    evdevKeyPtr	key;
    evdevAxesPtr axes;
} evdevStateRec, *evdevStatePtr;

typedef struct _evdevDevice {
    const char		*name;
    const char		*phys;
    const char		*device;
    int			seen;

    InputInfoPtr	pInfo;
    int			(*callback)(DeviceIntPtr cb_data, int what);

    evdevBitsRec	bits;
    struct input_id	id;

    evdevStateRec	state;

    struct _evdevDevice *next;
} evdevDeviceRec, *evdevDevicePtr;

typedef struct _evdevDriver {
    const char		*name;
    const char		*phys;
    const char		*device;

    evdevBitsRec	all_bits;
    evdevBitsRec	not_bits;
    evdevBitsRec	any_bits;

    struct input_id	id;

    int			pass;

    InputDriverPtr	drv;
    IDevPtr		dev;
    Bool		(*callback)(struct _evdevDriver *driver, evdevDevicePtr device);
    evdevDevicePtr	devices;
    Bool		configured;

    struct _evdevDriver	*next;
} evdevDriverRec, *evdevDriverPtr;

int evdevGetFDForDevice (evdevDevicePtr driver);
Bool evdevStart (InputDriverPtr drv);
Bool evdevNewDriver (evdevDriverPtr driver);
Bool evdevGetBits (int fd, evdevBitsPtr bits);

int EvdevBtnInit (DeviceIntPtr device);
int EvdevBtnOn (DeviceIntPtr device);
int EvdevBtnOff (DeviceIntPtr device);
int EvdevBtnNew(InputInfoPtr pInfo);
void EvdevBtnProcess (InputInfoPtr pInfo, struct input_event *ev);
void EvdevBtnPostFakeClicks(InputInfoPtr pInfo, int button, int count);

int EvdevAxesInit (DeviceIntPtr device);
int EvdevAxesOn (DeviceIntPtr device);
int EvdevAxesOff (DeviceIntPtr device);
int EvdevAxesNew(InputInfoPtr pInfo);
void EvdevAxesAbsProcess (InputInfoPtr pInfo, struct input_event *ev);
void EvdevAxesRelProcess (InputInfoPtr pInfo, struct input_event *ev);
void EvdevAxesSyn (InputInfoPtr pInfo);

int EvdevKeyInit (DeviceIntPtr device);
int EvdevKeyNew (InputInfoPtr pInfo);
int EvdevKeyOn (DeviceIntPtr device);
int EvdevKeyOff (DeviceIntPtr device);
void EvdevKeyProcess (InputInfoPtr pInfo, struct input_event *ev);

#endif	/* LNX_EVDEV_H_ */
