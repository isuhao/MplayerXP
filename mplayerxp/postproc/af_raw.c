/*
  This audio filter exports the incoming signal to raw or RIFF WAVE file
  TODO: add length + pts to export into sockets
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include "mp_config.h"

#include "osdep/bswap.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "af.h"
#include "help_mp.h"
#include "osdep/get_path.h"
#include "osdep/mplib.h"

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
typedef struct af_raw_s
{
  char* filename;      	// File to export data
  int wav_mode;
  struct WaveHeader wavhdr;
  FILE *fd;
} af_raw_t;

/* Initialization and runtime control
   af audio filter instance
   cmd control command
   arg argument
*/
static int __FASTCALL__ control(struct af_instance_s* af, int cmd, any_t* arg)
{
  af_raw_t* s = af->setup;

  switch (cmd){
  case AF_CONTROL_REINIT:{
    char *pt;
    // Accepts any streams
    memcpy(af->data,(af_data_t*)arg,sizeof(af_data_t));
    if(!s->fd) { /* reenterability */
	if(!(s->fd=fopen(s->filename,"wb")))
	    MSG_ERR("Can't open %s\n",s->filename);
	pt=strchr(s->filename,'.');
	s->wav_mode=0;
	if(pt) if(strcmp(pt+1,"wav")==0) s->wav_mode=1;
	if(s->wav_mode && s->fd)
	{
	    uint16_t fmt=af->data->format>>16;
	    if(!fmt) fmt=0x01; /* pcm */
	    s->wavhdr.riff=le2me_32(WAV_ID_RIFF);
	    s->wavhdr.file_length = le2me_32(0x7ffff000) + sizeof(struct WaveHeader) - 8;
	    s->wavhdr.wave=le2me_32(WAV_ID_WAVE);
	    s->wavhdr.fmt=le2me_32(WAV_ID_FMT);
	    s->wavhdr.fmt_length=le2me_32(16);
	    s->wavhdr.fmt_tag = le2me_16(fmt);
	    s->wavhdr.channels = le2me_16(af->data->nch);
	    s->wavhdr.sample_rate = le2me_32(af->data->rate);
	    s->wavhdr.bytes_per_second = le2me_32(af->data->bps*af->data->rate*af->data->nch);
	    s->wavhdr.block_align = le2me_16(af->data->nch * af->data->bps);
	    s->wavhdr.bits = le2me_16(af->data->bps*8);
	    s->wavhdr.data=le2me_32(WAV_ID_DATA);
	    s->wavhdr.data_length=le2me_32(0x7ffff000);
	    fwrite(&s->wavhdr,sizeof(struct WaveHeader),1,s->fd);
	    s->wavhdr.file_length=s->wavhdr.data_length=0;
	}
    }
    MSG_V("[af_raw] Was reinitialized, rate=%iHz, nch = %i, format = 0x%08X and bps = %i\n",af->data->rate,af->data->nch,af->data->format,af->data->bps);
    af->data->format=AF_FORMAT_SI | AF_FORMAT_NE; // fake! fixme !!!
    return AF_OK;
  }
  case AF_CONTROL_SHOWCONF:
    MSG_INFO("[af_raw] in use %s\n",s->filename);
    return AF_OK;
  case AF_CONTROL_COMMAND_LINE:
    s->filename=mp_strdup(arg);
    return AF_OK;
  }
  return AF_UNKNOWN;
}

/* Free allocated memory and clean up other stuff too.
   af audio filter instance
*/
static void __FASTCALL__ uninit( struct af_instance_s* af )
{
  af_raw_t* s = af->setup;
  if (af->data){
    mp_free(af->data);
    af->data = NULL;
  }

  if(s){
    if(s->fd)
    {
	off_t pos = ftello(s->fd);
	if(s->wav_mode){ /* Write wave header */
	    fseeko(s->fd, 0, SEEK_SET);
	    s->wavhdr.file_length = pos-8;
	    s->wavhdr.file_length = le2me_32(s->wavhdr.file_length);
	    s->wavhdr.data_length = le2me_32(s->wavhdr.data_length);
	    fwrite(&s->wavhdr,sizeof(struct WaveHeader),1,s->fd);
	    fseeko(s->fd, pos, SEEK_SET);
	}
	fclose(s->fd);
	s->fd=NULL;
    }
    if(s->filename) mp_free(s->filename);
    mp_free(af->setup);
    af->setup = NULL;
  }
}

/* Filter data through filter
   af audio filter instance
   data audio data
*/
static af_data_t* __FASTCALL__ play( struct af_instance_s* af, af_data_t* data,int final)
{
  af_raw_t* 	s   = af->setup;     // Setup for this instance
  if(s->fd) fwrite(data->audio,data->len,1,s->fd);
  s->wavhdr.data_length += data->len;
  // We don't modify data, just export it
  return data;
}

/* Allocate memory and set function pointers
   af audio filter instance 
   returns AF_OK or AF_ERROR
*/
static int __FASTCALL__ af_open( af_instance_t* af )
{
  af->control = control;
  af->uninit  = uninit;
  af->play    = play;
  af->mul.n   = 1;
  af->mul.d   = 1;
  af->data    = mp_calloc(1, sizeof(af_data_t));
  af->setup   = mp_calloc(1, sizeof(af_raw_t));
  if((af->data == NULL) || (af->setup == NULL))
    return AF_ERROR;

  ((af_raw_t *)af->setup)->filename = "1.wav";

  return AF_OK;
}

// Description of this filter
const af_info_t af_info_raw = {
    "Raw data output filter",
    "raw",
    "Nickols_K",
    "",
    AF_FLAGS_REENTRANT,
    af_open
};
