/*
    (c)2006 MPlayer / Reynaldo H. Verdejo Pinochet
	Based on code by Alex Beregszaszi for his 'center' filter

    License: GPL

    Simple voice removal filter
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mplayerxp.h"
#include "af.h"
#include "osdep/mplib.h"

// Data for specific instances of this filter

// Initialization and runtime control
static MPXP_Rc __FASTCALL__ config(struct af_instance_s* af,const af_conf_t* arg)
{
    af->conf.rate	= arg->rate;
    af->conf.nch	= arg->nch;
    af->conf.format	= MPAF_NE|MPAF_F|MPAF_BPS_4;
    return af_test_output(af,arg);
}
static MPXP_Rc control(struct af_instance_s* af, int cmd, any_t* arg)
{
    UNUSED(af);
    UNUSED(cmd);
    UNUSED(arg);
    return MPXP_Unknown;
}

// Deallocate memory
static void uninit(struct af_instance_s* af)
{
    UNUSED(af);
}

// Filter data through filter
static mp_aframe_t* play(struct af_instance_s* af,const mp_aframe_t* ind)
{
    const mp_aframe_t*c	= ind;		 // Current working data
    float*	in	= reinterpret_cast<float*>(c->audio);	 // Audio data
    unsigned	len	= c->len/4;	 // Number of samples in current audio block
    unsigned	nch	= c->nch;	 // Number of channels
    unsigned	i;
    mp_aframe_t* outd = new_mp_aframe_genome(ind);
    mp_alloc_aframe(outd);
    float*	out	= reinterpret_cast<float*>(outd->audio);	 // Audio data
    UNUSED(af);
    /*
	FIXME1 add a low band pass filter to avoid suppressing
	centered bass/drums
	FIXME2 better calculated* attenuation factor
    */

    for(i=0;i<len;i+=nch) {
	out[i]  = (in[i] - in[i+1]) * 0.7;
	out[i+1]=out[i];
    }

    return outd;
}

// Allocate memory and set function pointers
static MPXP_Rc af_open(af_instance_t* af){
    af->config	= config;
    af->control	= control;
    af->uninit	= uninit;
    af->play	= play;
    af->mul.n	= 1;
    af->mul.d	= 1;
    check_pin("afilter",af->pin,AF_PIN);
    return MPXP_Ok;
}

// Description of this filter
extern const af_info_t af_info_karaoke = {
    "Simple karaoke/voice-removal audio filter",
    "karaoke",
    "Reynaldo H. Verdejo Pinochet",
    "",
    AF_FLAGS_REENTRANT,
    af_open
};
