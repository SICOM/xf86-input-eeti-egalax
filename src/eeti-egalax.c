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

static int
xf86EETIeGalaxReadPacket(int fd, EETIeGalaxPrivatePtr priv)
{
	unsigned char *buf = priv->packet;

	priv->packet_size = 0;

	if (xf86ReadSerial(fd, buf, 2) != 2)
		return !Success;

	if (xf86ReadSerial(fd, buf + 2, buf[1]) != buf[1])
		return !Success;

	priv->packet_size = buf[1] + 2;
	return Success;
}

static void
xf86EETIeGalaxReadInput(InputInfoPtr pInfo)
{
	EETIeGalaxPrivatePtr priv = (EETIeGalaxPrivatePtr)(pInfo->private);
	unsigned char *buf = priv->packet;
	int has_pressure;
	int has_player;
	int rest_length;
	int mask;
	int scale;
	int hi_byte;
	int lo_byte;
	int dir1;
	int dir2;
	int x;
	int y;
	int pressed;

	priv->packet_size = 0;

	/*
	 * Read the 1st byte of the stream and decide what we have
	 */
	if (xf86ReadSerial(pInfo->fd, buf, 1) != 1)
		return;

	switch (buf[0]) {
	case 0x0a:
		/* Multitouch packet? */
		switch (xf86WaitForInput(pInfo->fd, 200000) != 1) {
		case 0:
			//ErrorF("%s: timeout waiting for input from device (multitouch packet)\n", pInfo->name);
			xf86FlushInput(pInfo->fd);
			return;
		case 1:
			/* Read the length byte */
			if (xf86ReadSerial(pInfo->fd, &buf[1], 1) != 1) {
				xf86FlushInput(pInfo->fd);
				return;
			}
			/* Read the rest of the packet */
			if (xf86ReadSerial(pInfo->fd, &buf[2], buf[1]) != buf[1]) {
				xf86FlushInput(pInfo->fd);
				return;
			}

			/* Ignore the multitouch packet for now... */

			break;
		default:
			//ErrorF("%s: error waiting for input from device (multitouch packet)\n", pInfo->name);
			break;
		}
		break;
	default:
		/* The first byte should have bit 7 set */
		if (buf[0] & 0x80 != 0x80) {
			xf86FlushInput(pInfo->fd);
			return;
		}

		has_pressure = (buf[0] & 0x40);
		has_player = (buf[0] & 0x20);
		rest_length = ((has_pressure || has_player) ? 5 : 4);

		switch (buf[0] & 0x06) {
		case 0x00:
			mask = 0x07ff;
			scale = 3;
			break;
		case 0x02:
			mask = 0x0fff;
			scale = 2;
			break;
		case 0x04:
			mask = 0x1fff;
			scale = 1;
			break;
		default:
		case 0x06:
			mask = 0x3fff;
			scale = 0;
			break;
		}

		pressed = buf[0] & 0x01;

		if (xf86ReadSerial(pInfo->fd, &buf[1], rest_length) != rest_length) {
			xf86FlushInput(pInfo->fd);
			return;
		}

		hi_byte = ((int)buf[1]) << 7;
		lo_byte = ((int)buf[2]) & 0x7f;
		dir1 = (hi_byte | lo_byte) & mask;
		dir1 <<= scale;

		hi_byte = ((int)buf[3]) << 7;
		lo_byte = ((int)buf[4]) & 0x7f;
		dir2 = (hi_byte | lo_byte) & mask;
		dir2 <<= scale;

		/*
		 * Documentation says the A0...A13/B0...B13 bits
		 * only represent two directions. The actual
		 * hardware orientation (i.e. which direction is X
		 * and which is Y) can be implementation-dependent.
		 */
		if (priv->swap_xy) {
			x = dir1;
			y = dir2;
		} else {
			x = dir2;
			y = dir1;
		}
		if (priv->invert_x)
			x = 0x3fff - x;
		if (priv->invert_y)
			y = 0x3fff - y;

		/*
		 * Send events.
		 *
		 * We *must* generate a motion before a button change if pointer
		 * location has changed as DIX assumes this. This is why we always
		 * emit a motion, regardless of the kind of packet processed.
		 */
		xf86PostMotionEvent(pInfo->dev, TRUE, 0, 2, x, y);

		/*
		 * Emit a button press or release.
		 */
		if ((priv->button_down == FALSE) && pressed) {
			xf86PostButtonEvent (pInfo->dev, TRUE, priv->button_number, 1, 0, 2, x, y);
			priv->button_down = TRUE;
		}
		if ((priv->button_down == TRUE) && !pressed) {
			xf86PostButtonEvent (pInfo->dev, TRUE, priv->button_number, 0, 0, 2, x, y);
			priv->button_down = FALSE;
		}

		break;
	}
}

static int
xf86EETIeGalaxControlProc(InputInfoPtr pInfo, xDeviceCtl * control)
{
	xDeviceAbsCalibCtl *c = (xDeviceAbsCalibCtl *)control;
	EETIeGalaxPrivatePtr priv = (EETIeGalaxPrivatePtr)(pInfo->private);

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
	EETIeGalaxPrivatePtr priv = (EETIeGalaxPrivatePtr)(pInfo->private);
	unsigned char map[MAXBUTTONS + 1] = {0, 1};
	unsigned char i;
	Bool result;
	Atom btn_labels[MAXBUTTONS] = {0};
	Atom axes_labels[2] = {0};
	static const unsigned char eeti_alive[3] = { 0x0a, 0x01, 'A' };
	static const unsigned char eeti_fwver[3] = { 0x0a, 0x01, 'D' };
	static const unsigned char eeti_ctrlr[3] = { 0x0a, 0x01, 'E' };

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
		pInfo->fd = xf86OpenSerial(pInfo->options);
		if (pInfo->fd == -1) {
			ErrorF("%s: cannot open input device\n", pInfo->name);
			return !Success;
		}

		/* Send the initial and info packets and check that we have the device we want */
		if (xf86WriteSerial(pInfo->fd, eeti_alive, sizeof(eeti_alive)) != sizeof(eeti_alive)) {
			ErrorF("%s: error sending initial packet to device\n", pInfo->name);
			return !Success;
		}
		switch (xf86WaitForInput(pInfo->fd, 200000) != 1) {
		case 0:
			ErrorF("%s: timeout waiting for input from device\n", pInfo->name);
			return !Success;
		case 1:
			if (xf86EETIeGalaxReadPacket(pInfo->fd, priv) != Success) {
				ErrorF("%s: error reading input packet\n", pInfo->name);
				return !Success;
			}
			if (priv->packet_size == sizeof(eeti_alive) && memcmp(priv->packet, eeti_alive, sizeof(eeti_alive)) == 0) {
				xf86Msg(X_INFO, "%s: EETI eGalax serial device detected\n", pInfo->name);
				break;
			}
			ErrorF("%s: bad response from device, not an EETI eGalax serial device\n", pInfo->name);
			return !Success;
		default:
			ErrorF("%s: error waiting for input from device\n", pInfo->name);
			return !Success;
		}
		if (xf86EETIeGalaxReadPacket(pInfo->fd, priv) != Success) {
			ErrorF("%s: error reading input packet\n", pInfo->name);
		}

		if (xf86WriteSerial(pInfo->fd, eeti_fwver, sizeof(eeti_fwver)) != sizeof(eeti_fwver)) {
			ErrorF("%s: error sending firmware query packet to device\n", pInfo->name);
			return !Success;
		}
		switch (xf86WaitForInput(pInfo->fd, 200000) != 1) {
		case 0:
			ErrorF("%s: timeout waiting for input from device\n", pInfo->name);
			return !Success;
		case 1:
			if (xf86EETIeGalaxReadPacket(pInfo->fd, priv) != Success) {
				ErrorF("%s: error reading input packet\n", pInfo->name);
				return !Success;
			}
			if (priv->packet_size >= sizeof(eeti_fwver) && memcmp(priv->packet, eeti_fwver, sizeof(eeti_fwver)) == 0) {
				priv->packet[2 + priv->packet[1]] = '\0';
				xf86Msg(X_INFO, "%s: EETI eGalax firmware version: %s\n", pInfo->name, &priv->packet[3]);
				break;
			}
			ErrorF("%s: bad response from device, not an EETI eGalax serial device\n", pInfo->name);
			return !Success;
		default:
			ErrorF("%s: error waiting for input from device\n", pInfo->name);
			return !Success;
		}

		if (xf86WriteSerial(pInfo->fd, eeti_ctrlr, sizeof(eeti_ctrlr)) != sizeof(eeti_ctrlr)) {
			ErrorF("%s: error sending controller type query packet to device\n", pInfo->name);
			return !Success;
		}
		switch (xf86WaitForInput(pInfo->fd, 200000) != 1) {
		case 0:
			ErrorF("%s: timeout waiting for input from device\n", pInfo->name);
			return !Success;
		case 1:
			if (xf86EETIeGalaxReadPacket(pInfo->fd, priv) != Success) {
				ErrorF("%s: error reading input packet\n", pInfo->name);
				return !Success;
			}
			if (priv->packet_size >= sizeof(eeti_ctrlr) && memcmp(priv->packet, eeti_ctrlr, sizeof(eeti_ctrlr)) == 0) {
				priv->packet[2 + priv->packet[1]] = '\0';
				xf86Msg(X_INFO, "%s: EETI eGalax controller type: %s\n", pInfo->name, &priv->packet[3]);
				break;
			}
			ErrorF("%s: bad response from device, not an EETI eGalax serial device\n", pInfo->name);
			return !Success;
		default:
			ErrorF("%s: error waiting for input from device\n", pInfo->name);
			return !Success;
		}

		xf86FlushInput(pInfo->fd);
		AddEnabledDevice(pInfo->fd);
		device->public.on = TRUE;
		break;

	case DEVICE_OFF:
	case DEVICE_CLOSE:
		if (pInfo->fd != -1) {
			RemoveEnabledDevice(pInfo->fd);
			xf86CloseSerial(pInfo->fd);
			pInfo->fd = -1;
		}

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
	priv->button_down = FALSE;
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
	priv->invert_y = xf86SetIntOption(pInfo->options, "InvertY", 1);
	priv->invert_x = xf86SetIntOption(pInfo->options, "InvertX", 1);

	return Success;
}

static const char *xf86EETIeGalaxDefOpts[] = {
	"Device",	"/dev/ttyS3",
	"BaudRate",	"9600"
	"DataBits",     "8",
	"Parity",	"None",
	"StopBits",	"1",
	"SwapXY",	"0",
	"InvertX"	"1",
	"InvertY",	"1",
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
