/*=============================================================================
//
//  This software has been released under the terms of the GNU General Public
//  license. See http://www.gnu.org/copyleft/gpl.html for details.
//
//  Copyright 2002 Anders Johansson ajh@watri.uwa.edu.au
//
//=============================================================================
*/

/* This filter adds a sub-woofer channels to the audio stream by
   averaging the left and right channel and low-pass filter them. The
   low-pass filter is implemented as a 4th order IIR Butterworth
   filter, with a variable cutoff frequency between 10 and 300 Hz. The
   filter gives 24dB/octave attenuation. There are two runtime
   controls one for setting which channel to insert the sub-audio into
   called AF_CONTROL_SUB_CH and one for setting the cutoff frequency
   called AF_CONTROL_SUB_FC.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "af.h"
#include "aflib.h"
#include "osdep/mplib.h"
#include "pp_msg.h"

// Q value for low-pass filter
#define Q 1.0

// Analog domain biquad section
typedef struct{
  float a[3];		// Numerator coefficients
  float b[3];		// Denominator coefficients
} biquad_t;

// S-parameters for designing 4th order Butterworth filter
static biquad_t sp[2] = {{{1.0,0.0,0.0},{1.0,0.765367,1.0}},
			 {{1.0,0.0,0.0},{1.0,1.847759,1.0}}};

// Data for specific instances of this filter
typedef struct af_sub_s
{
  float w[2][4];	// Filter taps for low-pass filter
  float q[2][2];	// Circular queues
  float	fc;		// Cutoff frequency [Hz] for low-pass filter
  float k;		// Filter gain;
  unsigned ch;		// Channel number which to insert the filtered data
}af_sub_t;

// Initialization and runtime control
static MPXP_Rc __FASTCALL__ control(struct af_instance_s* af, int cmd, any_t* arg)
{
  af_sub_t* s   = af->setup;

  switch(cmd){
  case AF_CONTROL_REINIT:{
    // Sanity check
    if(!arg) return MPXP_Error;

    af->data->rate   = ((mp_aframe_t*)arg)->rate;
    af->data->nch    = max(s->ch+1,((mp_aframe_t*)arg)->nch);
    af->data->format = MPAF_F|MPAF_NE|4;

    // Design low-pass filter
    s->k = 1.0;
    if((-1 == szxform(sp[0].a, sp[0].b, Q, s->fc,
       (float)af->data->rate, &s->k, s->w[0])) ||
       (-1 == szxform(sp[1].a, sp[1].b, Q, s->fc,
       (float)af->data->rate, &s->k, s->w[1])))
      return MPXP_Error;
    return af_test_output(af,(mp_aframe_t*)arg);
  }
  case AF_CONTROL_SHOWCONF:
    MSG_INFO("[af_sub] assigned channel %i\n",s->ch);
    return MPXP_Ok;
  case AF_CONTROL_COMMAND_LINE:{
    int   ch=5;
    float fc=60.0;
    sscanf(arg,"%f:%i", &fc , &ch);
    if(MPXP_Ok != control(af,AF_CONTROL_SUB_CH | AF_CONTROL_SET, &ch))
      return MPXP_Error;
    return control(af,AF_CONTROL_SUB_FC | AF_CONTROL_SET, &fc);
  }
  case AF_CONTROL_SUB_CH | AF_CONTROL_SET: // Requires reinit
    // Sanity check
    if((*(int*)arg >= AF_NCH) || (*(int*)arg < 0)){
      MSG_ERR("[sub] Subwoofer channel number must be between "
	     " 0 and %i current value is %i\n", AF_NCH-1, *(int*)arg);
      return MPXP_Error;
    }
    s->ch = *(int*)arg;
    return MPXP_Ok;
  case AF_CONTROL_SUB_CH | AF_CONTROL_GET:
    *(int*)arg = s->ch;
    return MPXP_Ok;
  case AF_CONTROL_SUB_FC | AF_CONTROL_SET: // Requires reinit
    // Sanity check
    if((*(float*)arg > 300) || (*(float*)arg < 20)){
      MSG_ERR("[sub] Cutoff frequency must be between 20Hz and"
	     " 300Hz current value is %0.2f",*(float*)arg);
      return MPXP_Error;
    }
    // Set cutoff frequency
    s->fc = *(float*)arg;
    return MPXP_Ok;
  case AF_CONTROL_SUB_FC | AF_CONTROL_GET:
    *(float*)arg = s->fc;
    return MPXP_Ok;
  default: break;
  }
  return MPXP_Unknown;
}

// Deallocate memory
static void __FASTCALL__ uninit(struct af_instance_s* af)
{
  if(af->data)
    mp_free(af->data);
  if(af->setup)
    mp_free(af->setup);
}

#ifndef IIR
#define IIR(in,w,q,out) { \
  float h0 = (q)[0]; \
  float h1 = (q)[1]; \
  float hn = (in) - h0 * (w)[0] - h1 * (w)[1];  \
  out = hn + h0 * (w)[2] + h1 * (w)[3];	 \
  (q)[1] = h0; \
  (q)[0] = hn; \
}
#endif

// Filter data through filter
static mp_aframe_t* __FASTCALL__ play(struct af_instance_s* af, mp_aframe_t* data,int final)
{
  mp_aframe_t*    c   = data;	 // Current working data
  af_sub_t*  	s   = af->setup; // Setup for this instance
  float*   	a   = c->audio;	 // Audio data
  int		len = c->len/4;	 // Number of samples in current audio block
  int		nch = c->nch;	 // Number of channels
  int		ch  = s->ch;	 // Channel in which to insert the sub audio
  register int  i;

  // Run filter
  for(i=0;i<len;i+=nch){
    // Average left and right
    register float x = 0.5 * (a[i] + a[i+1]);
    IIR(x * s->k, s->w[0], s->q[0], x);
    IIR(x , s->w[1], s->q[1], a[i+ch]);
  }

  return c;
}

// Allocate memory and set function pointers
static MPXP_Rc __FASTCALL__ open(af_instance_t* af){
  af_sub_t* s;
  af->control=control;
  af->uninit=uninit;
  af->play=play;
  af->mul.n=1;
  af->mul.d=1;
  af->data=mp_calloc(1,sizeof(mp_aframe_t));
  af->setup=s=mp_calloc(1,sizeof(af_sub_t));
  if(af->data == NULL || af->setup == NULL)
    return MPXP_Error;
  // Set default values
  s->ch = 5;  	 // Channel nr 6
  s->fc = 60; 	 // Cutoff frequency 60Hz
  return MPXP_Ok;
}

// Description of this filter
const af_info_t af_info_sub = {
    "Audio filter for adding a sub-base channel",
    "sub",
    "Anders",
    "",
    AF_FLAGS_NOT_REENTRANT,
    open
};
