#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace	usr;
/*
  This audio filter exports the incoming signal to raw or RIFF WAVE file
  TODO: add length + pts to export into sockets
*/
#include <iostream>
#include <fstream>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>

#include "osdep/bswap.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "af.h"
#include "af_internal.h"
#include "mpxp_help.h"
#include "pp_msg.h"

#define WAV_ID_RIFF 0x46464952 /* "RIFF" */
#define WAV_ID_WAVE 0x45564157 /* "WAVE" */
#define WAV_ID_FMT  0x20746d66 /* "fmt " */
#define WAV_ID_DATA 0x61746164 /* "data" */

struct WaveHeader
{
	uint32_t riff;
	uint32_t file_length;
	uint32_t wave;
	uint32_t fmt;
	uint32_t fmt_length;
	uint16_t fmt_tag;
	uint16_t channels;
	uint32_t sample_rate;
	uint32_t bytes_per_second;
	uint16_t block_align;
	uint16_t bits;
	uint32_t data;
	uint32_t data_length;
};

// Data for specific instances of this filter
struct af_raw_t
{
  char* filename;	// File to export data
  int wav_mode;
  struct WaveHeader wavhdr;
  std::ofstream fd;
};

/* Initialization and runtime control_af
   af audio filter instance
   cmd control_af command
   arg argument
*/
static MPXP_Rc __FASTCALL__ config_af(af_instance_t* af,const af_conf_t* arg)
{
    af_raw_t* s = reinterpret_cast<af_raw_t*>(af->setup);
    char *pt;
    // Accepts any streams
    memcpy(&af->conf,arg,sizeof(af_conf_t));
    if(s->fd.is_open()) { /* reenterability */
	s->fd.open(s->filename,std::ios_base::out|std::ios_base::binary);
	if(!(s->fd.is_open()))
	    mpxp_err<<"Can't open "<<s->filename<<std::endl;
	pt=strchr(s->filename,'.');
	s->wav_mode=0;
	if(pt) if(strcmp(pt+1,"wav")==0) s->wav_mode=1;
	if(s->wav_mode && s->fd.is_open())
	{
	    uint16_t fmt=af->conf.format>>16;
	    if(!fmt) fmt=0x01; /* pcm */
	    s->wavhdr.riff=le2me_32(WAV_ID_RIFF);
	    s->wavhdr.file_length = le2me_32(0x7ffff000) + sizeof(struct WaveHeader) - 8;
	    s->wavhdr.wave=le2me_32(WAV_ID_WAVE);
	    s->wavhdr.fmt=le2me_32(WAV_ID_FMT);
	    s->wavhdr.fmt_length=le2me_32(16);
	    s->wavhdr.fmt_tag = le2me_16(fmt);
	    s->wavhdr.channels = le2me_16(af->conf.nch);
	    s->wavhdr.sample_rate = le2me_32(af->conf.rate);
	    s->wavhdr.bytes_per_second = le2me_32((af->conf.format&MPAF_BPS_MASK)*af->conf.rate*af->conf.nch);
	    s->wavhdr.block_align = le2me_16(af->conf.nch*(af->conf.format&MPAF_BPS_MASK));
	    s->wavhdr.bits = le2me_16((af->conf.format&MPAF_BPS_MASK)*8);
	    s->wavhdr.data=le2me_32(WAV_ID_DATA);
	    s->wavhdr.data_length=le2me_32(0x7ffff000);
	    s->fd.write((char*)(&s->wavhdr),sizeof(struct WaveHeader));
	    s->wavhdr.file_length=s->wavhdr.data_length=0;
	}
    }
    mpxp_v<<"[af_raw] Was reinitialized, rate="<<af->conf.rate<<"Hz, nch = "<<af->conf.nch<<", format = 0x"<<std::hex<<af->conf.format<<std::endl;
    af->conf.format=MPAF_SI|MPAF_NE|MPAF_BPS_2; // fake! fixme !!!
    return MPXP_Ok;
}

static MPXP_Rc __FASTCALL__ control_af(af_instance_t* af, int cmd, any_t* arg)
{
  af_raw_t* s = reinterpret_cast<af_raw_t*>(af->setup);

  switch (cmd){
  case AF_CONTROL_SHOWCONF:
    mpxp_info<<"[af_raw] in use "<<s->filename<<std::endl;
    return MPXP_Ok;
  case AF_CONTROL_COMMAND_LINE:
    s->filename=mp_strdup(reinterpret_cast<char*>(arg));
    return MPXP_Ok;
  }
  return MPXP_Unknown;
}

/* Free allocated memory and clean up other stuff too.
   af audio filter instance
*/
static void __FASTCALL__ uninit( af_instance_t* af )
{
  af_raw_t* s = reinterpret_cast<af_raw_t*>(af->setup);
  if(s) {
    if(s->fd.is_open()) {
	off_t pos = s->fd.tellp();
	if(s->wav_mode){ /* Write wave header */
	    s->fd.seekp(0, std::ios_base::beg);
	    s->wavhdr.file_length = pos-8;
	    s->wavhdr.file_length = le2me_32(s->wavhdr.file_length);
	    s->wavhdr.data_length = le2me_32(s->wavhdr.data_length);
	    s->fd.write((char*)(&s->wavhdr),sizeof(struct WaveHeader));
	    s->fd.seekp(pos, std::ios_base::beg);
	}
	s->fd.close();
    }
    if(s->filename) delete s->filename;
    delete s;
    af->setup = NULL;
  }
}

/* Filter data through filter
   af audio filter instance
   data audio data
*/
static mp_aframe_t __FASTCALL__ play( af_instance_t* af,const mp_aframe_t& ind)
{
  af_raw_t*	s   = reinterpret_cast<af_raw_t*>(af->setup); // Setup for this instance
  if(s->fd.is_open()) s->fd.write((char*)(ind.audio),ind.len);
  s->wavhdr.data_length += ind.len;
  // We don't modify data, just export it
  return ind;
}

/* Allocate memory and set function pointers
   af audio filter instance
   returns MPXP_Ok or MPXP_Error
*/
static MPXP_Rc __FASTCALL__ af_open( af_instance_t* af )
{
  af->config_af = config_af;
  af->control_af = control_af;
  af->uninit  = uninit;
  af->play    = play;
  af->mul.n   = 1;
  af->mul.d   = 1;
  af->setup   = new(zeromem) af_raw_t;

  af_raw_t* s = reinterpret_cast<af_raw_t*>(af->setup);
  s->filename = mp_strdup("1.wav");
    check_pin("afilter",af->pin,AF_PIN);
  return MPXP_Ok;
}

// Description of this filter
extern const af_info_t af_info_raw = {
    "Raw data output filter",
    "raw",
    "Nickols_K",
    "",
    AF_FLAGS_REENTRANT,
    af_open
};
