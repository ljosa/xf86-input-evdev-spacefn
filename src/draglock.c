/*
 * Copyright 1990,91 by Thomas Roell, Dinkelscherben, Germany.
 * Copyright 1993 by David Dawes <dawes@xfree86.org>
 * Copyright 2002 by SuSE Linux AG, Author: Egbert Eich
 * Copyright 1994-2002 by The XFree86 Project, Inc.
 * Copyright 2002 by Paul Elliott
 * (Ported from xf86-input-mouse, above copyrights taken from there)
 * Copyright © 2008 University of South Australia
 * Copyright 2008 by Chris Salch
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of the authors
 * not be used in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  The authors make no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied
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
 */

/* Draglock code */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xf86.h>
#include <xf86Xinput.h>

#include "evdev.h"

void EvdevDragLockLockButton(InputInfoPtr pInfo, unsigned int button);


/* Setup and configuration code */
void
EvdevDragLockInit(InputInfoPtr pInfo)
{
    EvdevPtr pEvdev = (EvdevPtr)pInfo->private;
    char *option_string = NULL;
    int meta_button = 0;
    int lock_button = 0;
    char *next_num = NULL;
    char *end_str = NULL;
    BOOL pairs = FALSE;

    option_string = xf86CheckStrOption(pInfo->options, "DragLockButtons",NULL);

    if (!option_string)
        return;

    next_num = option_string;

    /* Loop until we hit the end of our option string */
    while (next_num != NULL) {
        lock_button = 0;
        meta_button = strtol(next_num, &end_str, 10);

        /* check to see if we found anything */
        if (next_num != end_str) {
            /* setup for the next number */
            next_num = end_str;
        } else {
            /* we have nothing more to parse, drop out of the loop */
            next_num = NULL;
        }

        /* Check for a button to lock if we have a meta button */
        if (meta_button != 0 && next_num != NULL ) {
            lock_button = strtol(next_num, &end_str, 10);

            /* check to see if we found anything */
            if (next_num != end_str) {
                /* setup for the next number */
                next_num = end_str;
            } else {
                /* we have nothing more to parse, drop out of the loop */
                next_num = NULL;
            }
        }

        /* Ok, let the user know what we found on this look */
        if (meta_button != 0) {
            if (lock_button == 0) {
                if (!pairs) {
                    /* We only have a meta button */
                    pEvdev->dragLock.meta = meta_button;

                    xf86Msg(X_CONFIG, "%s: DragLockButtons : "
                            "%i as meta\n",
                            pInfo->name, meta_button);
                } else {
                    xf86Msg(X_ERROR, "%s: DragLockButtons : "
                            "Incomplete pair specifying button pairs %s\n",
                            pInfo->name, option_string);
                }
            } else {

                /* Do bounds checking to make sure we don't crash */
                if ((meta_button <= EVDEV_MAXBUTTONS) && (meta_button >= 0 ) &&
                    (lock_button <= EVDEV_MAXBUTTONS) && (lock_button >= 0)) {

                    xf86Msg(X_CONFIG, "%s: DragLockButtons : %i -> %i\n",
                            pInfo->name, meta_button, lock_button);

                    pEvdev->dragLock.lock_pair[meta_button - 1] = lock_button;
                    pairs=TRUE;
                } else {
                    /* Let the user know something was wrong
                       with this pair of buttons */
                    xf86Msg(X_CONFIG, "%s: DragLockButtons : "
                            "Invalid button pair %i -> %i\n",
                            pInfo->name, meta_button, lock_button);
                }
            }
        } else {
            xf86Msg(X_ERROR, "%s: Found DragLockButtons "
                    "with  invalid lock button string : '%s'\n",
                    pInfo->name, option_string);

            /* This should be the case anyhow, just make sure */
            next_num = NULL;
        }

        /* Check for end of string, to avoid annoying error */
        if (next_num != NULL && *next_num == '\0')
            next_num = NULL;
    }
}

/* Updates DragLock button state and firest button event messges */
void
EvdevDragLockLockButton(InputInfoPtr pInfo, unsigned int button)
{
    EvdevPtr pEvdev = (EvdevPtr)pInfo->private;
    BOOL state=0;

    /* update button state */
    state = pEvdev->dragLock.lock_state[button - 1] ? FALSE : TRUE;
    pEvdev->dragLock.lock_state[button - 1] = state;

    xf86PostButtonEvent(pInfo->dev, 0, button, state, 0, 0);
}

/* Filter button presses looking for either a meta button or the
 * control of a button pair.
 *
 * @param button button number (1 for left, 3 for right)
 * @param value TRUE if button press, FALSE if release
 *
 * @return TRUE if the event was swallowed here, FALSE otherwise.
 */
BOOL
EvdevDragLockFilterEvent(InputInfoPtr pInfo, unsigned int button, int value)
{
    EvdevPtr pEvdev = (EvdevPtr)pInfo->private;

    if (button == 0)
        return FALSE;

    /* Do we have a single meta key or
       several button pairings? */
    if (pEvdev->dragLock.meta != 0) {

        if (pEvdev->dragLock.meta == button) {

            /* setup up for button lock */
            if (value)
                pEvdev->dragLock.meta_state = TRUE;

            return TRUE;
        } else if (pEvdev->dragLock.meta_state) { /* waiting to lock */

            pEvdev->dragLock.meta_state = FALSE;

            EvdevDragLockLockButton(pInfo, button);

            return TRUE;
        }
    } else if (pEvdev->dragLock.lock_pair[button - 1] && value) {
        /* A meta button in a meta/lock pair was pressed */
        EvdevDragLockLockButton(pInfo, pEvdev->dragLock.lock_pair[button - 1]);
        return TRUE;
    }

    /* Eat events for buttons that are locked */
    if (pEvdev->dragLock.lock_state[button - 1])
        return TRUE;

    return FALSE;
}

