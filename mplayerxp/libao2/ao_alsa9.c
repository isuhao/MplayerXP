/*
  ao_alsa9 - ALSA-0.9.x output plugin for MPlayer

  (C) Alex Beregszaszi <alex@naxine.org>
  
  modified for real alsa-0.9.0-support by Joy Winter <joy@pingfm.org>
  additional AC3 passthrough support by Andy Lo A Foe <andy@alsaplayer.org>  
  08/22/2002 iec958-init rewritten and merged with common init, joy
  
  Any bugreports regarding to this driver are welcome.
*/

#include <errno.h>
#include <sys/time.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <sys/poll.h>

#include "../config.h"
#include "../mplayer.h"
#define ALSA_PCM_NEW_HW_PARAMS_API
#define ALSA_PCM_NEW_SW_PARAMS_API
#include <alsa/asoundlib.h>

#include "audio_out.h"
#include "audio_out_internal.h"
#include "afmt.h"
#include "ao_msg.h"
#include "../libmpdemux/mrl.h"

static ao_info_t info = 
{
    "ALSA-1.x audio output",
    "alsa",
    "Alex Beregszaszi <alex@naxine.org>, Joy Winter <joy@pingfm.org>",
    "under developement"
};

LIBAO_EXTERN(alsa)


static snd_pcm_t *alsa_handler;
static snd_pcm_format_t alsa_format;
static snd_pcm_hw_params_t *alsa_hwparams;
static snd_pcm_sw_params_t *alsa_swparams;

/* possible 4096, original 8192 
 * was only needed for calculating chunksize? */
//static int alsa_fragsize = 4096;
/* 16 sets buffersize to 16 * chunksize is as default 1024
 * which seems to be good avarge for most situations 
 * so buffersize is 16384 frames by default */
static int alsa_fragcount = 16;
static int chunk_size = 1024; //is alsa_fragsize / 4

#define MIN_CHUNK_SIZE 1024

static size_t bits_per_sample, bytes_per_sample, bits_per_frame;
static size_t chunk_bytes;

static int ao_noblock = 0;
int first = 1;

static int open_mode;
static int set_block_mode;

#define ALSA_DEVICE_SIZE 48

#undef BUFFERTIME
#define SET_CHUNKSIZE
#undef USE_POLL

static int __FASTCALL__ fmt2alsa(int format)
{
    switch (format)
      {
      case AFMT_S8:
	return SND_PCM_FORMAT_S8;
	break;
      case AFMT_U8:
	return SND_PCM_FORMAT_U8;
	break;
      case AFMT_U16_LE:
	return SND_PCM_FORMAT_U16_LE;
	break;
      case AFMT_U16_BE:
	return SND_PCM_FORMAT_U16_BE;
	break;
#ifndef WORDS_BIGENDIAN
      case AFMT_AC3:
#endif
      case AFMT_S16_LE:
	return SND_PCM_FORMAT_S16_LE;
	break;
#ifdef WORDS_BIGENDIAN
      case AFMT_AC3:
#endif
      case AFMT_S16_BE:
	return SND_PCM_FORMAT_S16_BE;
	break;
      case AFMT_S32_LE:
	return SND_PCM_FORMAT_S32_LE;
	break;
      case AFMT_S32_BE:
	return SND_PCM_FORMAT_S32_BE;
	break;
      case AFMT_U32_LE:
	return SND_PCM_FORMAT_U32_LE;
	break;
      case AFMT_U32_BE:
	return SND_PCM_FORMAT_U32_BE;
	break;
      case AFMT_S24_LE:
	return SND_PCM_FORMAT_S24_LE;
	break;
      case AFMT_S24_BE:
	return SND_PCM_FORMAT_S24_BE;
	break;
      case AFMT_U24_LE:
	return SND_PCM_FORMAT_U24_LE;
	break;
      case AFMT_U24_BE:
	return SND_PCM_FORMAT_U24_BE;
	break;
      case AFMT_FLOAT32:
#ifdef WORDS_BIGENDIAN
	return SND_PCM_FORMAT_FLOAT_BE;
#else
	return SND_PCM_FORMAT_FLOAT_LE;
#endif
	break;
      default:
	return SND_PCM_FORMAT_MPEG;
	break;
      }
}

static void alsa_error_handler(const char *file, int line, const char *function,
			       int err, const char *format, ...)
{
  char tmp[0xc00];
  va_list va;

  va_start(va, format);
  vsnprintf(tmp, sizeof tmp, format, va);
  va_end(va);
  tmp[sizeof tmp - 1] = '\0';

  if (err)
    MSG_ERR("[AO_ALSA] alsa-lib: %s:%i:(%s) %s: %s\n",
	   file, line, function, tmp, snd_strerror(err));
  else
    MSG_ERR("[AO_ALSA] alsa-lib: %s:%i:(%s) %s\n",
	   file, line, function, tmp);
}

/* to set/get/query special features/parameters */
static int __FASTCALL__ control(int cmd, long arg)
{
  int rval;
  switch(cmd) {
	case AOCONTROL_QUERY_FORMAT:
	    rval=fmt2alsa(arg);
	    return snd_pcm_hw_params_test_format(alsa_handler, alsa_hwparams,rval)==0?
		    CONTROL_TRUE:CONTROL_FALSE;
	case AOCONTROL_QUERY_CHANNELS:
	    rval=arg;
	    return snd_pcm_hw_params_test_channels(alsa_handler, alsa_hwparams,rval)==0?
		    CONTROL_TRUE:CONTROL_FALSE;
	case AOCONTROL_QUERY_RATE:
	    rval=arg;
	    return snd_pcm_hw_params_test_rate(alsa_handler, alsa_hwparams,rval,0)==0?
		    CONTROL_TRUE:CONTROL_FALSE;
  case AOCONTROL_GET_VOLUME:
  case AOCONTROL_SET_VOLUME:
#ifndef WORDS_BIGENDIAN 
{ //seems to be a problem on macs?
    {
      ao_control_vol_t *vol = (ao_control_vol_t *)arg;

      int err;
      snd_mixer_t *handle;
      snd_mixer_elem_t *elem;
      snd_mixer_selem_id_t *sid;

      const char *mix_name = "PCM";
      char *card = "default";

      long pmin, pmax;
      long get_vol, set_vol;
      float calc_vol, diff, f_multi;

      if(ao_data.format == AFMT_AC3)
	return CONTROL_TRUE;

      //allocate simple id
      snd_mixer_selem_id_alloca(&sid);
	
      //sets simple-mixer index and name
      snd_mixer_selem_id_set_index(sid, 0);
      snd_mixer_selem_id_set_name(sid, mix_name);

      if ((err = snd_mixer_open(&handle, 0)) < 0) {
	MSG_ERR("alsa-control: mixer open error: %s\n", snd_strerror(err));
	return CONTROL_ERROR;
      }

      if ((err = snd_mixer_attach(handle, card)) < 0) {
	MSG_ERR("alsa-control: mixer attach %s error: %s", card, snd_strerror(err));
	snd_mixer_close(handle);
	return CONTROL_ERROR;
      }

      if ((err = snd_mixer_selem_register(handle, NULL, NULL)) < 0) {
	MSG_ERR("alsa-control: mixer register error: %s", snd_strerror(err));
	snd_mixer_close(handle);
	return CONTROL_ERROR;
      }
      if ((err = snd_mixer_load(handle)) < 0) {
	MSG_ERR("alsa-control: mixer load error: %s", snd_strerror(err));
	snd_mixer_close(handle);
	return CONTROL_ERROR;
      }

      elem = snd_mixer_find_selem(handle, sid);
      if (!elem) {
	MSG_ERR("alsa-control: unable to find simple control '%s',%i\n", snd_mixer_selem_id_get_name(sid), snd_mixer_selem_id_get_index(sid));
	snd_mixer_close(handle);
	return CONTROL_ERROR;
	}

      snd_mixer_selem_get_playback_volume_range(elem,&pmin,&pmax);
      f_multi = (100 / (float)pmax);

      if (cmd == AOCONTROL_SET_VOLUME) {

	diff = (vol->left+vol->right) / 2;
	set_vol = rint(diff / f_multi);
	
	if (set_vol < 0)
	  set_vol = 0;
	else if (set_vol > pmax)
	  set_vol = pmax;

	//setting channels
	if ((err = snd_mixer_selem_set_playback_volume(elem, 0, set_vol)) < 0) {
	  MSG_ERR("alsa-control: error setting left channel, %s",snd_strerror(err));
	  return CONTROL_ERROR;
	}
	if ((err = snd_mixer_selem_set_playback_volume(elem, 1, set_vol)) < 0) {
	  MSG_ERR("alsa-control: error setting right channel, %s",snd_strerror(err));
	  return CONTROL_ERROR;
	}

	//printf("diff=%f, set_vol=%i, pmax=%i, mult=%f\n", diff, set_vol, pmax, f_multi);
      }
      else {
	snd_mixer_selem_get_playback_volume(elem, 0, &get_vol);
	calc_vol = get_vol;
	calc_vol = rintf(calc_vol * f_multi);

	vol->left = vol->right = (int)calc_vol;

	//printf("get_vol = %i, calc=%i\n",get_vol, calc_vol);
      }
      snd_mixer_close(handle);
      return CONTROL_OK;
    }
}// end big-endian
#endif
#ifdef WORDS_BIGENDIAN
{
  {
    return (CONTROL_UNKNOWN);
  }
}
#endif
    
  } //end witch
  return(CONTROL_UNKNOWN);
}

static void __FASTCALL__ show_caps(unsigned device)
{
    snd_pcm_info_t *alsa_info;
    snd_pcm_t *pcm;
    snd_pcm_hw_params_t *hw_params;
    snd_output_t *sout;
    int err,cards=-1;
    unsigned rmin,rmax;
    unsigned j,sdmin,sdmax;
    char adevice[ALSA_DEVICE_SIZE];
    if ((err = snd_card_next(&cards)) < 0 || cards < 0)
    {
	MSG_ERR("AO-INFO: alsa-init: no soundcards found: %s\n", snd_strerror(err));
	return;
    }
    snd_pcm_info_malloc(&alsa_info);
    snd_pcm_info_set_device(alsa_info,device);
    sdmin=snd_pcm_info_get_subdevice(alsa_info);
    sdmax=sdmin+snd_pcm_info_get_subdevices_count(alsa_info);
    MSG_INFO("AO-INFO: show caps for device %i:%i-%i\n",device,sdmin,sdmax);
    for(j=sdmin;j<=sdmax;j++)
    {
	int i;
	snd_pcm_info_set_subdevice(alsa_info,j);
	sprintf(adevice,"hw:%u,%u",snd_pcm_info_get_device(alsa_info),snd_pcm_info_get_subdevice(alsa_info));
	MSG_INFO("AO-INFO: %s %s.%s.%s\n\n",adevice,snd_pcm_info_get_id(alsa_info),snd_pcm_info_get_name(alsa_info),snd_pcm_info_get_subdevice_name(alsa_info));
	if(snd_pcm_open(&pcm,adevice,SND_PCM_STREAM_PLAYBACK,SND_PCM_NONBLOCK)<0)
	{
	    MSG_ERR("alsa-init: playback open error: %s\n", snd_strerror(err));
	    return;
	}
	snd_pcm_hw_params_malloc(&hw_params);
	if(snd_pcm_hw_params_any(pcm, hw_params)<0)
	{
	    MSG_ERR("alsa-init: can't get initial parameters: %s\n", snd_strerror(err));
	    return;
	}
	MSG_INFO("    AO-INFO: List of access type: ");
	for(i=0;i<SND_PCM_ACCESS_LAST;i++)
	    if(!snd_pcm_hw_params_test_access(pcm,hw_params,i))
		MSG_INFO("%s ",snd_pcm_access_name(i));
	MSG_INFO("\n");
	MSG_INFO("    AO-INFO: List of supported formats: ");
	for(i=0;i<SND_PCM_FORMAT_LAST;i++)
	    if(!snd_pcm_hw_params_test_format(pcm,hw_params,i))
		MSG_INFO("%s ",snd_pcm_format_name(i));
	MSG_INFO("\n");
	MSG_INFO("    AO-INFO: List of supported channels: ");
	for(i=0;i<64;i++)
	    if(!snd_pcm_hw_params_test_format(pcm,hw_params,i))
		MSG_INFO("%u ",i);
	MSG_INFO("\n");
	snd_pcm_hw_params_get_rate_min(hw_params,&rmin,&err);
	snd_pcm_hw_params_get_rate_max(hw_params,&rmax,&err);
	MSG_INFO("    AO-INFO: Rates range: %u %u\n",rmin,rmax);
	snd_output_stdio_attach(&sout, stderr, 0);
	snd_pcm_hw_params_dump(hw_params, sout);
	if(hw_params) snd_pcm_hw_params_free(hw_params);
	if(pcm) snd_pcm_close(pcm);
    }
    snd_pcm_info_free(alsa_info);
}


static mrl_config_t alsaconf[]={
    { "noblock", &ao_noblock, MRL_TYPE_BOOL, 0, 1 },
    { NULL, NULL, 0, 0, 0 }
};
/*
    open & setup audio device
    return: 1=success 0=fail
*/
static int __FASTCALL__ init(int flags)
{
    int err;
    int cards = -1;
    int period_val;
    snd_pcm_info_t *alsa_info;
    char *str_block_mode;
    char *alsa_dev=NULL;
    char *alsa_port=NULL;
    char alsa_device[ALSA_DEVICE_SIZE];

    alsa_handler = NULL;
    alsa_device[0]='\0';

    MSG_V("alsa-init: compiled for ALSA-%s\n", SND_LIB_VERSION_STR);

    if (ao_subdevice) {
	const char *param;
	char *p;
	param=mrl_parse_line(ao_subdevice,NULL,NULL,&alsa_dev,&alsa_port);
	mrl_parse_params(param,alsaconf);
	if(alsa_port) {
	p=strchr(alsa_port,',');
	if(p)
	{
	    if(strcmp(p+1,"-1")==0)
	    {
		*p='\0';
		show_caps(atoi(alsa_port));
		return 0;
	    }
	}
	if(alsa_port) snprintf(alsa_device,sizeof(alsa_device),"%s:%s",alsa_dev,alsa_port);
	else	      strncpy(alsa_device,alsa_dev,sizeof(alsa_device));
	MSG_V("alsa-init: soundcard set to %s\n", alsa_device);
    } //end parsing ao_subdevice
    }

    if ((err = snd_card_next(&cards)) < 0 || cards < 0)
    {
	MSG_ERR("alsa-init: no soundcards found: %s\n", snd_strerror(err));
	return 0;
    }

    snd_lib_error_set_handler(alsa_error_handler);

    if (alsa_device[0] == '\0')
    {
	int tmp_device, tmp_subdevice, err;

	if ((err = snd_pcm_info_malloc(&alsa_info)) < 0)
	{
	    MSG_ERR("alsa-init: memory allocation error: %s\n", snd_strerror(err));
	    return 0;
	}
	
	if ((tmp_device = snd_pcm_info_get_device(alsa_info)) < 0)
	{
	    MSG_ERR("alsa-init: cant get device\n");
	    return 0;
	}

	if ((tmp_subdevice = snd_pcm_info_get_subdevice(alsa_info)) < 0)
	{
	    MSG_ERR("alsa-init: cant get subdevice\n");
	    return 0;
	}
	
	MSG_V("alsa-init: got device=%i, subdevice=%i\n", tmp_device, tmp_subdevice);

	if ((err = snprintf(alsa_device, ALSA_DEVICE_SIZE, "hw:%1d,%1d", tmp_device, tmp_subdevice)) <= 0)
	{
	    MSG_ERR("alsa-init: cant wrote device-id\n");
	}

	snd_pcm_info_free(alsa_info);
    }

    MSG_WARN("alsa-init: Testing & bugs are welcome. Found %d cards, use: %s\n",cards+1,alsa_device);
    //setting modes for block or nonblock-mode
    if (ao_noblock) {
      open_mode = SND_PCM_NONBLOCK;
      set_block_mode = 1;
      str_block_mode = "nonblock-mode";
    }
    else {
      open_mode = 0;
      set_block_mode = 0;
      str_block_mode = "block-mode";
    }

    //sets buff/chunksize if its set manually
    if (ao_data.buffersize) {
      switch (ao_data.buffersize)
	{
	case 1:
	  alsa_fragcount = 16;
	  chunk_size = 512;
	  MSG_V("alsa-init: buffersize set manually to 8192\n"
		"alsa-init: chunksize set manually to 512\n");
	  break;
	case 2:
	  alsa_fragcount = 8;
	  chunk_size = 1024;
	  MSG_V("alsa-init: buffersize set manually to 8192\n"
		"alsa-init: chunksize set manually to 1024\n");
	  break;
	case 3:
	  alsa_fragcount = 32;
	  chunk_size = 512;
	  MSG_V("alsa-init: buffersize set manually to 16384\n"
		"alsa-init: chunksize set manually to 512\n");
	  break;
	case 4:
	  alsa_fragcount = 16;
	  chunk_size = 1024;
	  MSG_V("alsa-init: buffersize set manually to 16384\n"
		"alsa-init: chunksize set manually to 1024\n");
	  break;
	default:
	  alsa_fragcount = 16;
	  chunk_size = 1024;
	  break;
	}
    }

    if (!alsa_handler) {
	//modes = 0, SND_PCM_NONBLOCK, SND_PCM_ASYNC
	if ((err = snd_pcm_open(&alsa_handler, alsa_device, SND_PCM_STREAM_PLAYBACK, open_mode)) < 0)
	{
	  if (ao_noblock) {
		MSG_ERR("alsa-init: open in nonblock-mode failed, trying to open in block-mode\n");
		if ((err = snd_pcm_open(&alsa_handler, alsa_device, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
		    MSG_ERR("alsa-init: playback open error: %s\n", snd_strerror(err));
		    alsa_device[0]='\0';
		    return 0;
		} else {
		    set_block_mode = 0;
		    str_block_mode = "block-mode";
		}
	  } else {
		MSG_ERR("alsa-init: playback open error: %s\n", snd_strerror(err));
		alsa_device[0]='\0';
		return 0;
	  }
	}
      alsa_device[0]='\0';
      if ((err = snd_pcm_nonblock(alsa_handler, set_block_mode)) < 0) {
	MSG_ERR("alsa-init: error set block-mode %s\n", snd_strerror(err));
      }
      else MSG_V("alsa-init: pcm opend in %s\n", str_block_mode);
      
      snd_pcm_hw_params_malloc(&alsa_hwparams);
      snd_pcm_sw_params_malloc(&alsa_swparams);

      // setting hw-parameters
      if ((err = snd_pcm_hw_params_any(alsa_handler, alsa_hwparams)) < 0)
	{
	  MSG_ERR("alsa-init: unable to get initial parameters: %s\n",
		 snd_strerror(err));
	  return 0;
	}

	err = snd_pcm_hw_params_set_access(alsa_handler, alsa_hwparams,SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0) {
	    MSG_ERR("alsa-init: unable to set access type: %s\n", snd_strerror(err));
	    return 0;
      }

#ifdef BUFFERTIME
      {
	int alsa_buffer_time = 500000; /* original 60 */

	if ((err = snd_pcm_hw_params_set_buffer_time_near(alsa_handler, alsa_hwparams, alsa_buffer_time, 0)) < 0)
	  {
	    MSG_ERR("alsa-init: unable to set buffer time near: %s\n",
		   snd_strerror(err));
	    return 0;
	  } else
	    alsa_buffer_time = err;

	if ((err = snd_pcm_hw_params_set_period_time_near(alsa_handler, alsa_hwparams, alsa_buffer_time/4, 0)) < 0)
	  /* original: alsa_buffer_time/ao_data.bps */
	  {
	    MSG_ERR("alsa-init: unable to set period time: %s\n",
		   snd_strerror(err));
	    return 0;
	  }
	MSG_V("alsa-init: buffer_time: %d, period_time :%d\n",alsa_buffer_time, err);
      }
#endif

#ifdef SET_CHUNKSIZE
      {
	//set chunksize
	if ((err = snd_pcm_hw_params_set_period_size(alsa_handler, alsa_hwparams, chunk_size, 0)) < 0)
	  {
	    MSG_ERR("alsa-init: unable to set periodsize: %s\n", snd_strerror(err));
	    return 0;
	  }
	else MSG_V("alsa-init: chunksize set to %i\n", chunk_size);

	//set period_count
	snd_pcm_hw_params_get_periods_max(alsa_hwparams,&period_val, 0);
	if (period_val < alsa_fragcount) {
	  alsa_fragcount = period_val;			
	}

	MSG_V("alsa-init: current val=%i, fragcount=%i\n", period_val, alsa_fragcount);

	if ((err = snd_pcm_hw_params_set_periods(alsa_handler, alsa_hwparams, alsa_fragcount, 0)) < 0) {
	  MSG_ERR("alsa-init: unable to set periods: %s\n", snd_strerror(err));
	}
      }
#endif


    } // end switch alsa_handler (spdif)
    return 1;
} // end init

static int __FASTCALL__ configure(int rate_hz, int channels, int format)
{
    int err,i;
    snd_pcm_uframes_t dummy;

    MSG_V("alsa-conf: requested format: %d Hz, %d channels, %s\n", rate_hz,
	channels, ao_format_name(format));

    ao_data.samplerate = rate_hz;
    ao_data.bps = channels * rate_hz;
    ao_data.format = format;
    ao_data.channels = channels;
    ao_data.outburst = OUTBURST;
    //ao_data.buffersize = MAX_OUTBURST; // was 16384

    alsa_format=fmt2alsa(format);

    switch(alsa_format)
      {
      case SND_PCM_FORMAT_S16_LE:
      case SND_PCM_FORMAT_U16_LE:
      case SND_PCM_FORMAT_S16_BE:
      case SND_PCM_FORMAT_U16_BE:
	ao_data.bps *= 2;
	break;
      case SND_PCM_FORMAT_S32_LE:
      case SND_PCM_FORMAT_S32_BE:
      case SND_PCM_FORMAT_U32_LE:
      case SND_PCM_FORMAT_U32_BE:
      case SND_PCM_FORMAT_FLOAT_BE:
      case SND_PCM_FORMAT_FLOAT_LE:
	ao_data.bps *= 4;
	break;
      case SND_PCM_FORMAT_S24_LE:
      case SND_PCM_FORMAT_S24_BE:
      case SND_PCM_FORMAT_U24_LE:
      case SND_PCM_FORMAT_U24_BE:
	ao_data.bps *= 3;
	break;
      case -1:
	MSG_ERR("alsa-conf: invalid format (%s) requested - output disabled\n",
	       ao_format_name(format));
	return 0;
      default:
	break;
      }
    bytes_per_sample = ao_data.bps / ao_data.samplerate;

      if ((err = snd_pcm_hw_params_set_format(alsa_handler, alsa_hwparams,
					      alsa_format)) < 0)
	{
	  MSG_ERR("alsa-conf: unable to set format(%s): %s\n",
		 snd_pcm_format_name(alsa_format),
		 snd_strerror(err));
	  MSG_HINT("Please try one of: ");
	  for(i=0;i<SND_PCM_FORMAT_LAST;i++) 
	    if (!(snd_pcm_hw_params_test_format(alsa_handler, alsa_hwparams, i)))
		MSG_HINT("%s ",snd_pcm_format_name(i));
	  MSG_HINT("\n");
	  return 0;
	}

      if ((err = snd_pcm_hw_params_set_rate_near(alsa_handler, alsa_hwparams, &ao_data.samplerate, 0)) < 0) 
        {
	  MSG_ERR("alsa-conf: unable to set samplerate %u: %s\n",
		 ao_data.samplerate,
		 snd_strerror(err));
	  return 0;
        }

      if ((err = snd_pcm_hw_params_set_channels(alsa_handler, alsa_hwparams,
						ao_data.channels)) < 0)
	{
	  MSG_ERR("alsa-conf: unable to set %u channels: %s\n",
		 ao_data.channels,
		 snd_strerror(err));
	  return 0;
	}

        // gets buffersize for control
        if ((snd_pcm_hw_params_get_buffer_size(alsa_hwparams,&dummy)) < 0)
	{
	    MSG_ERR("alsa-conf: unable to get buffersize: %s\n", snd_strerror(err));
	    return 0;
	}
        else {
	    ao_data.buffersize = dummy * bytes_per_sample;
	    MSG_V("alsa-conf: got buffersize=%i\n", ao_data.buffersize);
        }
	bits_per_sample = snd_pcm_format_physical_width(alsa_format);
	bits_per_frame = bits_per_sample * channels;
	chunk_bytes = chunk_size * bits_per_frame / 8;

	MSG_V("alsa-conf: bits per sample (bps)=%i, bits per frame (bpf)=%i, chunk_bytes=%i\n",bits_per_sample,bits_per_frame,chunk_bytes);

      /* finally install hardware parameters */
      if ((err = snd_pcm_hw_params(alsa_handler, alsa_hwparams)) < 0)
	{
	  MSG_ERR("alsa-conf: unable to set hw-parameters: %s\n",
		 snd_strerror(err));
	  return 0;
	}
      // setting sw-params (only avail-min) if noblocking mode was choosed
      if (ao_noblock)
	{

	  if ((err = snd_pcm_sw_params_current(alsa_handler, alsa_swparams)) < 0)
	    {
	      MSG_ERR("alsa-conf: unable to get parameters: %s\n",snd_strerror(err));
	      return 0;
	    }

	  //set min available frames to consider pcm ready (4)
	  //increased for nonblock-mode should be set dynamically later
	  if ((err = snd_pcm_sw_params_set_avail_min(alsa_handler, alsa_swparams, 4)) < 0)
	    {
	      MSG_ERR("alsa-conf: unable to set avail_min %s\n",snd_strerror(err));
	      return 0;
	    }

	  if ((err = snd_pcm_sw_params(alsa_handler, alsa_swparams)) < 0)
	    {
	      MSG_ERR("alsa-conf: unable to install sw-params\n");
	      return 0;
	    }

	}//end swparams
      if ((err = snd_pcm_prepare(alsa_handler)) < 0)
	{
	  MSG_ERR("alsa-conf: pcm prepare error: %s\n", snd_strerror(err));
	  return 0;
	}
      // end setting hw-params
      MSG_V("alsa-conf: %d Hz/%d channels/%d bpf/%d bytes buffer/%s\n",
	     ao_data.samplerate, ao_data.channels, bytes_per_sample, ao_data.buffersize,
	     snd_pcm_format_description(alsa_format));
    return 1;
} // end init


/* close audio device */
static void uninit(void)
{

  if (alsa_handler) {
    int err;

    if (!ao_noblock) {
      if ((err = snd_pcm_drain(alsa_handler)) < 0)
	{
	  MSG_ERR("alsa-uninit: pcm drain error: %s\n", snd_strerror(err));
	  return;
	}
    }

    if ((err = snd_pcm_close(alsa_handler)) < 0)
      {
	MSG_ERR("alsa-uninit: pcm close error: %s\n", snd_strerror(err));
	return;
      }
    else {
      alsa_handler = NULL;
      MSG_V("alsa-uninit: pcm closed\n");
    }
  }
  else {
    MSG_ERR("alsa-uninit: no handler defined!\n");
  }
  snd_pcm_hw_params_free(alsa_hwparams);
  snd_pcm_sw_params_free(alsa_swparams);
}

static void audio_pause(void)
{
    int err;

    if (!ao_noblock) {
      //drain causes error in nonblock-mode!
      if ((err = snd_pcm_drop(alsa_handler)) < 0)
	{
	  MSG_ERR("alsa-pause: pcm drain error: %s\n", snd_strerror(err));
	  return;
	}
    }
    else {
      MSG_V("alsa-pause: paused nonblock\n");

      return;
    }
}

static void audio_resume(void)
{
    int err;

    if (snd_pcm_state(alsa_handler) == SND_PCM_STATE_SUSPENDED) {
	MSG_WARN("ao_alsa: PCM in suspened mode. Trying resume\n");
	while ((err = snd_pcm_resume(alsa_handler)) == -EAGAIN) sleep(1);
    }
    if ((err = snd_pcm_prepare(alsa_handler)) < 0)
    {
	MSG_ERR("alsa-resume: pcm prepare error: %s\n", snd_strerror(err));
	return;
    }
}

/* stop playing and empty buffers (for seeking/pause) */
static void reset(void)
{
    int err;

    if ((err = snd_pcm_drop(alsa_handler)) < 0)
    {
	MSG_ERR("alsa-reset: pcm drop error: %s\n", snd_strerror(err));
	return;
    }
    if ((err = snd_pcm_prepare(alsa_handler)) < 0)
    {
	MSG_ERR("alsa-reset: pcm prepare error: %s\n", snd_strerror(err));
	return;
    }
    return;
}

/*
    plays 'len' bytes of 'data'
    returns: number of bytes played
    modified last at 29.06.02 by jp
    thanxs for marius <marius@rospot.com> for giving us the light ;)
*/

static int __FASTCALL__ play(void* data, int len, int flags)
{
  int num_frames;
  snd_pcm_sframes_t res = 0;
  len = len / ao_data.outburst * ao_data.outburst;
  num_frames = len / bytes_per_sample;

  //mp_msg(MSGT_AO,MSGL_ERR,"alsa-play: frames=%i, len=%i\n",num_frames,len);

  if (!alsa_handler) {
    MSG_ERR("alsa: Device configuration error\n");
    return 0;
  }

  if (num_frames == 0)
    return 0;

  do {
    res = snd_pcm_writei(alsa_handler, data, num_frames);

      if (res == -EINTR) {
	/* nothing to do */
	res = 0;
      }
      else if (res == -ESTRPIPE) {	/* suspend */
	MSG_INFO("alsa: PCM in suspend mode. Trying resume\n");
	while ((res = snd_pcm_resume(alsa_handler)) == -EAGAIN)
	  sleep(1);
      }
      if (res < 0) {
	MSG_ERR("alsa: WriteError %s\n", snd_strerror(res));
	if ((res = snd_pcm_prepare(alsa_handler)) < 0) {
	  MSG_ERR("alsa: PcmPrepareError %s\n", snd_strerror(res));
	  return 0;
	  break;
	}
      }
  } while (res == 0);

  return res < 0 ? res : res * bytes_per_sample;
}

/* how many byes are free in the buffer */
static int get_space(void)
{
    snd_pcm_status_t *status;
    int ret;

    snd_pcm_status_alloca(&status);

    if ((ret = snd_pcm_status(alsa_handler, status)) < 0)
    {
	MSG_ERR("ao_alsa: cannot get PCM status: %s\n", snd_strerror(ret));
	return 0;
    }

    ret = snd_pcm_status_get_avail(status) * bytes_per_sample;
    if (ret > ao_data.buffersize)  // Buffer underrun?
	ret = ao_data.buffersize;
    return ret;
}

/* delay in seconds between first and last sample in buffer */
static float get_delay(void)
{
  if (alsa_handler) {
    snd_pcm_sframes_t delay;

    if (snd_pcm_delay(alsa_handler, &delay) < 0)
      return 0;

    if (delay < 0) {
      /* underrun - move the application pointer forward to catch up */
#if SND_LIB_VERSION >= 0x000901 /* snd_pcm_forward() exists since 0.9.0rc8 */
      snd_pcm_forward(alsa_handler, -delay);
#endif
      delay = 0;
    }
    return (float)delay / (float)ao_data.samplerate;
  } else {
    return 0;
  }
}
