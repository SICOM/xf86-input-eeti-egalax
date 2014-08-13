/*
 * Copyright 2014 by Sicom Systems Inc.
 * Written by Zoltán Böszörményi <zboszormenyi@sicom.com>
 *
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

#ifndef EETI_EGALAX_H
#define EETI_EGALAX_H

#define EETI_RESPONSE_SIZE	64

typedef struct _EETIeGalaxPrivateRec
{
	int	min_x;		/* Minimum x reported by calibration	*/
	int	max_x;		/* Maximum x 				*/
	int	min_y;		/* Minimum y reported by calibration	*/
	int	max_y;		/* Maximum y				*/
	Bool	button_down;	/* is the "button" currently down	*/
	int	swap_xy;	/* option "SwapXY"			*/
	int	invert_y;	/* option "InvertY"			*/
	int	invert_x;	/* option "InvertX"			*/
	int	button_number;	/* option "ButtonNumber"		*/
	int	packet_size;
	unsigned char packet[EETI_RESPONSE_SIZE];
} EETIeGalaxPrivateRec, *EETIeGalaxPrivatePtr;

#endif
