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
 *   Zephaniah E. Hull (warp@aehallh.com),
 *   Kristian Høgsberg (krh@redhat.com)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <X11/extensions/XIproto.h>

#include "evdev.h"

#include <xf86.h>

#include <xf86Module.h>
#include <mipointer.h>
#include <xf86_OSlib.h>


#include <xf86_OSproc.h>

static int EvdevProc(DeviceIntPtr device, int what);

/**
 * Obtain various information using ioctls on the given socket. This
 * information is used to determine if a device has axis, buttons or keys.
 *
 * @return TRUE on success or FALSE on error.
 */
static Bool
evdevGetBits (int fd, evdevBitsPtr bits)
{
#define get_bitmask(fd, which, where) \
    if (ioctl(fd, EVIOCGBIT(which, sizeof (where)), where) < 0) {			\
        xf86Msg(X_ERROR, "ioctl EVIOCGBIT %s failed: %s\n", #which, strerror(errno));	\
        return FALSE;									\
    }

    get_bitmask (fd, 0, bits->ev);
    get_bitmask (fd, EV_KEY, bits->key);
    get_bitmask (fd, EV_REL, bits->rel);
    get_bitmask (fd, EV_ABS, bits->abs);
    get_bitmask (fd, EV_MSC, bits->msc);
    get_bitmask (fd, EV_LED, bits->led);
    get_bitmask (fd, EV_SND, bits->snd);
    get_bitmask (fd, EV_FF, bits->ff);

#undef get_bitmask

    return TRUE;
}

/*
 * Evdev option handling stuff.
 *
 * We should probably move this all off to it's own file, but for now it lives
 * hereish.
 */

evdev_map_parsers_t evdev_map_parsers[] = {
    {
	.name = "RelAxis",
	.func = EvdevParseMapToRelAxis,
    },
    {
	.name = "AbsAxis",
	.func = EvdevParseMapToAbsAxis,
    },
    {
	.name = "Button",
	.func = EvdevParseMapToButton,
    },
    {
	.name = "Buttons",
	.func = EvdevParseMapToButtons,
    },
    {
	.name = NULL,
	.func = NULL,
    }
};

Bool
EvdevParseMapOption (InputInfoRec *pInfo, char *option, char *def, void **map_data, evdev_map_func_f *map_func)
{
    evdev_option_token_t *tokens;
    const char *s;
    int i;

    s = xf86SetStrOption(pInfo->options, option, def);
    tokens = EvdevTokenize (s, " ="); 
    if (tokens->next) {
	for (i = 0; evdev_map_parsers[i].name; i++) {
	    if (!strcasecmp (tokens->str, evdev_map_parsers[i].name)) {
		if (!evdev_map_parsers[i].func (pInfo, option, tokens->next, map_data, map_func)) {
		    xf86Msg (X_ERROR, "%s: Unable to parse '%s' as a map specifier.\n", pInfo->name, s);
		    EvdevFreeTokens (tokens);
		    return 0;
		}
		return 1;
	    }
	}

	if (!evdev_map_parsers[i].name)
	    xf86Msg (X_ERROR, "%s: Unable to find parser for '%s' as a map specifier.\n", pInfo->name, s);
    } else {
	xf86Msg (X_ERROR, "%s: Unable to parse '%s' as a map specifier string.\n", pInfo->name, s);
    }
    EvdevFreeTokens (tokens);
    return 0;
}

evdev_option_token_t *
EvdevTokenize (const char *option, const char *tokens)
{
    evdev_option_token_t *head = NULL, *token = NULL, *prev = NULL;
    const char *ctmp;
    const char *first;
    char *tmp = NULL;
    int len;

    first = strchr (option, tokens[0]);

    while (1) {
	if (first)
	    len = first - option;
	else {
	    len = strlen(option);
	    if (!len)
		break;
	}

	if (!len) {
	    option++;
	    first = strchr (option, tokens[0]);
	    continue;
	}

	token = calloc (1, sizeof(evdev_option_token_t));
	if (!head)
	    head = token;
	if (prev)
	    prev->next = token;

	prev = token;

	tmp = calloc(1, len + 1);
	strncpy (tmp, option, len);

	if (tokens[1]) {
	    ctmp = strchr (tmp, tokens[1]);
	    if (ctmp) {
		token->chain = EvdevTokenize (ctmp+1, tokens + 1);
	    } else
		token->str = tmp;
	} else
	    token->str = tmp;

	if (!first)
	    break;

	option = first + 1;
	first = strchr (option, tokens[0]);
    }

    return head;
}

void
EvdevFreeTokens (evdev_option_token_t *token)
{
    evdev_option_token_t *next;

    while (token) {
	if (token->chain)
	    EvdevFreeTokens (token->chain);
	free (token->str);
	next = token->next;
	free (token);
	token = next;
    }
}



static void
EvdevReadInput(InputInfoPtr pInfo)
{
    struct input_event ev;
    int len;

    while (xf86WaitForInput (pInfo->fd, 0) > 0) {
        len = read(pInfo->fd, &ev, sizeof(ev));
        if (len != sizeof(ev)) {
            /* The kernel promises that we always only read a complete
             * event, so len != sizeof ev is an error. */
            xf86Msg(X_ERROR, "Read error: %s (%d, %d != %zd)\n",
		    strerror(errno), errno, len, sizeof (ev));
	    if (len < 0)
            {
                xf86DisableDevice(pInfo->dev, TRUE);
                return;
            }
            break;
        }

        switch (ev.type) {
        case EV_REL:
	    EvdevAxesRelProcess (pInfo, &ev);
	    break;

        case EV_ABS:
	    EvdevAxesAbsProcess (pInfo, &ev);
            break;

        case EV_KEY:
	    if ((ev.code >= BTN_MISC) && (ev.code < KEY_OK))
		EvdevBtnProcess (pInfo, &ev);
	    else
		EvdevKeyProcess (pInfo, &ev);
	    break;

        case EV_SYN:
	    if (ev.code == SYN_REPORT) {
		EvdevAxesSynRep (pInfo);
		/* EvdevBtnSynRep (pInfo); */
		/* EvdevKeySynRep (pInfo); */
	    } else if (ev.code == SYN_CONFIG) {
		EvdevAxesSynCfg (pInfo);
		/* EvdevBtnSynCfg (pInfo); */
		/* EvdevKeySynCfg (pInfo); */
	    }
            break;
        }
    }
}

static int
EvdevProc(DeviceIntPtr device, int what)
{
    InputInfoPtr pInfo = device->public.devicePrivate;
    evdevDevicePtr pEvdev = pInfo->private;

    if (!pEvdev->device)
	return BadRequest;

    switch (what)
    {
    case DEVICE_INIT:
	if (pEvdev->state.axes)
	    EvdevAxesInit (device);
	if (pEvdev->state.btn)
	    EvdevBtnInit (device);
	if (pEvdev->state.key)
	    EvdevKeyInit (device);
	xf86Msg(X_INFO, "%s: Init\n", pInfo->name);
	break;

    case DEVICE_ON:
	xf86Msg(X_INFO, "%s: On\n", pInfo->name);
	if (device->public.on)
	    break;

	SYSCALL(pInfo->fd = open (pEvdev->device, O_RDWR | O_NONBLOCK));
	if (pInfo->fd == -1) {
	    xf86Msg(X_ERROR, "%s: cannot open input device.\n", pInfo->name);

	    if (pEvdev->device)
		xfree(pEvdev->device);
	    pEvdev->device = NULL;

	    return BadRequest;
	}

	if (pEvdev->state.can_grab)
	    if (ioctl(pInfo->fd, EVIOCGRAB, (void *)1))
		xf86Msg(X_ERROR, "%s: Unable to grab device (%s).\n", pInfo->name, strerror(errno));

	xf86FlushInput (pInfo->fd);

        xf86AddEnabledDevice(pInfo);

	device->public.on = TRUE;

	if (pEvdev->state.axes)
	    EvdevAxesOn (device);
	if (pEvdev->state.btn)
	    EvdevBtnOn (device);
	if (pEvdev->state.key)
	    EvdevKeyOn (device);
	break;

    case DEVICE_CLOSE:
    case DEVICE_OFF:
	xf86Msg(X_INFO, "%s: Off\n", pInfo->name);
	if (pInfo->fd != -1) {
	    if (pEvdev->state.can_grab)
		ioctl(pInfo->fd, EVIOCGRAB, (void *)0);

	    RemoveEnabledDevice (pInfo->fd);
	    xf86RemoveSIGIOHandler (pInfo->fd);
	    close (pInfo->fd);
	    pInfo->fd = -1;

	    if (pEvdev->state.axes)
		EvdevAxesOff (device);
	    if (pEvdev->state.btn)
		EvdevBtnOff (device);
	    if (pEvdev->state.key)
		EvdevKeyOff (device);
	}

	device->public.on = FALSE;
	break;
    }

    return Success;
}

static int
EvdevSwitchMode (ClientPtr client, DeviceIntPtr device, int mode)
{
    InputInfoPtr pInfo = device->public.devicePrivate;
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;

    switch (mode)
    {
	case Absolute:
	case Relative:
	    xf86Msg(X_INFO, "%s: Switching mode to %d.\n", pInfo->name, mode);
	    if (!state->abs)
		return !Success;
	    break;
	default:
	    return !Success;
    }

    return Success;
}

InputInfoPtr
EvdevPreInit(InputDriverPtr drv, IDevPtr dev, int flags)
{
    InputInfoPtr pInfo;
    evdevDevicePtr pEvdev;

    if (!(pInfo = xf86AllocateInput(drv, 0)))
	return NULL;

    pEvdev = Xcalloc (sizeof (evdevDeviceRec));
    if (!pEvdev) {
	pInfo->private = NULL;
	xf86DeleteInput (pInfo, 0);
	return NULL;
    }

    /* Initialise the InputInfoRec. */
    pInfo->name = xstrdup(dev->identifier);
    pInfo->flags = 0;
    pInfo->type_name = "UNKNOWN";
    pInfo->device_control = EvdevProc;
    pInfo->read_input = EvdevReadInput;
    pInfo->switch_mode = EvdevSwitchMode;
    pInfo->conf_idev = dev;

    pInfo->private = pEvdev;

    pEvdev->device = xf86CheckStrOption(dev->commonOptions, "path", NULL);
    if (!pEvdev->device)
        pEvdev->device = xf86CheckStrOption(dev->commonOptions, "Device", NULL);

    xf86CollectInputOptions(pInfo, NULL, NULL);
    xf86ProcessCommonOptions(pInfo, pInfo->options);

    SYSCALL(pInfo->fd = open (pEvdev->device, O_RDWR | O_NONBLOCK));
    if (pInfo->fd  == -1) {
	xf86Msg(X_ERROR, "%s: cannot open input pEvdev\n", pInfo->name);
	pInfo->private = NULL;
	xfree(pEvdev);
	xf86DeleteInput (pInfo, 0);
	return NULL;
    }

    if (!evdevGetBits (pInfo->fd, &pEvdev->bits)) {
	xf86Msg(X_ERROR, "%s: cannot load bits\n", pInfo->name);
	pInfo->private = NULL;
	close (pInfo->fd);
	xfree(pEvdev);
	xf86DeleteInput (pInfo, 0);
	return NULL;
    }

    if (ioctl(pInfo->fd, EVIOCGRAB, (void *)1)) {
	xf86Msg(X_INFO, "%s: Unable to grab pEvdev (%s).  Cowardly refusing to check use as keyboard.\n", pInfo->name, strerror(errno));
	pEvdev->state.can_grab = 0;
    } else {
	pEvdev->state.can_grab = 1;
        ioctl(pInfo->fd, EVIOCGRAB, (void *)0);
    }


    /* XXX: Note, the order of these is (maybe) still important. */
    EvdevBtnNew0 (pInfo);
    EvdevAxesNew0 (pInfo);

    EvdevAxesNew1 (pInfo);
    EvdevBtnNew1 (pInfo);

    if (pEvdev->state.can_grab)
	EvdevKeyNew (pInfo);

    close (pInfo->fd);
    pInfo->fd = -1;

    pInfo->flags |= XI86_OPEN_ON_INIT;
    if (!(pInfo->flags & XI86_CONFIGURED)) {
        xf86Msg(X_ERROR, "%s: Don't know how to use pEvdev.\n", pInfo->name);
	pInfo->private = NULL;
	close (pInfo->fd);
	xfree(pEvdev);
	xf86DeleteInput (pInfo, 0);
        return NULL;
    }

    return pInfo;
}

static void
EvdevUnInit (InputDriverRec *drv, InputInfoRec *pInfo, int flags)
{
    evdevDevicePtr pEvdev = pInfo->private;
    evdevStatePtr state = &pEvdev->state;

    if (pEvdev->device) {
	xfree (pEvdev->device);
	pEvdev->device = NULL;
    }

    if (state->btn) {
	xfree (state->btn);
	state->btn = NULL;
    }

    if (state->abs) {
	xfree (state->abs);
	state->abs = NULL;
    }

    if (state->rel) {
	xfree (state->rel);
	state->rel = NULL;
    }

    if (state->axes) {
	xfree (state->axes);
	state->axes = NULL;
    }

    if (state->key) {
	evdevKeyRec *key = state->key;

	if (key->xkb_rules) {
	    xfree (key->xkb_rules);
	    key->xkb_rules = NULL;
	}

	if (key->xkb_model) {
	    xfree (key->xkb_model);
	    key->xkb_model = NULL;
	}

	if (key->xkb_layout) {
	    xfree (key->xkb_layout);
	    key->xkb_layout = NULL;
	}

	if (key->xkb_variant) {
	    xfree (key->xkb_variant);
	    key->xkb_variant = NULL;
	}

	if (key->xkb_options) {
	    xfree (key->xkb_options);
	    key->xkb_options = NULL;
	}

	xfree (state->key);
	state->key = NULL;
    }


    xf86DeleteInput (pInfo, 0);
}


_X_EXPORT InputDriverRec EVDEV = {
    1,
    "evdev",
    NULL,
    EvdevPreInit,
    EvdevUnInit,
    NULL,
    0
};

static void
EvdevUnplug(pointer	p)
{
}

static pointer
EvdevPlug(pointer	module,
          pointer	options,
          int		*errmaj,
          int		*errmin)
{
    xf86AddInputDriver(&EVDEV, module, 0);
    return module;
}

static XF86ModuleVersionInfo EvdevVersionRec =
{
    "evdev",
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR, PACKAGE_VERSION_PATCHLEVEL,
    ABI_CLASS_XINPUT,
    ABI_XINPUT_VERSION,
    MOD_CLASS_XINPUT,
    {0, 0, 0, 0}
};

_X_EXPORT XF86ModuleData evdevModuleData =
{
    &EvdevVersionRec,
    EvdevPlug,
    EvdevUnplug
};
