#ifndef __MP_AUDIO_FRAME_INCLUDED_H
#define __MP_AUDIO_FRAME_INCLUDED_H 1

#include "mp_config.h"
#include <stdlib.h>

/* The sample format system is based on bitmasks. The
   format definition only refers to the storage format not the
   resolution. */
typedef enum mpaf_format_enum{
    MPAF_BPS_MASK	=0x00000FFFUL, /* byte per sample */
// Endianess
    MPAF_BE		=0x00000000UL, // Big Endian
    MPAF_LE		=0x00001000UL, // Little Endian
    MPAF_END_MASK	=0x00001000UL,
#if WORDS_BIGENDIAN	// Native endian of cpu
    MPAF_NE		=MPAF_BE,
#else
    MPAF_NE		=MPAF_LE,
#endif
// Signed/unsigned
    MPAF_SI		=0x00000000UL, // SIgned
    MPAF_US		=0x00002000UL, // Un Signed
    MPAF_SIGN_MASK	=0x00002000UL,
// Fixed or floating point
    MPAF_I		=0x00000000UL, // Integer
    MPAF_F		=0x00004000UL, // Foating point
    MPAF_POINT_MASK	=0x00004000UL,
// Special flags refering to non pcm data
    MPAF_PCM		=0x00010000UL, //
    MPAF_A_LAW		=0x00060000UL, //
    MPAF_MU_LAW		=0x00070000UL, //
    MPAF_IMA_ADPCM	=0x00110000UL, // Same as 16 bit signed int 
    MPAF_MPEG2		=0x00500000UL, // MPEG1 layer2 audio
    MPAF_MPEG3		=0x00550000UL, // MPEG1 layer3 audio
    MPAF_AC3		=0x20000000UL, // Dolby Digital AC3
    MPAF_SPECIAL_MASK	=0xFFFF0000UL
}mpaf_format_e;

/* Decodes the format from mplayer format to libaf format */
extern mpaf_format_e __FASTCALL__ mpaf_format_decode(unsigned format);
extern unsigned __FASTCALL__      mpaf_format_encode(mpaf_format_e fmt);
extern char* __FASTCALL__         mpaf_fmt2str(mpaf_format_e format, char* str, size_t size);
extern mpaf_format_e __FASTCALL__ mpaf_str2fmt(const char *str);


typedef struct mp_audio_frame_s {
    unsigned		flags; /* currently unused */
    float		pts;   /* PTS if this frame */
    unsigned		xp_idx;/* index in ring buffer */
    any_t*		audio; /* data of audio frame */
    unsigned		len;   /* length of data */
    /*------ stream description ----------*/
    unsigned		rate;  /* rate of audio */
    unsigned		nch;   /* number of channels */
    mpaf_format_e	format;/* PCM format of audio */
}mp_aframe_t;

extern mp_aframe_t*	new_mp_aframe(unsigned rate,unsigned nch,mpaf_format_e format,unsigned xp_idx);
extern int		free_mp_aframe(mp_aframe_t* mpaf);

#endif
