/*=============================================================================
//	
//  This software has been released under the terms of the GNU General Public
//  license. See http://www.gnu.org/copyleft/gpl.html for details.
//
//  Copyright (C) 2001 Rafal Bosak <gyver@fanthom.irc.pl>
//  Copyright 2004 Nickols_K <nickols_k@mail.ru>
//=============================================================================
*/

/*
   Crystality filter, was based on Crystality Plugin v0.92 for XMMS by
   Rafal Bosak <gyver@fanthom.irc.pl>.

   Note: You will need a reasonably good stereo and a good ear to notice
   quality improvement, otherwise this is not for you.

   This plugin tries to patch mp3 format flaws, not a poor audio hardware!
*/

#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <inttypes.h>
#define __USE_GNU
#include <math.h>
#include <limits.h>

#include "af.h"
#include "dsp.h"
#include "osdep/mplib.h"

#define DELAY1 35000
#define DELAY2 21000
#define DELAY3 14000

/* Data for specific instances of this filter */
typedef struct af_crystality_s
{
    int echos;
    int echo_level;
    int feedback_level;

    float echo_sfactor;
    float feedback_sfactor;
    lowp_t lp_reverb[2];

    float left0p, right0p;
    float *buf;
    unsigned buf_size;
    unsigned _bufPos;
    unsigned bufPos[3];
} af_crystality_t;

static void __FASTCALL__ set_defaults(af_crystality_t *s)
{
    s->echos = 3;
    s->echo_level = 11;
    s->feedback_level = 30;
}

static void __FASTCALL__ echo_level_cb(af_crystality_t *s)
{
    s->echo_sfactor = s->echo_level;
}

static void __FASTCALL__ feedback_level_cb(af_crystality_t *s)
{
    s->feedback_sfactor = (s->feedback_level * 3) / 2;
}

static void __FASTCALL__ init_echo3d(af_crystality_t *s,unsigned rate)
{
    echo_level_cb(s);
    feedback_level_cb(s);
    lowp_init(&s->lp_reverb[0],5000,rate);
    lowp_init(&s->lp_reverb[1],5000,rate);
    s->buf_size=0;
    if(s->echos>0) s->buf_size+= DELAY1;
    if(s->echos>1) s->buf_size+= DELAY2;
    if(s->echos>2) s->buf_size+= DELAY3;
    s->buf=mp_malloc(s->buf_size*sizeof(float));
}

/*
 * quite nice echo
 */

static void __FASTCALL__ echo3d(af_crystality_t *s,float *data, unsigned datasize)
{
  unsigned x,i;
  float _left, _right, dif, leftc, rightc, left[4], right[4];
  float *dataptr;
  float lt, rt;
  unsigned weight[2][3] = { { 9, 8, 8 }, { 11, 9, 10 }};

  s->_bufPos = s->buf_size - 1;
  s->bufPos[0] = 1 + s->buf_size - DELAY1;
  s->bufPos[1] = 1 + s->buf_size - DELAY1 - DELAY2;
  s->bufPos[2] = 1 + s->buf_size - DELAY1 - DELAY2 - DELAY3;
  dataptr = data;

  for (x = 0; x < datasize; x += 8) {

    // ************ load sample **********
    left[0] = dataptr[0];	
    right[0] = dataptr[1];

    leftc = rightc = 0;
    // ************ calc echos **********
    for(i=0;i<s->echos;i++)
    {
	dif = (left[i] - right[i]);
	// ************ slightly expand stereo for direct input **********
	left[i] += dif;
	right[i] -= dif;
	left[i] *= 0.5;
	right[i] *= 0.5;
	// ************ compute echo  **********
	left[i+1] = s->buf[s->bufPos[i]++];
	if (s->bufPos[i] == s->buf_size) s->bufPos[i] = 0;	
	right[i+1] = s->buf[s->bufPos[i]++];
	if (s->bufPos[i] == s->buf_size) s->bufPos[i] = 0;
	leftc += left[i]/weight[0][i];
	rightc += right[i]/weight[1][i];
    }

    // ************ mix reverb with (near to) direct input **********
    _left = s->left0p + leftc * s->echo_sfactor / 16;
    _right = s->right0p + rightc * s->echo_sfactor / 16;

    /* do not reverb high frequencies (filter) */
    lt=(lowpass(&s->lp_reverb[0],leftc+left[0]/2)*s->feedback_sfactor)/256;
    rt=(lowpass(&s->lp_reverb[1],rightc+right[0]/2)*s->feedback_sfactor)/256;;

    s->buf[s->_bufPos++] = lt;
    if (s->_bufPos == s->buf_size) s->_bufPos = 0;
    s->buf[s->_bufPos++] = rt;
    if (s->_bufPos == s->buf_size) s->_bufPos = 0;

    s->left0p = left[0];
    s->right0p = right[0];

    // ************ store sample **********
    dataptr[0] = clamp(_left,INT_MIN,INT_MAX);
    dataptr[1] = clamp(_right,INT_MIN,INT_MAX);
    dataptr += 2;

   }
}

// Initialization and runtime control
static int __FASTCALL__ control(struct af_instance_s* af, int cmd, any_t* arg)
{
  af_crystality_t* s   = (af_crystality_t*)af->setup; 

  switch(cmd){
  case AF_CONTROL_REINIT:{
    // Sanity check
    if(!arg) return AF_ERROR;
    if(((af_data_t*)arg)->nch!=2) return AF_ERROR;

    af->data->rate   = ((af_data_t*)arg)->rate;
    af->data->nch    = ((af_data_t*)arg)->nch;
    af->data->format = AF_FORMAT_NE | AF_FORMAT_F;
    af->data->bps    = 4;
    init_echo3d(s,af->data->rate);
    
    return af_test_output(af,arg);
  }
  case AF_CONTROL_COMMAND_LINE:{
    sscanf((char*)arg,"%d:%d:%d",
	    &s->echos,
	    &s->echo_level,
	    &s->feedback_level);
    s->echos=clamp(s->echos,1,3);
    return AF_OK;
  }
  default: break;
  }
  return AF_UNKNOWN;
}

// Deallocate memory 
static void __FASTCALL__ uninit(struct af_instance_s* af)
{
  if(af->data)
    mp_free(af->data);
  if(((af_crystality_t *)af->setup)->buf) mp_free(((af_crystality_t *)af->setup)->buf);
  if(af->setup)
    mp_free(af->setup);
}

// Filter data through filter
static af_data_t* __FASTCALL__ play(struct af_instance_s* af, af_data_t* data,int final)
{
    af_data_t* c = data; /* Current working data */

    echo3d(af->setup,(float*)c->audio, c->len);
    return c;
}

// Allocate memory and set function pointers
static int __FASTCALL__ af_open(af_instance_t* af){
  af->control=control;
  af->uninit=uninit;
  af->play=play;
  af->mul.n=1;
  af->mul.d=1;
  af->data=mp_calloc(1,sizeof(af_data_t));
  af->setup=mp_calloc(1,sizeof(af_crystality_t));
  if(af->data == NULL || af->setup == NULL)
    return AF_ERROR;
  set_defaults(af->setup);
  init_echo3d(af->setup,44100);
  return AF_OK;
}

// Description of this filter
const af_info_t af_info_echo3d = {
  "3D echos",
  "echo3d",
  "Rafal Bosak <gyver@fanthom.irc.pl>",
  "Modified by Nickols_K",
  AF_FLAGS_REENTRANT,
  af_open
};
