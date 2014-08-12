/*
 * Copyright 2014 by Sicom Systems Inc.
 * Written by Zoltán Böszörményi <zboszormenyi@sicom.com>
 *
 * based on xf86-input-void:
 *
 * Copyright 1999 by Frederic Lepied, France. <Lepied@XFree86.org>
 *                                                                            
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is  hereby granted without fee, provided that
 * the  above copyright   notice appear  in   all  copies and  that both  that
 * copyright  notice   and   this  permission   notice  appear  in  supporting
 * documentation, and that   the  name of  Frederic   Lepied not  be  used  in
 * advertising or publicity pertaining to distribution of the software without
 * specific,  written      prior  permission.     Frederic  Lepied   makes  no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.                   
 *                                                                            
 * FREDERIC  LEPIED DISCLAIMS ALL   WARRANTIES WITH REGARD  TO  THIS SOFTWARE,
 * INCLUDING ALL IMPLIED   WARRANTIES OF MERCHANTABILITY  AND   FITNESS, IN NO
 * EVENT  SHALL FREDERIC  LEPIED BE   LIABLE   FOR ANY  SPECIAL, INDIRECT   OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA  OR PROFITS, WHETHER  IN  AN ACTION OF  CONTRACT,  NEGLIGENCE OR OTHER
 * TORTIOUS  ACTION, ARISING    OUT OF OR   IN  CONNECTION  WITH THE USE    OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 */

/*
 * This device can be used as a core pointer or as a core keyboard.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <misc.h>
#include <xf86.h>
#define NEED_XF86_TYPES 1
#if !defined(DGUX)
#include <xisb.h>
#endif
#include <xf86_OSproc.h>
#include <xf86Xinput.h>
#include <exevents.h>		/* Needed for InitValuator/Proximity stuff */
#include <X11/keysym.h>
#include <mipointer.h>

#include <xf86Module.h>

#include <X11/Xatom.h>
#include <xserver-properties.h>

#include "eeti-egalax.h"

#define MAXBUTTONS 1

#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) < 12
#error "XINPUT ABI 12 required."
#endif
/******************************************************************************
 * Function/Macro keys variables
 *****************************************************************************/


static void
PointerControlProc(DeviceIntPtr dev, PtrCtrl *ctrl)
{
	return;
}

static void
xf86EETIeGalaxReadInput(InputInfoPtr pInfo)
{
}

/*
 * The ControlProc function may need to be tailored for your device
 */
static int
xf86EETIeGalaxControlProc(InputInfoPtr pInfo, xDeviceCtl * control)
{
	xDeviceAbsCalibCtl *c = (xDeviceAbsCalibCtl *) control;
	EETIeGalaxPrivatePtr priv = (EETIeGalaxPrivatePtr) (pInfo->private);

	priv->min_x = c->min_x;
	priv->max_x = c->max_x;
	priv->min_y = c->min_y;
	priv->max_y = c->max_y;

	return Success;
}

/*
 * xf86EETIeGalaxControlProc --
 *
 * called to change the state of a device.
 */
static int
xf86EETIeGalaxDeviceControl(DeviceIntPtr device, int what)
{
	InputInfoPtr pInfo = device->public.devicePrivate;
	EETIeGalaxPrivatePtr priv = (EETIeGalaxPrivatePtr) (pInfo->private);
	unsigned char map[MAXBUTTONS + 1] = {0, 1};
	unsigned char i;
	Bool result;
	Atom btn_labels[MAXBUTTONS] = {0};
	Atom axes_labels[2] = {0};

	axes_labels[0] = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_X);
	axes_labels[1] = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_Y);

	btn_labels[0] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_LEFT);
	btn_labels[1] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_MIDDLE);
	btn_labels[2] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_RIGHT);

	switch (what)
	{
	case DEVICE_INIT:
		device->public.on = FALSE;

		for (i = 1; i <= MAXBUTTONS; i++)
			map[i] = i;
	
		if (InitButtonClassDeviceStruct(device, MAXBUTTONS, btn_labels, map) == FALSE) {
			ErrorF("Unable to allocate Button class device\n");
			return !Success;
		}

		if (InitValuatorClassDeviceStruct(device, 2, axes_labels, GetMotionHistorySize(), Absolute) == FALSE) {
			ErrorF ("Unable to allocate Valuator class device\n");
			return !Success;
		}

		InitValuatorAxisStruct(device,
							0,
							axes_labels[0],
							0, /* min val */1, /* max val */
							1, /* resolution */
							0, /* min_res */
							1, /* max_res */
							Absolute);
		InitValuatorAxisStruct(device,
							1,
							axes_labels[1],
							0, /* min val */1, /* max val */
							1, /* resolution */
							0, /* min_res */
							1, /* max_res */
							Absolute);
		/* allocate the motion history buffer if needed */
		xf86MotionHistoryAllocate(pInfo);

		if (InitPtrFeedbackClassDeviceStruct(device, PointerControlProc) == FALSE) {
			ErrorF("unable to init pointer feedback class device\n");
			return !Success;
		}
		break;

	case DEVICE_ON:
		device->public.on = TRUE;
		break;

	case DEVICE_OFF:
	case DEVICE_CLOSE:
		device->public.on = FALSE;
		break;

	default:
		return BadValue;
	}
	return Success;
}

/*
 * xf86EETIeGalaxUninit --
 *
 * called when the driver is unloaded.
 */
static void
xf86EETIeGalaxUninit(InputDriverPtr drv, InputInfoPtr pInfo, int flags)
{
	xf86EETIeGalaxDeviceControl(pInfo->dev, DEVICE_OFF);
}

/*
 * xf86EETIeGalaxInit --
 *
 * called when the module subsection is found in XF86Config
 */
static int
xf86EETIeGalaxInit(InputDriverPtr drv, InputInfoPtr pInfo, int flags)
{
	EETIeGalaxPrivatePtr priv = calloc (1, sizeof (EETIeGalaxPrivateRec));

	if (!priv)
		return BadAlloc;

	priv->min_x = 0;
	priv->max_x = 16383;	/* Max 14 bit resolution reported */
	priv->min_y = 0;
	priv->max_y = 16383;	/* ditto */

	priv->button_number = 1;
	priv->swap_xy = 0;
	priv->invert_x = 0;
	priv->invert_y = 0;

	/* Initialise the InputInfoRec. */
	pInfo->type_name = "EETIeGalax"; /* XI_TOUCHSCREEN */
	pInfo->device_control = xf86EETIeGalaxDeviceControl; /* DeviceControl */
	pInfo->read_input = xf86EETIeGalaxReadInput; /* ReadInput */
	pInfo->control_proc = xf86EETIeGalaxControlProc; /* ControlProc */
	pInfo->switch_mode = NULL;
	pInfo->private = priv;

	xf86OptionListReport(pInfo->options);

	/* Options set below in xf86EETIeGalaxDefOpts are used */
	pInfo->fd = xf86OpenSerial(pInfo->options);
	if (pInfo->fd == -1) {
		xf86Msg(X_ERROR, "%s: cannot open input device\n", pInfo->name);
		return !Success;
	}

	xf86CloseSerial(pInfo->fd);
	pInfo->fd = -1;

	priv->button_number = xf86SetIntOption(pInfo->options, "ButtonNumber", 1);
	priv->swap_xy = xf86SetIntOption(pInfo->options, "SwapXY", 0);
	priv->invert_y = xf86SetIntOption(pInfo->options, "InvertY", 0);
	priv->invert_x = xf86SetIntOption(pInfo->options, "InvertX", 0);

	return Success;
}

static const char *xf86EETIeGalaxDefOpts[] = {
	"Device",	"/dev/ttyS3",
	"BaudRate",	"9600"
	"DataBits",     "8",
	"Parity",	"None",
	"StopBits",	"1",
	NULL
};

_X_EXPORT InputDriverRec EETI_EGALAX = {
	1,			/* driver version */
	"eeti_egalax",		/* driver name */
	NULL,			/* identify */
	xf86EETIeGalaxInit,	/* pre-init */
	xf86EETIeGalaxUninit,	/* un-init */
	NULL,			/* module */
	xf86EETIeGalaxDefOpts,	/* default options */
#ifdef XI86_DRV_CAP_SERVER_FD
	XI86_DRV_CAP_SERVER_FD
#endif
};

/*
 ***************************************************************************
 *
 * Dynamic loading functions
 *
 ***************************************************************************
 */
/*
 * xf86EETIeGalaxUnplug --
 *
 * called when the module subsection is found in XF86Config
 */
static void
xf86EETIeGalaxUnplug(pointer p)
{
}

/*
 * xf86EETIeGalaxPlug --
 *
 * called when the module subsection is found in XF86Config
 */
static pointer
xf86EETIeGalaxPlug(pointer module, pointer options, int *errmaj, int *errmin)
{
	xf86AddInputDriver(&EETI_EGALAX, module, 0);
	return module;
}

static XF86ModuleVersionInfo xf86EETIeGalaxVersionRec =
{
	"eeti-egalax",
	MODULEVENDORSTRING,
	MODINFOSTRING1,
	MODINFOSTRING2,
	XORG_VERSION_CURRENT,
	PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR, PACKAGE_VERSION_PATCHLEVEL,
	ABI_CLASS_XINPUT,
	ABI_XINPUT_VERSION,
	MOD_CLASS_XINPUT,
	{0, 0, 0, 0}		/* signature, to be patched into the file by */
				/* a tool */
};

_X_EXPORT XF86ModuleData eeti_egalaxModuleData = {
	&xf86EETIeGalaxVersionRec,
	xf86EETIeGalaxPlug,
	xf86EETIeGalaxUnplug
};
