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

#ifndef BITS_PER_LONG
#define BITS_PER_LONG		(sizeof(unsigned long) * 8)
#endif

#define NBITS(x)		((((x)-1)/BITS_PER_LONG)+1)
#define LONG(x)			((x)/BITS_PER_LONG)
#define MASK(x)			(1UL << ((x) & (BITS_PER_LONG - 1)))

#ifndef test_bit
#define test_bit(bit, array)	(!!(array[LONG(bit)] & MASK(bit)))
#endif
#ifndef set_bit
#define set_bit(bit, array)	(array[LONG(bit)] |= MASK(bit))
#endif
#ifndef clear_bit
#define clear_bit(bit, array)	(array[LONG(bit)] &= ~MASK(bit))
#endif


#include <X11/extensions/XKB.h>
#include <X11/extensions/XKBstr.h>


#define EVDEV_MAXBUTTONS	96

typedef struct {
    unsigned long	ev[NBITS(EV_MAX)];
    unsigned long	key[NBITS(KEY_MAX)];
    unsigned long	rel[NBITS(REL_MAX)];
    unsigned long	abs[NBITS(ABS_MAX)];
    unsigned long	msc[NBITS(MSC_MAX)];
    unsigned long	led[NBITS(LED_MAX)];
    unsigned long	snd[NBITS(SND_MAX)];
    unsigned long	ff[NBITS(FF_MAX)];
} evdevBitsRec, *evdevBitsPtr;

#define EV_BTN_IGNORE_X   	1
#define EV_BTN_IGNORE_EVDEV   	2
#define EV_BTN_IGNORE_MAP	(EV_BTN_IGNORE_X | EV_BTN_IGNORE_EVDEV)
typedef struct {
    int		real_buttons;
    int		buttons;
    CARD8	ignore[EVDEV_MAXBUTTONS];
    CARD8	map[EVDEV_MAXBUTTONS];
    void	(*callback[EVDEV_MAXBUTTONS])(InputInfoPtr pInfo, int button, int value);
} evdevBtnRec, *evdevBtnPtr;

typedef struct {
    int		axes;
    int		v[ABS_MAX];
    int		old_x, old_y;
    int		count;
    int		min[ABS_MAX];
    int		max[ABS_MAX];
    int		map[ABS_MAX];
    int		screen; /* Screen number for this device. */
    Bool	use_touch;
    Bool	touch;
    Bool	reset_x, reset_y;
} evdevAbsRec, *evdevAbsPtr;

typedef struct {
    int		axes;
    int		v[REL_MAX];
    int		count;
    int		map[REL_MAX];
    int		btnMap[REL_MAX][2];
} evdevRelRec, *evdevRelPtr;

typedef struct {
    int		axes;
    int		v[ABS_MAX];
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
    const char		*device;

    evdevBitsRec	bits;

    evdevStateRec	state;
} evdevDeviceRec, *evdevDevicePtr;

int EvdevBtnInit (DeviceIntPtr device);
int EvdevBtnOn (DeviceIntPtr device);
int EvdevBtnOff (DeviceIntPtr device);
int EvdevBtnNew0(InputInfoPtr pInfo);
int EvdevBtnNew1(InputInfoPtr pInfo);
void EvdevBtnProcess (InputInfoPtr pInfo, struct input_event *ev);
void EvdevBtnPostFakeClicks(InputInfoPtr pInfo, int button, int count);
int EvdevBtnFind (InputInfoPtr pInfo, const char *button);
int EvdevBtnExists (InputInfoPtr pInfo, int button);

int EvdevAxesInit (DeviceIntPtr device);
int EvdevAxesOn (DeviceIntPtr device);
int EvdevAxesOff (DeviceIntPtr device);
int EvdevAxesNew0(InputInfoPtr pInfo);
int EvdevAxesNew1(InputInfoPtr pInfo);
void EvdevAxesAbsProcess (InputInfoPtr pInfo, struct input_event *ev);
void EvdevAxesRelProcess (InputInfoPtr pInfo, struct input_event *ev);
void EvdevAxesSynRep (InputInfoPtr pInfo);
void EvdevAxesSynCfg (InputInfoPtr pInfo);

int EvdevKeyInit (DeviceIntPtr device);
int EvdevKeyNew (InputInfoPtr pInfo);
int EvdevKeyOn (DeviceIntPtr device);
int EvdevKeyOff (DeviceIntPtr device);
void EvdevKeyProcess (InputInfoPtr pInfo, struct input_event *ev);

#endif	/* LNX_EVDEV_H_ */
