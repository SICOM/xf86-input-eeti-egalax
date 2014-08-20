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

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

#include "eetiegalax.h"

#define MAXBUTTONS 3

#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) < 12
#error "XINPUT ABI 12 required."
#endif

static Atom prop_invert;
static Atom prop_calibration;
static Atom prop_swap;
static Atom prop_axis_label;
static Atom prop_btn_label;

/******************************************************************************
 * Function/Macro keys variables
 *****************************************************************************/

static int
openSerialDevice(InputInfoPtr pInfo) {
	const char *devname = xf86SetStrOption(pInfo->options, "Device", NULL);
	int fd;
	struct termios tio;

	if (!devname) {
		xf86Msg(X_ERROR, "openSerialDevice: No Device specified.\n");
		return -1;
	}

	fd = open(devname, O_RDWR | O_NOCTTY | O_NONBLOCK | O_EXCL);
	if (fd < 0)
		return -1;

	tcgetattr(fd, &tio);
	cfmakeraw(&tio);
	cfsetospeed(&tio, B9600);
	cfsetispeed(&tio, B9600);

	tcsetattr(fd, TCSANOW, &tio);

	return fd;
}

static int
get_bytes(InputInfoPtr pInfo, unsigned char *buf, int n) {
	int pos = 0;
	int err = 0;
	int waitret;
	struct timeval tv;
	fd_set set;

	FD_ZERO(&set);
	FD_SET(pInfo->fd, &set);
	tv.tv_sec = 0;
	tv.tv_usec = 50000;
	waitret = select(pInfo->fd + 1, &set, NULL, NULL, &tv);
	if (waitret <= 0)
		return waitret;

	while (n) {
	again:
		FD_ZERO(&set);
		FD_SET(pInfo->fd, &set);
		tv.tv_sec = 0;
		tv.tv_usec = 25000;
		waitret = select(pInfo->fd + 1, &set, NULL, NULL, &tv);
		if (waitret <= 0)
			return waitret;
		err = read(pInfo->fd, &buf[pos], 1);
		if (err == 0)
			goto again;
		if (err == -1) {
			if (errno == EAGAIN)
				goto again;
			break;
		}
		pos++;
		n--;
	}
#if 0
	{
		char *debug_line = malloc(5 * pos + 1);
		int i;

		for (i = 0; i < pos; i++)
			sprintf(debug_line + 5 * i, "0x%02x ", buf[i]);
		debug_line[5 * pos] = '\0';
		xf86Msg(X_INFO, "%s: get_bytes read: %s\n", pInfo->name, debug_line);
		free(debug_line);
	}
#endif
	if (err == -1)
		return -1;
	else
		return pos;
}

static void
flush_serial(int fd)
{
	usleep(200000);
	tcflush(fd, TCIOFLUSH);
}

static void
PointerControlProc(DeviceIntPtr dev, PtrCtrl *ctrl)
{
	return;
}

static int
eetiegalaxReadPacket(InputInfoPtr pInfo)
{
	EETIeGalaxPrivatePtr priv = (EETIeGalaxPrivatePtr)(pInfo->private);
	unsigned char *buf = priv->packet;
	int len, rest_length;

	/* Read the packet's first 3 bytes */
	len = get_bytes(pInfo, buf, 3);
	if (len != 3) {
		xf86Msg(X_INFO, "%s: No response or partial packet from EETI device, flushing line\n", pInfo->name);
		flush_serial(pInfo->fd);
		return !Success;
	}
	if (buf[0] != 0x0a) {
		xf86Msg(X_WARNING, "%s: Invalid response: initial byte %02d, flushing line\n", pInfo->name, buf[0]);
		flush_serial(pInfo->fd);
		return !Success;
	}

	rest_length = buf[1] - 1;

	if (rest_length > 0) {
		len = get_bytes(pInfo, &buf[3], rest_length);
		if (len != rest_length) {
			xf86Msg(X_WARNING, "%s: Truncated packet from EETI device, flushing line\n", pInfo->name);
			flush_serial(pInfo->fd);
			return 0;
		}
	}

	priv->packet_size = rest_length + 3;

	return Success;
}

static void
eetiegalaxReadInput(InputInfoPtr pInfo)
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
	int invert_x, x, min_x, max_x;
	int invert_y, y, min_y, max_y;
	int pressed;

	priv->packet_size = 0;

	/*
	 * Read the first three bytes of the stream and decide what we have
	 */
	if (get_bytes(pInfo, buf, 3) != 3) {
		flush_serial(pInfo->fd);
		return;
	}

	if (buf[0] == 0x0a && buf[2] == '4') {
		rest_length = buf[1] - 1;

		/* Multitouch packet? */
		if (rest_length > 0 && get_bytes(pInfo, &buf[3], rest_length) != rest_length) {
			flush_serial(pInfo->fd);
			return;
		}
		priv->packet_size = buf[1] + 2;

		/* Ignore the multitouch packet for now... */
	} else if (((buf[0] & 0x80) == 0x80) && ((buf[1] & 0x80) == 0x00) && ((buf[2] & 0x80) == 0x00)) {
		/*
		 * The packat is 5 or 6 bytes in total, three of those were
		 * already read above. The first byte has bit 7 set,
		 * all others has bit 7 unset. The above check in this if ()
		 * branch is the most we can do at that point.
		 */
		has_pressure = (buf[0] & 0x40);
		has_player = (buf[0] & 0x20);
		rest_length = ((has_pressure || has_player) ? 3 : 2);

		if (get_bytes(pInfo, &buf[3], rest_length) != rest_length) {
			flush_serial(pInfo->fd);
			return;
		}

		/*
		 * Detect resolution: 14, 13, 12 or 11 bits.
		 * Treat the value as always 14 bits and shift left with the
		 * proper number of bits if needed.
		 */
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

#if 0
		xf86Msg(X_INFO, "%s: calibration min_x % 5d max_x % 5d min_y % 5d max_y % 5d\n", pInfo->name, priv->min_x, priv->max_x, priv->min_y, priv->max_y);
#endif

		if (priv->min_x > priv->max_x) {
			invert_x = 1;
			min_x = priv->max_x;
			max_x = priv->min_x;
		} else {
			invert_x = priv->invert_x;
			min_x = priv->min_x;
			max_x = priv->max_x;
		}

		if (priv->min_y > priv->max_y) {
			invert_y = 1;
			min_y = priv->max_y;
			max_y = priv->min_y;
		} else {
			invert_y = priv->invert_y;
			min_y = priv->min_y;
			max_y = priv->max_y;
		}

#if 0
		xf86Msg(X_INFO, "%s: raw      x: % 5d y: % 5d button %s\n", pInfo->name, x, y, (pressed ? "DOWN" : "UP"));
#endif
		if (invert_x)
			x = 0x3fff - x;
		if (invert_y)
			y = 0x3fff - y;

#if 0
		xf86Msg(X_INFO, "%s: inverted x: % 5d y: % 5d button %s\n", pInfo->name, x, y, (pressed ? "DOWN" : "UP"));
#endif

		x = xf86ScaleAxis(x, 16383, 0, max_x, min_x);
		y = xf86ScaleAxis(y, 16383, 0, max_y, min_y);

#if 0
		xf86Msg(X_INFO, "%s: scaled   x: % 5d y: % 5d button %s\n", pInfo->name, x, y, (pressed ? "DOWN" : "UP"));
#endif

		if ((priv->proximity == FALSE) && pressed) {
			priv->proximity = TRUE;
			xf86PostProximityEvent(pInfo->dev, 1, 0, 2, x, y);
		}

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

		/*
		 * the untouch should always come after the button release
		 */
		if ((priv->proximity == TRUE) && !pressed) {
			priv->proximity = FALSE;
			xf86PostProximityEvent (pInfo->dev, 0, 0, 2, x, y);
		}
	} else
		flush_serial(pInfo->fd);
}

static int
eetiegalaxControlProc(InputInfoPtr pInfo, xDeviceCtl * control)
{
#if 0
	xDeviceAbsCalibCtl *c = (xDeviceAbsCalibCtl *)control;
	EETIeGalaxPrivatePtr priv = (EETIeGalaxPrivatePtr)(pInfo->private);

	priv->min_x = c->min_x;
	priv->max_x = c->max_x;
	priv->min_y = c->min_y;
	priv->max_y = c->max_y;

	xf86Msg(X_INFO, "%s: calibration called\n", pInfo->name);
#endif

	return Success;
}

static int
eetiegalaxSetProperty(DeviceIntPtr device, Atom atom, XIPropertyValuePtr value, BOOL checkonly) {
	InputInfoPtr pInfo = device->public.devicePrivate;
	EETIeGalaxPrivatePtr priv = (EETIeGalaxPrivatePtr)pInfo->private;

	if (atom == prop_invert) {
		if (value->format != 8 || value->size != 2 || value->type != XA_INTEGER)
			return BadMatch;
		if (!checkonly) {
			BOOL *invert = value->data;

			priv->invert_x = invert[0];
			priv->invert_y = invert[1];

			xf86Msg(X_INFO, "%s: InvertX / InvertY set to (%d,%d)\n", pInfo->name, priv->invert_x, priv->invert_y);
		}
	} else if (atom == prop_calibration) {
		if (value->format != 32 || value->type != XA_INTEGER)
			return BadMatch;
		if (value->size != 4 && value->size != 0)
			return BadMatch;
		if (!checkonly) {
			if (value->size == 0) {
				priv->min_x = 0;
				priv->max_x = 16383;
				priv->min_y = 0;
				priv->max_y = 16383;
				xf86Msg(X_INFO, "%s: calibration removed\n", pInfo->name);
			} else if (value->size == 4) {
				int *calibration = value->data;

				priv->min_x = calibration[0];
				priv->max_x = calibration[1];
				priv->min_y = calibration[2];
				priv->max_y = calibration[3];

				/* re-set AxisValuator values? */

				xf86Msg(X_INFO, "%s: calibration set (%d,%d,%d,%d)\n", pInfo->name, priv->min_x, priv->max_x, priv->min_y, priv->max_y);
			}
		}
	} else if (atom == prop_swap) {
		if (value->format != 8 || value->type != XA_INTEGER || value->size != 1)
			return BadMatch;
		if (!checkonly) {
			int *swap_xy = value->data;

			priv->swap_xy = *swap_xy;
			xf86Msg(X_INFO, "%s: SwapXY set to %d\n", pInfo->name, priv->swap_xy);
		}
	} else if (atom == prop_axis_label || atom == prop_btn_label)
		return BadAccess; /* Read-only properties */

	return Success;
}

static int
eetiegalaxSendCheckResponse(InputInfoPtr pInfo, const unsigned char *eeti_packet, const int size)
{
	EETIeGalaxPrivatePtr priv = (EETIeGalaxPrivatePtr)(pInfo->private);

	/* Send the initial packet and check that we have the device we want */
	if (write(pInfo->fd, eeti_packet, size) != size) {
		ErrorF("%s: error sending packet to device\n", pInfo->name);
		return !Success;
	}
	tcdrain(pInfo->fd);

	if (eetiegalaxReadPacket(pInfo) != Success) {
		ErrorF("%s: error reading response packet\n", pInfo->name);
		return !Success;
	}
#if 0
	xf86Msg(X_INFO, "%s: packet_size %d (should be at least %d) 1st char '%c' (expected '%c') 3rd char '%c' (expected '%c')\n",
			pInfo->name, priv->packet_size, size, priv->packet[0], eeti_packet[0], priv->packet[2], eeti_packet[2]);
#endif
	if ((priv->packet_size >= size) && (priv->packet[0] == eeti_packet[0]) && (priv->packet[2] == eeti_packet[2]))
		return Success;

	xf86Msg(X_ERROR, "%s: bad response from device, not an EETI eGalax serial device\n", pInfo->name);
	return !Success;
}

/*
 * eetiegalaxControlProc --
 *
 * called to change the state of a device.
 */
static int
eetiegalaxDeviceControl(DeviceIntPtr device, int what)
{
	InputInfoPtr pInfo = device->public.devicePrivate;
	EETIeGalaxPrivatePtr priv = (EETIeGalaxPrivatePtr)(pInfo->private);
	unsigned char map[MAXBUTTONS + 1] = {0, 1};
	unsigned char i;
	Atom btn_labels[MAXBUTTONS] = {0};
	Atom axes_labels[2] = {0};
	int rc;
	BOOL invert[2];
	int calibration[4];
	static const unsigned char eeti_alive[3] = { 0x0a, 0x01, 'A' };
	static const unsigned char eeti_fwver[3] = { 0x0a, 0x01, 'D' };
	static const unsigned char eeti_ctrlr[3] = { 0x0a, 0x01, 'E' };


	switch (what)
	{
	case DEVICE_INIT:
		device->public.on = FALSE;

		prop_axis_label = XIGetKnownProperty(AXIS_LABEL_PROP);
		axes_labels[0] = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_X);
		axes_labels[1] = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_Y);
		XIChangeDeviceProperty(device, prop_axis_label, XA_ATOM, 32, PropModeReplace, 2, axes_labels, FALSE);
		XISetDevicePropertyDeletable(device, prop_axis_label, FALSE);

		prop_btn_label = XIGetKnownProperty(BTN_LABEL_PROP);
		btn_labels[0] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_LEFT);
		btn_labels[1] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_MIDDLE);
		btn_labels[2] = XIGetKnownProperty(BTN_LABEL_PROP_BTN_RIGHT);
		XIChangeDeviceProperty(device, prop_btn_label, XA_ATOM, 32, PropModeReplace, MAXBUTTONS, btn_labels, FALSE);
		XISetDevicePropertyDeletable(device, prop_btn_label, FALSE);

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
							0,	/* min val */
							16383,	/* max val */
							1,	/* resolution */
							0,	/* min_res */
							1,	/* max_res */
							Absolute);
		InitValuatorAxisStruct(device,
							1,
							axes_labels[1],
							0,	/* min val */
							16383,	/* max val */
							1,	/* resolution */
							0,	/* min_res */
							1,	/* max_res */
							Absolute);
		/* allocate the motion history buffer if needed */
		xf86MotionHistoryAllocate(pInfo);

		if (InitProximityClassDeviceStruct(device) == FALSE) {
			ErrorF ("unable to allocate proximity class device\n");
			return !Success;
		}

		if (InitPtrFeedbackClassDeviceStruct(device, PointerControlProc) == FALSE) {
			ErrorF("unable to init pointer feedback class device\n");
			return !Success;
		}

		invert[0] = priv->invert_x;
		invert[1] = priv->invert_y;
		prop_invert = MakeAtom(EVDEV_PROP_INVERT_AXES, strlen(EVDEV_PROP_INVERT_AXES), TRUE);
		rc = XIChangeDeviceProperty(device, prop_invert, XA_INTEGER, 8, PropModeReplace, 2, invert, FALSE);
		if (rc != Success)
			return !Success;
		XISetDevicePropertyDeletable(device, prop_invert, FALSE);

		prop_calibration = MakeAtom(EVDEV_PROP_CALIBRATION, strlen(EVDEV_PROP_CALIBRATION), TRUE);
		calibration[0] = priv->min_x;
		calibration[1] = priv->max_x;
		calibration[2] = priv->min_y;
		calibration[3] = priv->max_y;
		rc = XIChangeDeviceProperty(device, prop_calibration, XA_INTEGER, 32, PropModeReplace, 4, calibration, FALSE);
		if (rc != Success)
			return !Success;
		XISetDevicePropertyDeletable(device, prop_calibration, FALSE);

		prop_swap = MakeAtom(EVDEV_PROP_SWAP_AXES, strlen(EVDEV_PROP_SWAP_AXES), TRUE);
		rc = XIChangeDeviceProperty(device, prop_swap, XA_INTEGER, 8, PropModeReplace, 1, &priv->swap_xy, FALSE);
		if (rc != Success)
			return !Success;
		XISetDevicePropertyDeletable(device, prop_swap, FALSE);

		XIRegisterPropertyHandler(device, eetiegalaxSetProperty, NULL, NULL);

		break;

	case DEVICE_ON:
		pInfo->fd = openSerialDevice(pInfo);
		if (pInfo->fd == -1) {
			ErrorF("%s: cannot open input device\n", pInfo->name);
			return !Success;
		}

		if (eetiegalaxSendCheckResponse(pInfo, eeti_alive, sizeof(eeti_alive)) == Success)
			xf86Msg(X_INFO, "%s: EETI eGalax serial device detected\n", pInfo->name);
		else
			goto error_close_device;

		if (eetiegalaxSendCheckResponse(pInfo, eeti_fwver, sizeof(eeti_fwver)) == Success) {
			priv->packet[priv->packet_size] = '\0';
			xf86Msg(X_INFO, "%s: EETI eGalax firmware version: %s\n", pInfo->name, &priv->packet[3]);
		} else
			goto error_close_device;

		if (eetiegalaxSendCheckResponse(pInfo, eeti_ctrlr, sizeof(eeti_ctrlr)) == Success) {
			priv->packet[priv->packet_size] = '\0';
			xf86Msg(X_INFO, "%s: EETI eGalax controller type: %s\n", pInfo->name, &priv->packet[3]);
		} else
			goto error_close_device;

		xf86FlushInput(pInfo->fd);
		AddEnabledDevice(pInfo->fd);
		device->public.on = TRUE;
		break;

	case DEVICE_OFF:
	case DEVICE_CLOSE:
		if (pInfo->fd != -1) {
			if (device->public.on == TRUE)
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

error_close_device:
	if (pInfo->fd != -1) {
		xf86CloseSerial(pInfo->fd);
		pInfo->fd = -1;
	}

	return !Success;
}

/*
 * eetiegalaxUninit --
 *
 * called when the driver is unloaded.
 */
static void
eetiegalaxUninit(InputDriverPtr drv, InputInfoPtr pInfo, int flags)
{
	eetiegalaxDeviceControl(pInfo->dev, DEVICE_OFF);
}

/*
 * eetiegalaxInit --
 *
 * called when the module subsection is found in XF86Config
 */
static int
eetiegalaxInit(InputDriverPtr drv, InputInfoPtr pInfo, int flags)
{
	EETIeGalaxPrivatePtr priv = calloc (1, sizeof (EETIeGalaxPrivateRec));

	if (!priv)
		return BadAlloc;

	priv->min_x = 0;
	priv->max_x = 16383;
	priv->min_y = 0;
	priv->max_y = 16383;
	priv->button_down = FALSE;
	priv->proximity = FALSE;
	priv->button_number = 1;
	priv->swap_xy = 0;
	priv->invert_x = 0;
	priv->invert_y = 0;

	/* Initialise the InputInfoRec. */
	pInfo->type_name = "EETIeGalax"; /* XI_TOUCHSCREEN */
	pInfo->device_control = eetiegalaxDeviceControl; /* DeviceControl */
	pInfo->read_input = eetiegalaxReadInput; /* ReadInput */
	pInfo->control_proc = eetiegalaxControlProc; /* ControlProc */
	pInfo->switch_mode = NULL;
	pInfo->private = priv;

	xf86OptionListReport(pInfo->options);

	/* Options set below in eetiegalaxDefOpts are used */
	pInfo->fd = openSerialDevice(pInfo);
	if (pInfo->fd == -1) {
		xf86Msg(X_ERROR, "%s: cannot open input device\n", pInfo->name);
		return !Success;
	}

	xf86CloseSerial(pInfo->fd);
	pInfo->fd = -1;

	priv->button_number = xf86SetIntOption(pInfo->options, "ButtonNumber", 1);
	if (priv->button_number < 0 || priv->button_number > 3) {
		priv->button_number = 1;
		xf86Msg(X_WARNING, "%s: ButtonNumber out of range (1-3), set to 1\n", pInfo->name);
	}

	priv->swap_xy = xf86SetBoolOption(pInfo->options, "SwapXY", 0);

	priv->invert_x = xf86SetBoolOption(pInfo->options, "InvertX", 0);
	priv->invert_y = xf86SetBoolOption(pInfo->options, "InvertY", 0);

	priv->min_x = xf86SetIntOption(pInfo->options, "MinX", 0);
	priv->max_x = xf86SetIntOption(pInfo->options, "MaxX", 16383);
	if (priv->min_x > priv->max_x) {
		int tmp = priv->min_x;
		priv->min_x = priv->max_x;
		priv->max_x = tmp;
		priv->invert_x = 1;
		xf86Msg(X_WARNING, "%s: InvertX automatically set to ON because of flipped MinX and MaxX\n", pInfo->name);
	}

	priv->min_y = xf86SetIntOption(pInfo->options, "MinY", 0);
	priv->max_y = xf86SetIntOption(pInfo->options, "MaxY", 16383);
	if (priv->min_y > priv->max_y) {
		int tmp = priv->min_y;
		priv->min_y = priv->max_y;
		priv->max_y = tmp;
		priv->invert_y = 1;
		xf86Msg(X_WARNING, "%s: InvertY automatically set to ON because of flipped MinY and MaxY\n", pInfo->name);
	}

	return Success;
}

_X_EXPORT InputDriverRec EETIEGALAX = {
	1,			/* driver version */
	"eetiegalax",		/* driver name */
	NULL,			/* identify */
	eetiegalaxInit,	/* pre-init */
	eetiegalaxUninit,	/* un-init */
	NULL,			/* module */
	NULL,			/* default options */
};

/*
 ***************************************************************************
 *
 * Dynamic loading functions
 *
 ***************************************************************************
 */
/*
 * eetiegalaxUnplug --
 *
 * called when the module subsection is found in XF86Config
 */
static void
eetiegalaxUnplug(pointer p)
{
}

/*
 * eetiegalaxPlug --
 *
 * called when the module subsection is found in XF86Config
 */
static pointer
eetiegalaxPlug(pointer module, pointer options, int *errmaj, int *errmin)
{
	xf86AddInputDriver(&EETIEGALAX, module, 0);
	return module;
}

static XF86ModuleVersionInfo eetiegalaxVersionRec =
{
	"eetiegalax",
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

_X_EXPORT XF86ModuleData eetiegalaxModuleData = {
	&eetiegalaxVersionRec,
	eetiegalaxPlug,
	eetiegalaxUnplug
};
