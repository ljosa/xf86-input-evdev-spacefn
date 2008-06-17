/*
 * Copyright 1990,91 by Thomas Roell, Dinkelscherben, Germany.
 * Copyright 1993 by David Dawes <dawes@xfree86.org>
 * Copyright 2002 by SuSE Linux AG, Author: Egbert Eich
 * Copyright 1994-2002 by The XFree86 Project, Inc.
 * Copyright 2002 by Paul Elliott
 * (Ported from xf86-input-mouse, above copyrights taken from there)
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
 */

/* Middle mouse button emulation code. */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xf86.h>

#include "evdev.h"

enum {
    MBEMU_DISABLED = 0,
    MBEMU_ENABLED,
    MBEMU_AUTO
};

/*
 * Lets create a simple finite-state machine for 3 button emulation:
 *
 * We track buttons 1 and 3 (left and right).  There are 11 states:
 *   0 ground           - initial state
 *   1 delayed left     - left pressed, waiting for right
 *   2 delayed right    - right pressed, waiting for left
 *   3 pressed middle   - right and left pressed, emulated middle sent
 *   4 pressed left     - left pressed and sent
 *   5 pressed right    - right pressed and sent
 *   6 released left    - left released after emulated middle
 *   7 released right   - right released after emulated middle
 *   8 repressed left   - left pressed after released left
 *   9 repressed right  - right pressed after released right
 *  10 pressed both     - both pressed, not emulating middle
 *
 * At each state, we need handlers for the following events
 *   0: no buttons down
 *   1: left button down
 *   2: right button down
 *   3: both buttons down
 *   4: emulate3Timeout passed without a button change
 * Note that button events are not deltas, they are the set of buttons being
 * pressed now.  It's possible (ie, mouse hardware does it) to go from (eg)
 * left down to right down without anything in between, so all cases must be
 * handled.
 *
 * a handler consists of three values:
 *   0: action1
 *   1: action2
 *   2: new emulation state
 *
 * action > 0: ButtonPress
 * action = 0: nothing
 * action < 0: ButtonRelease
 *
 * The comment preceeding each section is the current emulation state.
 * The comments to the right are of the form
 *      <button state> (<events>) -> <new emulation state>
 * which should be read as
 *      If the buttons are in <button state>, generate <events> then go to
 *      <new emulation state>.
 */
static signed char stateTab[11][5][3] = {
/* 0 ground */
  {
    {  0,  0,  0 },   /* nothing -> ground (no change) */
    {  0,  0,  1 },   /* left -> delayed left */
    {  0,  0,  2 },   /* right -> delayed right */
    {  2,  0,  3 },   /* left & right (middle press) -> pressed middle */
    {  0,  0, -1 }    /* timeout N/A */
  },
/* 1 delayed left */
  {
    {  1, -1,  0 },   /* nothing (left event) -> ground */
    {  0,  0,  1 },   /* left -> delayed left (no change) */
    {  1, -1,  2 },   /* right (left event) -> delayed right */
    {  2,  0,  3 },   /* left & right (middle press) -> pressed middle */
    {  1,  0,  4 },   /* timeout (left press) -> pressed left */
  },
/* 2 delayed right */
  {
    {  3, -3,  0 },   /* nothing (right event) -> ground */
    {  3, -3,  1 },   /* left (right event) -> delayed left (no change) */
    {  0,  0,  2 },   /* right -> delayed right (no change) */
    {  2,  0,  3 },   /* left & right (middle press) -> pressed middle */
    {  3,  0,  5 },   /* timeout (right press) -> pressed right */
  },
/* 3 pressed middle */
  {
    { -2,  0,  0 },   /* nothing (middle release) -> ground */
    {  0,  0,  7 },   /* left -> released right */
    {  0,  0,  6 },   /* right -> released left */
    {  0,  0,  3 },   /* left & right -> pressed middle (no change) */
    {  0,  0, -1 },   /* timeout N/A */
  },
/* 4 pressed left */
  {
    { -1,  0,  0 },   /* nothing (left release) -> ground */
    {  0,  0,  4 },   /* left -> pressed left (no change) */
    { -1,  0,  2 },   /* right (left release) -> delayed right */
    {  3,  0, 10 },   /* left & right (right press) -> pressed both */
    {  0,  0, -1 },   /* timeout N/A */
  },
/* 5 pressed right */
  {
    { -3,  0,  0 },   /* nothing (right release) -> ground */
    { -3,  0,  1 },   /* left (right release) -> delayed left */
    {  0,  0,  5 },   /* right -> pressed right (no change) */
    {  1,  0, 10 },   /* left & right (left press) -> pressed both */
    {  0,  0, -1 },   /* timeout N/A */
  },
/* 6 released left */
  {
    { -2,  0,  0 },   /* nothing (middle release) -> ground */
    { -2,  0,  1 },   /* left (middle release) -> delayed left */
    {  0,  0,  6 },   /* right -> released left (no change) */
    {  1,  0,  8 },   /* left & right (left press) -> repressed left */
    {  0,  0, -1 },   /* timeout N/A */
  },
/* 7 released right */
  {
    { -2,  0,  0 },   /* nothing (middle release) -> ground */
    {  0,  0,  7 },   /* left -> released right (no change) */
    { -2,  0,  2 },   /* right (middle release) -> delayed right */
    {  3,  0,  9 },   /* left & right (right press) -> repressed right */
    {  0,  0, -1 },   /* timeout N/A */
  },
/* 8 repressed left */
  {
    { -2, -1,  0 },   /* nothing (middle release, left release) -> ground */
    { -2,  0,  4 },   /* left (middle release) -> pressed left */
    { -1,  0,  6 },   /* right (left release) -> released left */
    {  0,  0,  8 },   /* left & right -> repressed left (no change) */
    {  0,  0, -1 },   /* timeout N/A */
  },
/* 9 repressed right */
  {
    { -2, -3,  0 },   /* nothing (middle release, right release) -> ground */
    { -3,  0,  7 },   /* left (right release) -> released right */
    { -2,  0,  5 },   /* right (middle release) -> pressed right */
    {  0,  0,  9 },   /* left & right -> repressed right (no change) */
    {  0,  0, -1 },   /* timeout N/A */
  },
/* 10 pressed both */
  {
    { -1, -3,  0 },   /* nothing (left release, right release) -> ground */
    { -3,  0,  4 },   /* left (right release) -> pressed left */
    { -1,  0,  5 },   /* right (left release) -> pressed right */
    {  0,  0, 10 },   /* left & right -> pressed both (no change) */
    {  0,  0, -1 },   /* timeout N/A */
  },
};


int
EvdevMBEmuTimer(InputInfoPtr pInfo)
{
    EvdevPtr pEvdev = pInfo->private;
    int	sigstate;
    int id;

    sigstate = xf86BlockSIGIO ();

    pEvdev->emulateMB.pending = FALSE;
    if ((id = stateTab[pEvdev->emulateMB.state][4][0]) != 0) {
        xf86PostButtonEvent(pInfo->dev, 0, abs(id), (id >= 0), 0, 0);
        pEvdev->emulateMB.state =
            stateTab[pEvdev->emulateMB.state][4][2];
    } else {
        ErrorF("Got unexpected buttonTimer in state %d\n",
                pEvdev->emulateMB.state);
    }

    xf86UnblockSIGIO (sigstate);
    return 0;
}


/**
 * Emulate a middle button on button press.
 *
 * @param code Evdev event code (BTN_LEFT or BTN_RIGHT)
 * @param press TRUE if press, FALSE if release.
 *
 * @return TRUE if event was swallowed by middle mouse button emulation, FALSE
 * otherwise.
 */
BOOL
EvdevMBEmuFilterEvent(InputInfoPtr pInfo, int code, BOOL press)
{
    EvdevPtr pEvdev = pInfo->private;
    int id;
    int *btstate;
    int ret = FALSE;

    if (!pEvdev->emulateMB.enabled)
        return ret;

    /* don't care about other buttons */
    if (code != BTN_LEFT && code != BTN_RIGHT)
        return ret;

    btstate = &pEvdev->emulateMB.buttonstate;
    if (press)
        *btstate |= (code == BTN_LEFT) ? 0x1 : 0x2;
    else
        *btstate &= (code == BTN_LEFT) ? ~0x1 : ~0x2;

    if ((id = stateTab[pEvdev->emulateMB.state][*btstate][0]) != 0)
    {
        xf86PostButtonEvent(pInfo->dev, 0, abs(id), (id >= 0), 0, 0);
        ret = TRUE;
    }
    if ((id = stateTab[pEvdev->emulateMB.state][*btstate][1]) != 0)
    {
        xf86PostButtonEvent(pInfo->dev, 0, abs(id), (id >= 0), 0, 0);
        ret = TRUE;
    }

    pEvdev->emulateMB.state =
        stateTab[pEvdev->emulateMB.state][*btstate][2];

    if (stateTab[pEvdev->emulateMB.state][4][0] != 0) {
        pEvdev->emulateMB.expires = GetTimeInMillis () + pEvdev->emulateMB.timeout;
        pEvdev->emulateMB.pending = TRUE;
        ret = TRUE;
    } else {
        pEvdev->emulateMB.pending = FALSE;
    }

    return ret;
}


void EvdevMBEmuWakeupHandler(pointer data,
                             int i,
                             pointer LastSelectMask)
{
    InputInfoPtr pInfo = (InputInfoPtr)data;
    EvdevPtr     pEvdev = (EvdevPtr)pInfo->private;
    int ms;

    if (pEvdev->emulateMB.pending)
    {
        ms = pEvdev->emulateMB.expires - GetTimeInMillis();
        if (ms <= 0)
            EvdevMBEmuTimer(pInfo);
    }
}

void EvdevMBEmuBlockHandler(pointer data,
                            struct timeval **waitTime,
                            pointer LastSelectMask)
{
    InputInfoPtr    pInfo = (InputInfoPtr) data;
    EvdevPtr        pEvdev= (EvdevPtr) pInfo->private;
    int             ms;

    if (pEvdev->emulateMB.pending)
    {
        ms = pEvdev->emulateMB.expires - GetTimeInMillis ();
        if (ms <= 0)
            ms = 0;
        AdjustWaitForDelay (waitTime, ms);
    }
}

void
EvdevMBEmuPreInit(InputInfoPtr pInfo)
{
    EvdevPtr pEvdev = (EvdevPtr)pInfo->private;
    pEvdev->emulateMB.enabled = MBEMU_AUTO;

    if (xf86FindOption(pInfo->options, "Emulate3Buttons"))
    {
        pEvdev->emulateMB.enabled = xf86SetBoolOption(pInfo->options,
                                                      "Emulate3Buttons",
                                                      MBEMU_ENABLED);
        xf86Msg(X_INFO, "%s: Forcing middle mouse button emulation.\n",
                        pInfo->name);
    }

    pEvdev->emulateMB.timeout = xf86SetIntOption(pInfo->options,
                                                 "Emulate3Timeout", 50);
    RegisterBlockAndWakeupHandlers (EvdevMBEmuBlockHandler,
                                    EvdevMBEmuWakeupHandler,
                                    (pointer)pInfo);

}

void
EvdevMBEmuFinalize(InputInfoPtr pInfo)
{
    RemoveBlockAndWakeupHandlers (EvdevMBEmuBlockHandler,
                                  EvdevMBEmuWakeupHandler,
                                  (pointer)pInfo);

}

/* Enable/disable middle mouse button emulation. */
void
EvdevMBEmuEnable(InputInfoPtr pInfo, BOOL enable)
{
    EvdevPtr pEvdev = (EvdevPtr)pInfo->private;
    if (pEvdev->emulateMB.enabled == MBEMU_AUTO)
        pEvdev->emulateMB.enabled = enable;
}
