/***************************************************************************
 *   Copyright (C) 2007 Ryan Schultz, PCSX-df Team, PCSX team              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.           *
 ***************************************************************************/

#ifndef __DECODE_XA_H__
#define __DECODE_XA_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "psxcommon.h"

typedef struct {
	s32	y0, y1;
} ADPCM_Decode_t;

typedef struct {
	int				freq;
	int				nbits;
	int				stereo;
	int				nsamples;
	ADPCM_Decode_t	left, right;
	short			pcm[2][16384];
} xa_decode_t;

typedef struct {
	u8  filenum;
	u8  channum;
	u8  submode;
	u8  coding;

	u8  filenum2;
	u8  channum2;
	u8  submode2;
	u8  coding2;
} xa_subheader_t;

s32 xa_decode_sector(xa_subheader_t* header, xa_decode_t* decoded, u8* xaData);

#ifdef __cplusplus
}
#endif
#endif
