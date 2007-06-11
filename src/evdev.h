/*
 * Copyright © 2006-2007 Zephaniah E. Hull
 * Copyright © 2004 Red Hat, Inc.
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
 * Authors:
 *   Zephaniah E. Hull (warp@aehallh.com)
 *   Kristian Høgsberg (krh@redhat.com)
 */

#ifndef __EVDEV_H
#define __EVDEV_H

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

/*
 * At the moment, ABS_MAX is larger then REL_MAX.
 * As they are the only two providors of axes, ABS_MAX is it.
 */
#define AXES_MAX	ABS_MAX


#define BTN_MAX	96

struct _evdevDevice;

/*
 * FIXME: The mode option here is a kludge.
 * It can be 0 (rel mode), 1 (abs mode), or -1 (input side has no clue).
 *
 * Worse, it arguably shouldn't even be the sender that decides here.
 * And only the Axes targets and sources care at all right now.
 */
typedef void (*evdev_map_func_f)(InputInfoPtr pInfo, int value, int mode, void *map_data);

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

#define EV_BTN_B_PRESENT   	(1<<0)

typedef struct {
    int		real_buttons;
    int		buttons;
    int		b_flags[BTN_MAX];
    void	*b_map_data[ABS_MAX];
    evdev_map_func_f b_map[BTN_MAX];
    void	(*callback[BTN_MAX])(InputInfoPtr pInfo, int button, int value);
} evdevBtnRec, *evdevBtnPtr;

#define EV_ABS_V_PRESENT	(1<<0)
#define EV_ABS_V_M_AUTO		(1<<1)
#define EV_ABS_V_M_REL		(1<<2)
#define EV_ABS_V_INVERT		(1<<3)
#define EV_ABS_V_RESET		(1<<4)
#define EV_ABS_V_USE_TOUCH	(1<<5)

#define EV_ABS_USE_TOUCH	(1<<0)
#define EV_ABS_TOUCH		(1<<1)
#define EV_ABS_UPDATED		(1<<2)

typedef struct {
    int		flags;
    int		axes;
    int		v[ABS_MAX];
    int		v_flags[ABS_MAX];
    void	*v_map_data[ABS_MAX];
    evdev_map_func_f v_map[ABS_MAX];
} evdevAbsRec, *evdevAbsPtr;

#define EV_REL_V_PRESENT	(1<<0)
#define EV_REL_V_INVERT		(1<<1)
#define EV_REL_UPDATED		(1<<0)

typedef struct {
    int		flags;
    int		v_flags[REL_MAX];
    int		v[REL_MAX];
    int		axes;
    void 	*v_map_data[REL_MAX];
    evdev_map_func_f v_map[REL_MAX];
} evdevRelRec, *evdevRelPtr;

#define EV_AXES_V_M_ABS		(1<<0)
#define EV_AXES_V_M_REL		(1<<1)
#define EV_AXES_V_PRESENT	(1<<2)
#define EV_AXES_V_UPDATED	(1<<3)

#define EV_AXES_V_M_MASK	(EV_AXES_V_M_ABS | EV_AXES_V_M_REL)

#define EV_AXES_UPDATED		(1<<0)

typedef struct {
    int		axes;
    int		flags;
    int		v_flags[AXES_MAX];
    int		v_min[AXES_MAX];
    int		v_max[AXES_MAX];
    int		v[AXES_MAX];
    int		rotation;
    float	rot_sin, rot_cos;
    int		x, y;
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


/*
 * Option handling stuff.
 */

typedef struct evdev_option_token_s {
    const char *str;
    struct evdev_option_token_s *chain;
    struct evdev_option_token_s *next;
} evdev_option_token_t;

typedef Bool (*evdev_parse_opt_func_f)(InputInfoPtr pInfo, const char *name, evdev_option_token_t *token, int *flags);
typedef Bool (*evdev_parse_map_func_f)(InputInfoPtr pInfo,
	const char *name,
	evdev_option_token_t *option,
	void **map_data, evdev_map_func_f *map_func);

evdev_option_token_t *EvdevTokenize (const char *option, const char *tokens);
void EvdevFreeTokens (evdev_option_token_t *token);
Bool EvdevParseMapToRelAxis (InputInfoPtr pInfo,
	const char *name,
	evdev_option_token_t *option,
	void **map_data, evdev_map_func_f *map_func);
Bool EvdevParseMapToAbsAxis (InputInfoPtr pInfo,
	const char *name,
	evdev_option_token_t *option,
	void **map_data, evdev_map_func_f *map_func);
Bool
EvdevParseMapToButton (InputInfoRec *pInfo,
	const char *name,
	evdev_option_token_t *option,
	void **map_data, evdev_map_func_f *map_func);
Bool
EvdevParseMapToButtons (InputInfoRec *pInfo,
	const char *name,
	evdev_option_token_t *option,
	void **map_data, evdev_map_func_f *map_func);

typedef struct {
    char *name;
    evdev_parse_map_func_f func;
} evdev_map_parsers_t;

extern evdev_map_parsers_t evdev_map_parsers[];
Bool EvdevParseMapOption (InputInfoRec *pInfo, char *option, char *def, void **map_data, evdev_map_func_f *map_func);

#endif	/* __EVDEV_H */
