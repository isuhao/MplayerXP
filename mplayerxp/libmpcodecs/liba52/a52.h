/*
 * a52.h
 * Copyright (C) 2000-2002 Michel Lespinasse <walken@zoy.org>
 * Copyright (C) 1999-2000 Aaron Holtzman <aholtzma@ess.engr.uvic.ca>
 *
 * This file is part of a52dec, a mp_free ATSC A-52 stream decoder.
 * See http://liba52.sourceforge.net/ for updates.
 *
 * a52dec is mp_free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * a52dec is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef A52_H
#define A52_H

#include "mp_config.h"

#ifndef LIBA52_DOUBLE
typedef float sample_t;
#else
typedef double sample_t;
#endif

typedef struct a52_state_s a52_state_t;

enum {
    A52_CHANNEL	=0,
    A52_MONO	=1,
    A52_STEREO	=2,
    A52_3F	=3,
    A52_2F1R	=4,
    A52_3F1R	=5,
    A52_2F2R	=6,
    A52_3F2R	=7,
    A52_CHANNEL1=8,
    A52_CHANNEL2=9,
    A52_DOLBY	=10,
    A52_CHANNEL_MASK=15,
    A52_LFE	=16,
    A52_ADJUST_LEVEL=32
};

a52_state_t * a52_init (uint32_t mm_accel);
sample_t * a52_samples (a52_state_t * state);
extern int a52_syncinfo (uint8_t * buf, int * flags,
			int * sample_rate, int * bit_rate);
extern int a52_frame (a52_state_t * state, uint8_t * buf, int * flags,
			sample_t * level, sample_t bias);
extern void a52_dynrng (a52_state_t * state,
			sample_t (* call) (sample_t, any_t*), any_t* data);
extern int a52_block (a52_state_t * state);
extern void a52_free (a52_state_t * state);
extern uint16_t crc16_block(uint8_t *data,uint32_t num_bytes);
extern any_t* a52_resample_init_float(a52_state_t * state,uint32_t mm_accel,int flags,int chans);
extern any_t* a52_resample_init(a52_state_t * state,uint32_t mm_accel,int flags,int chans);
extern int (* a52_resample) (float * _f, int16_t * s16);
extern int (* a52_resample32) (float * _f, float * s16);

#endif /* A52_H */
