#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h> /* GLIBC specific. Exists under cygwin too! */
#include "ad_internal.h"
#include "mp_config.h"
#include "help_mp.h"
#include "loader/dshow/DS_AudioDecoder.h"
#include "codecs_ld.h"

static const ad_info_t info =
{
	"Win32 direct show audio",
	"dshow",
	"Nickols_K",
	""
};

static const config_t options[] = {
  { NULL, NULL, 0, 0, 0, 0, NULL}
};

LIBAD_EXTERN(dshow)

typedef struct dshow_priv_s
{
  float pts;
  DS_AudioDecoder* ds_adec;
}dshow_priv_t;

int init(sh_audio_t *sh)
{
  return 1;
}

int preinit(sh_audio_t *sh_audio)
{
  dshow_priv_t *priv;
  if(!(sh_audio->context=malloc(sizeof(dshow_priv_t)))) return 0;
  priv=sh_audio->context;
  if(!(priv->ds_adec=DS_AudioDecoder_Open(sh_audio->codec->dll_name,&sh_audio->codec->guid,sh_audio->wf)))
  {
    MSG_ERR(MSGTR_MissingDLLcodec,sh_audio->codec->dll_name);
    free(sh_audio->context);
    return 0;
  }
  sh_audio->i_bps=sh_audio->wf->nAvgBytesPerSec;
  sh_audio->channels=sh_audio->wf->nChannels;
  sh_audio->samplerate=sh_audio->wf->nSamplesPerSec;
  sh_audio->audio_in_minsize=2*sh_audio->wf->nBlockAlign;
  if(sh_audio->audio_in_minsize<8192) sh_audio->audio_in_minsize=8192;
  sh_audio->audio_out_minsize=16384;
  MSG_V("INFO: Win32/DShow init OK!\n");
  return 1;
}

void uninit(sh_audio_t *sh)
{
  dshow_priv_t* priv = sh->context;
  DS_AudioDecoder_Destroy(priv->ds_adec);
  free(priv);
}

int control(sh_audio_t *sh_audio,int cmd,void* arg, ...)
{
  int skip;
    switch(cmd)
    {
      case ADCTRL_RESYNC_STREAM:
          sh_audio->a_in_buffer_len=0; // reset ACM/DShow audio buffer
	  return CONTROL_TRUE;
      case ADCTRL_SKIP_FRAME:
	{
		float pts;
		    skip=sh_audio->wf->nBlockAlign;
		    if(skip<16){
		      skip=(sh_audio->wf->nAvgBytesPerSec/16)&(~7);
		      if(skip<16) skip=16;
		    }
		    demux_read_data_r(sh_audio->ds,NULL,skip,&pts);
	}
	return CONTROL_TRUE;
      default:
	  return CONTROL_UNKNOWN;
    }
  return CONTROL_UNKNOWN;
}

int decode_audio(sh_audio_t *sh_audio,unsigned char *buf,int minlen,int maxlen,float *pts)
{
  dshow_priv_t* priv = sh_audio->context;
  int len=-1;
      { int size_in=0;
        int size_out=0;
        int srcsize=DS_AudioDecoder_GetSrcSize(priv->ds_adec, maxlen);
        MSG_DBG3("DShow says: srcsize=%d  (buffsize=%d)  out_size=%d\n",srcsize,sh_audio->a_in_buffer_size,maxlen);
        if(srcsize>sh_audio->a_in_buffer_size) srcsize=sh_audio->a_in_buffer_size; // !!!!!!
        if(sh_audio->a_in_buffer_len<srcsize){
          int l;
          l=demux_read_data_r(sh_audio->ds,&sh_audio->a_in_buffer[sh_audio->a_in_buffer_len],
            srcsize-sh_audio->a_in_buffer_len,pts);
            *pts=FIX_APTS(sh_audio,*pts,-sh_audio->a_in_buffer_len);
            sh_audio->a_in_buffer_len+=l;
	    priv->pts=*pts;
        }
	else *pts=priv->pts;
        DS_AudioDecoder_Convert(priv->ds_adec, sh_audio->a_in_buffer,sh_audio->a_in_buffer_len,
            buf,maxlen, &size_in,&size_out);
        MSG_DBG2("DShow: audio %d -> %d converted  (in_buf_len=%d of %d)  %d\n",size_in,size_out,sh_audio->a_in_buffer_len,sh_audio->a_in_buffer_size,ds_tell_pts_r(sh_audio->ds));
        if(size_in>=sh_audio->a_in_buffer_len){
          sh_audio->a_in_buffer_len=0;
        } else {
          sh_audio->a_in_buffer_len-=size_in;
          memcpy(sh_audio->a_in_buffer,&sh_audio->a_in_buffer[size_in],sh_audio->a_in_buffer_len);
	  priv->pts=FIX_APTS(sh_audio,priv->pts,size_in);
        }
        len=size_out;
      }
  return len;
}
