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
#define CLAMP(_X_,_MI_,_MA_)	{if(_X_<_MI_)_X_=_MI_;if(_X_>_MA_)_X_=_MA_;}

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
void ADPCM_InitDecode(ADPCM_Decode_t* decp)
{
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

static __inline void ADPCM_DecodeBlock16(const u8* block_header, xa_decode_t* xdp, const u8* samples, ADPCM_Decode_t* decp, int channel, short* destp)
{
	// Extract 4 or 8 bit nibble depending on BPS
	int bps = xdp->nbits;

	//printf("Bps: %01d", bps);
	u32 sampleData = 0;
	s32 nibble = 0;
	s32 finalSample = 0;

	s32 fy0, fy1;

	fy0 = decp->y0;
	fy1 = decp->y1;

	for (int block = 0; block < 28 / 4; block++)
	{
		//printf("Shift: %02x", shift);
		//printf("Filter: %02x", filter);

		if (bps == 4)
		{
			nibble = ((sampleData >> (block * bps)) & 0x0F);
		}
		if (xdp->nsamples == 37800 && bps == 8)
		{
			nibble = ((sampleData >> (block * bps)) & 0xFF);
		}

		u8 shift = 12 - (block_header[4 + block] & 0xF);
		u8 filter = (block_header[4 + block] & 0x30) >> 4;
		s32 filterPos = tbl_XA_Factor[filter][0];
		s32 filterNeg = tbl_XA_Factor[filter][1];

		for (int i = 0; i < 28; i++)
		{
			s16 sam = (nibble << 12) >> shift;
			//printf("Data: %02x", sampleData);
			//printf("Nibble: %02x", nibble);

			finalSample = (sam << shift) + ((fy0 * filterPos) + (fy1 * filterNeg) + 32) / 64;

			CLAMP(finalSample, 32768, 32767);
			//printf("Sample: %02x", finalSample);
			*(destp++) = finalSample;
		}
		decp->y0 = fy0;
		decp->y1 = fy1;
	}
}

static int headtable[4] = { 0,2,8,10 };

//===========================================
static void xa_decode_data(xa_subheader_t* header, xa_decode_t* decoded, u8* xaData)
{
	u8* block_header, * sound_datap, * sound_datap2;
	u8 filterPos;
	int i, j, k, nbits;
	u16 data[4096], * datap;
	u32* Left, * Right;

	//nbits = decoded->nbits == 4 ? 4 : 2;

	// TODO. Extract and mix sample data
	//block_header = xaData + 4;1

	// 16 bytes after header
	//sound_datap = block_header + 16;

	sound_datap = (u8)malloc(4096);

	Left = (u32)malloc(8192);
	Right = (u32)malloc(8192);

	for (j = 0; j < 18; j++)
	{
		// 4 bit vs 8 bit sound
		for (int i = 0; i < decoded->nbits; i++)
		{
			block_header = xaData + j * 128;		// sound groups header
			sound_datap = block_header + 16;	// sound data just after the header
			datap = data;
			sound_datap2 = sound_datap + i;

			// Odds are Left positives are Right
			// Note, the interleave changes based on SampleRate, Stenznek mentioned some games like rugrats
			// handle this interleave incorrectly and will spam the buffer with too much data.
			// We must crash and clear the buffer for audio to continue?
			for (u32 k = 0; k < 7; k++, sound_datap2 += 2)
			{
				u32 sampleData = (u32)(sound_datap2[0] & 0x0f);
				sampleData |= (u32)((sound_datap2[4] & 0x0f) << 4);
				sampleData |= (u32)((sound_datap2[8] & 0x0f) << 8);
				sampleData |= (u32)((sound_datap2[12] & 0x0f) << 12);
				if (i % 2)
				{
					Right[i] = sampleData;
				}
				else
				{
					Left[i] = sampleData;
				}
			}
			if (decoded->stereo)
			{
				// Allocate maximum sample size
				//cdr.Xa.pcm[0].reserve(16384);
				//cdr.Xa.pcm[1].reserve(16384);

				ADPCM_DecodeBlock16(block_header, header, Left, &decoded->left, 0, decoded->pcm[0]);
				ADPCM_DecodeBlock16(block_header, header, Right, &decoded->right, 1, decoded->pcm[1]);

				//Console.Warning("Sample L: %02x", decoded->pcm[0].front());
				//Console.Warning("Sample R: %02x", decoded->pcm[1].front());
			}
			else
			{
				// Mono sound
				//cdr.Xa.pcm[0].reserve(16384);
				ADPCM_DecodeBlock16(block_header, header, Left, &decoded->left, 0, decoded->pcm[0]);
				ADPCM_DecodeBlock16(block_header, header, Right, &decoded->left, 0, decoded->pcm[0]);

				//Console.Warning("Sample M: %02x", decoded->pcm[0].front());
			}
		}
		sound_datap++;
	}
}

//============================================
//===  XA SPECIFIC ROUTINES
//============================================

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
	u8* sectorp,
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
	xa_decode_data(subheadp, xdp, sectorp);

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