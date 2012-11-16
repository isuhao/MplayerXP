/*=============================================================================
//
//  This software has been released under the terms of the GNU General Public
//  license. See http://www.gnu.org/copyleft/gpl.html for details.
//
//  Copyright 2006 Michael Niedermayer
//  Copyright 2004 Alex Beregszaszi & Pierre Lombard (original af_extrastereo.c upon which this is based)
//
//=============================================================================
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <inttypes.h>
#include <math.h>
#include <limits.h>

#include "af.h"
#include "osdep/mplib.h"
#include "pp_msg.h"

// Data for specific instances of this filter
typedef struct af_sinesuppress_s
{
    double freq;
    double decay;
    double real;
    double imag;
    double ref;
    double pos;
}af_sinesuppress_t;

static mp_aframe_t* play_s16(struct af_instance_s* af, mp_aframe_t* data,int final);
#if 0
static mp_aframe_t* play_float(struct af_instance_s* af, mp_aframe_t* data,int final);
#endif

// Initialization and runtime control
static MPXP_Rc __FASTCALL__ config(struct af_instance_s* af,const mp_aframe_t* arg)
{
    af_sinesuppress_t* s   = (af_sinesuppress_t*)af->setup;
    // Sanity check
    if(!arg) return MPXP_Error;

    af->data->rate   = arg->rate;
    af->data->nch    = 1;
#if 0
    if (mpaf_testa(arg->format,MPAF_NE | MPAF_F) {
	af->data->format = MPAF_FLOAT_NE|4;
	af->play = play_float;
    }// else
#endif
    {
	af->data->format = MPAF_NE|MPAF_SI|2;
	af->play = play_s16;
    }

    return af_test_output(af,arg);
}
static MPXP_Rc control(struct af_instance_s* af, int cmd, any_t* arg)
{
  af_sinesuppress_t* s   = (af_sinesuppress_t*)af->setup;

  switch(cmd){
  case AF_CONTROL_COMMAND_LINE:{
    float f1,f2;
    sscanf((char*)arg,"%f:%f", &f1,&f2);
    s->freq = f1;
    s->decay = f2;
    return MPXP_Ok;
  }
  case AF_CONTROL_SHOWCONF:
  {
    MSG_INFO("[af_sinesuppress] %f:%f\n",s->freq,s->decay);
    return MPXP_Ok;
  }
  }
  return MPXP_Unknown;
}

// Deallocate memory
static void uninit(struct af_instance_s* af)
{
  if(af->data)
    mp_free(af->data);
  if(af->setup)
    mp_free(af->setup);
}

// Filter data through filter
static mp_aframe_t* play_s16(struct af_instance_s* af, mp_aframe_t* data,int final)
{
  af_sinesuppress_t *s = af->setup;
  register int i = 0;
  int16_t *a = (int16_t*)data->audio;	// Audio data
  int len = data->len/2;		// Number of samples

  for (i = 0; i < len; i++)
  {
    double co= cos(s->pos);
    double si= sin(s->pos);

    s->real += co * a[i];
    s->imag += si * a[i];
    s->ref  += co * co;

    a[i] -= (s->real * co + s->imag * si) / s->ref;

    s->real -= s->real * s->decay;
    s->imag -= s->imag * s->decay;
    s->ref  -= s->ref  * s->decay;

    s->pos += 2 * M_PI * s->freq / data->rate;
  }

   MSG_V("[sinesuppress] f:%8.2f: amp:%8.2f\n", s->freq, sqrt(s->real*s->real + s->imag*s->imag) / s->ref);

  return data;
}

#if 0
static mp_aframe_t* play_float(struct af_instance_s* af, mp_aframe_t* data,int final)
{
  af_sinesuppress_t *s = af->setup;
  register int i = 0;
  float *a = (float*)data->audio;	// Audio data
  int len = data->len/4;		// Number of samples
  float avg, l, r;

  for (i = 0; i < len; i+=2)
  {
    avg = (a[i] + a[i + 1]) / 2;

/*    l = avg + (s->mul * (a[i] - avg));
    r = avg + (s->mul * (a[i + 1] - avg));*/

    a[i] = af_softclip(l);
    a[i + 1] = af_softclip(r);
  }

  return data;
}
#endif

// Allocate memory and set function pointers
static MPXP_Rc af_open(af_instance_t* af){
  af->config=config;
  af->control=control;
  af->uninit=uninit;
  af->play=play_s16;
  af->mul.n=1;
  af->mul.d=1;
  af->data=mp_calloc(1,sizeof(mp_aframe_t));
  af->setup=mp_calloc(1,sizeof(af_sinesuppress_t));
  if(af->data == NULL || af->setup == NULL)
    return MPXP_Error;

  ((af_sinesuppress_t*)af->setup)->freq = 50.0;
  ((af_sinesuppress_t*)af->setup)->decay = 0.0001;
    check_pin("afilter",af->pin,AF_PIN);
  return MPXP_Ok;
}

// Description of this filter
const af_info_t af_info_sinesuppress = {
    "Sine Suppress",
    "sinesuppress",
    "Michael Niedermayer",
    "",
    0,
    af_open
};
