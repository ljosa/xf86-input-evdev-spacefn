/*
 * Copyright © 2004-2008 Red Hat, Inc.
 * Copyright © 2008 University of South Australia
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
 *	Kristian Høgsberg (krh@redhat.com)
 *	Adam Jackson (ajax@redhat.com)
 *	Peter Hutterer (peter@cs.unisa.edu.au)
 */

#ifndef EVDEV_H
#define EVDEV_H

#include <linux/input.h>

#include <xf86Xinput.h>
#include <xf86_OSproc.h>

#if defined(XKB)
/* XXX VERY WRONG.  this is a client side header. */
#include <X11/extensions/XKBstr.h>
#endif

typedef struct {
    int kernel24;
    int screen;
    int min_x, min_y, max_x, max_y;
    int abs_x, abs_y, old_x, old_y;
    int flags;
    int tool;

    /* XKB stuff has to be per-device rather than per-driver */
    int noXkb;
#ifdef XKB
    char                    *xkb_rules;
    char                    *xkb_model;
    char                    *xkb_layout;
    char                    *xkb_variant;
    char                    *xkb_options;
    XkbComponentNamesRec    xkbnames;
#endif
    /* Middle mouse button emulation */
    struct {
        BOOL                enabled;
        BOOL                pending;     /* timer waiting? */
        int                 buttonstate; /* phys. button state */
        int                 state;       /* state machine (see bt3emu.c) */
        Time                expires;     /* time of expiry */
        Time                timeout;
    } emulateMB;
} EvdevRec, *EvdevPtr;

/* Middle Button emulation */
int  EvdevMBEmuTimer(InputInfoPtr);
BOOL EvdevMBEmuFilterEvent(InputInfoPtr, int, BOOL);
void EvdevMBEmuWakeupHandler(pointer, int, pointer);
void EvdevMBEmuBlockHandler(pointer, struct timeval**, pointer);
void EvdevMBEmuPreInit(InputInfoPtr);
void EvdevMBEmuFinalize(InputInfoPtr);
void EvdevMBEmuEnable(InputInfoPtr, BOOL);

#endif
