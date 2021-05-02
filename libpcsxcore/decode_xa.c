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

 /*
 * XA audio decoding functions (Kazzuya).
 */

#include "decode_xa.h"

#define FIXED

#define NOT(_X_)				(!(_X_))
#define XACLAMP(_X_,_MI_,_MA_)	{if(_X_<_MI_)_X_=_MI_;if(_X_>_MA_)_X_=_MA_;}

#define SH	4
#define SHC	10

 // No$ calls this pos_xa_adpcm_table. This is also the other neg one as well XA ADPCM only has 4 values on either side where SPU has 5
static const s32 tbl_XA_Factor[16][2] = {
	{0, 0},
	{60, 0},
	{115, -52},
	{98, -55} };

 //============================================
 //===  ADPCM DECODING ROUTINES
 //============================================

#ifndef FIXED
static double K0[4] = {
	0.0,
	0.9375,
	1.796875,
	1.53125
};

static double K1[4] = {
	0.0,
	0.0,
	-0.8125,
	-0.859375
};
#else
static int K0[4] = {
	0.0 * (1 << SHC),
	0.9375 * (1 << SHC),
	1.796875 * (1 << SHC),
	1.53125 * (1 << SHC)
};

static int K1[4] = {
	0.0 * (1 << SHC),
	0.0 * (1 << SHC),
	-0.8125 * (1 << SHC),
	-0.859375 * (1 << SHC)
};
#endif

#define BLKSIZ 28       /* block size (32 - 4 nibbles) */

//===========================================
void ADPCM_InitDecode(ADPCM_Decode_t* decp) {
	decp->y0 = 0;
	decp->y1 = 0;
}

//===========================================
#ifndef FIXED
#define IK0(fid)	((int)((-K0[fid]) * (1<<SHC)))
#define IK1(fid)	((int)((-K1[fid]) * (1<<SHC)))
#else
#define IK0(fid)	(-K0[fid])
#define IK1(fid)	(-K1[fid])
#endif

static __inline void ADPCM_DecodeBlock16( ADPCM_Decode_t *decp, u8 filter_range, const void *vblockp, short *destp, int inc ) {
	int i;
	int range, filterid;
	long fy0, fy1;
	const u16 *blockp;

	blockp = (const unsigned short *)vblockp;
	filterid = (filter_range >>  4) & 0x0f;
	range    = (filter_range >>  0) & 0x0f;

	fy0 = decp->y0;
	fy1 = decp->y1;

	for (i = BLKSIZ/4; i; --i) {
		long y;
		long x0, x1, x2, x3;

		y = *blockp++;
		x3 = (short)( y        & 0xf000) >> range; x3 <<= SH;
		x2 = (short)((y <<  4) & 0xf000) >> range; x2 <<= SH;
		x1 = (short)((y <<  8) & 0xf000) >> range; x1 <<= SH;
		x0 = (short)((y << 12) & 0xf000) >> range; x0 <<= SH;

		x0 -= (IK0(filterid) * fy0 + (IK1(filterid) * fy1)) >> SHC; fy1 = fy0; fy0 = x0;
		x1 -= (IK0(filterid) * fy0 + (IK1(filterid) * fy1)) >> SHC; fy1 = fy0; fy0 = x1;
		x2 -= (IK0(filterid) * fy0 + (IK1(filterid) * fy1)) >> SHC; fy1 = fy0; fy0 = x2;
		x3 -= (IK0(filterid) * fy0 + (IK1(filterid) * fy1)) >> SHC; fy1 = fy0; fy0 = x3;

		CLAMP( x0, -32768<<SH, 32767<<SH ); *destp = x0 >> SH; destp += inc;
		CLAMP( x1, -32768<<SH, 32767<<SH ); *destp = x1 >> SH; destp += inc;
		CLAMP( x2, -32768<<SH, 32767<<SH ); *destp = x2 >> SH; destp += inc;
		CLAMP( x3, -32768<<SH, 32767<<SH ); *destp = x3 >> SH; destp += inc;
	}
	decp->y0 = fy0;
	decp->y1 = fy1;
}

static int headtable[4] = { 0,2,8,10 };

//===========================================
static void xa_decode_data(xa_decode_t* xdp, unsigned char* srcp) {
	u8 *block_header, *sound_datap, *sound_datap2;
	u8 filterPos;
	int i, j, k, nbits;
	u16 data[4096], *datap;
	u16 *Left, *Right;
	u16 *destp;

	destp = xdp->pcm;
	nbits = xdp->nbits == 4 ? 4 : 2;

	//TODO. Extract and mix sample data
	// block_header = xaData + 4;

	// 16 bytes after header
	// sound_datap = block_header + 16;

	//Left = new u16[8192];
	//Right = new u16[8192];

	for (i = 0; i < 18; i++)
	{
		block_header = srcp + i * 128;	// sound groups header
		sound_datap = block_header + 16;	// sound data just after the header

		// Odds are Left positives are Right
		// Note, the interleave changes based on SampleRate, Stenznek mentioned some games like rugrats
		// handle this interleave incorrectly and will spam the buffer with too much data.
		// We must crash and clear the buffer for audio to continue?
		// The below is an attempt to extract word data into each seperate channel by following pcsx and No$
		// We have to also condisder the amount of interleve. 1 / 8 means this should only process once every 8 sectors or so
		for (u32 block = 0; block < 3; block++)
		{

			// Is this extracting enough data at once?
			// 2-2 Sample Rate     (0=37800Hz, 1=18900Hz, 2-3=Reserved)
			// I don't believe were extracting the right data based on the size of bits and the sample rate
			/*********************************************************
			* PCSX SUGGESTS THIS FOR 4 BIT SOUND
			* *(datap++) = (u32)(sound_datap2[0] & 0x0f) |
			* (u32)((sound_datap2[4] & 0x0f) << 4) |
			* (u32)((sound_datap2[8] & 0x0f) << 8) |
			* (u32)((sound_datap2[12] & 0x0f) << 12);

			* PCSX SUGGESTS THIS FOR 8 BIT SOUND
			* *(datap++) = (U16)sound_datap2[0] |
			* (U16)(sound_datap2[4] << 8);

			* NO$ SUGGESTS BOTH CORRECT

			* No$ Suggests this is an alternate way to extract samples
			* t = signed4bit((src[16+blk+j*4] SHR (nibble*4)) AND 0Fh)
			***************************************************************/
			switch(nbits)
			{
				case 4:
				datap = data;
				for (k=0; k < 7; k++, sound_datap += 16)
				{
					*(datap++) = (u32)(sound_datap[0]) |
					(u32)((sound_datap[4] & 0x0f) << 4) |
					(u32)((sound_datap[8] & 0x0f) << 8) |
					(u32)((sound_datap[12] & 0x0f) << 12);
				}
				datap += 28 * 2;
				break;

				case 8:
				datap = data;

				for (k=0; k < 14; k++, sound_datap += 8)
				{
					*(datap++) = (u16)sound_datap[0] |
					(u16)(sound_datap[4] << 8);
				}
				datap += 28;
				break;
			}

			if (xdp->stereo)
			{
				ADPCM_DecodeBlock16(&xdp->left, block_header[headtable[i] + 0], xdp, datap, 2);
				ADPCM_DecodeBlock16(&xdp->right, block_header[headtable[i] + 1], xdp, datap, 2);

				//Console.Warning("Sample L: %02x", decoded->pcm[0][i]);
				//Console.Warning("Sample R: %02x", decoded->pcm[1][i]);
			}
			else
			{
				// Mono sound
				ADPCM_DecodeBlock16(&xdp->left, block_header[headtable[i] + 0], xdp, datap, 1);
				ADPCM_DecodeBlock16(&xdp->left, block_header[headtable[i] + 1], xdp, datap, 1);

				//Console.Warning("Sample M: %02x", decoded->pcm[0][i]);
			}
		}
		sound_datap += 128;
	}

}

//============================================
//===  XA SPECIFIC ROUTINES
//============================================
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

#define SUB_SUB_EOF     (1<<7)  // end of file
#define SUB_SUB_RT      (1<<6)  // real-time sector
#define SUB_SUB_FORM    (1<<5)  // 0 form1  1 form2
#define SUB_SUB_TRIGGER (1<<4)  // used for interrupt
#define SUB_SUB_DATA    (1<<3)  // contains data
#define SUB_SUB_AUDIO   (1<<2)  // contains audio
#define SUB_SUB_VIDEO   (1<<1)  // contains video
#define SUB_SUB_EOR     (1<<0)  // end of record

#define AUDIO_CODING_GET_STEREO(_X_)    ( (_X_) & 3)
#define AUDIO_CODING_GET_FREQ(_X_)      (((_X_) >> 2) & 3)
#define AUDIO_CODING_GET_BPS(_X_)       (((_X_) >> 4) & 3)
#define AUDIO_CODING_GET_EMPHASIS(_X_)  (((_X_) >> 6) & 1)

#define SUB_UNKNOWN 0
#define SUB_VIDEO   1
#define SUB_AUDIO   2

//============================================
static int parse_xa_audio_sector(xa_decode_t* xdp,
	xa_subheader_t* subheadp,
	unsigned char* sectorp,
	int is_first_sector) {
	if (is_first_sector) {
		switch (AUDIO_CODING_GET_FREQ(subheadp->coding)) {
		case 0: xdp->freq = 37800;   break;
		case 1: xdp->freq = 18900;   break;
		default: xdp->freq = 0;      break;
		}
		switch (AUDIO_CODING_GET_BPS(subheadp->coding)) {
		case 0: xdp->nbits = 4; break;
		case 1: xdp->nbits = 8; break;
		default: xdp->nbits = 0; break;
		}
		switch (AUDIO_CODING_GET_STEREO(subheadp->coding)) {
		case 0: xdp->stereo = 0; break;
		case 1: xdp->stereo = 1; break;
		default: xdp->stereo = 0; break;
		}

		if (xdp->freq == 0)
			return -1;

		ADPCM_InitDecode(&xdp->left);
		ADPCM_InitDecode(&xdp->right);

		xdp->nsamples = 18 * 28 * 8;
		if (xdp->stereo == 1) xdp->nsamples /= 2;
	}
	xa_decode_data(xdp, sectorp);

	return 0;
}

//================================================================
//=== THIS IS WHAT YOU HAVE TO CALL
//=== xdp              - structure were all important data are returned
//=== sectorp          - data in input
//=== pcmp             - data in output
//=== is_first_sector  - 1 if it's the 1st sector of the stream
//===                  - 0 for any other successive sector
//=== return -1 if error
//================================================================
s32 xa_decode_sector(xa_decode_t* xdp,
	unsigned char* sectorp, int is_first_sector) {
	if (parse_xa_audio_sector(xdp, (xa_subheader_t*)sectorp, sectorp + sizeof(xa_subheader_t), is_first_sector))
		return -1;

	return 0;
}

/* EXAMPLE:
"nsamples" is the number of 16 bit samples
every sample is 2 bytes in mono and 4 bytes in stereo

xa_decode_t	xa;

	sectorp = read_first_sector();
	xa_decode_sector( &xa, sectorp, 1 );
	play_wave( xa.pcm, xa.freq, xa.nsamples );

	while ( --n_sectors )
	{
		sectorp = read_next_sector();
		xa_decode_sector( &xa, sectorp, 0 );
		play_wave( xa.pcm, xa.freq, xa.nsamples );
	}
*/
