#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h> /* GLIBC specific. Exists under cygwin too! */
#define __AD_ACM
#include "ad_internal.h"
#include "codecs_ld.h"
#include "loader/wineacm.h"
#include "libmpdemux/aviprint.h"
#include "libao3/afmt.h"
#include "mpxp_help.h"

static const ad_info_t info = {
    "Win32/ACM decoders",
    "acm",
    "Nickols_K",
    "build-in"
};

static const config_t options[] = {
  { NULL, NULL, 0, 0, 0, 0, NULL}
};

LIBAD_EXTERN(msacm)

struct ad_private_t {
  float pts;
  WAVEFORMATEX o_wf;   // out format
  HACMSTREAM srcstream;  // handle
  sh_audio_t* sh;
};

static const audio_probe_t* __FASTCALL__ probe(uint32_t wtag) { return NULL; }

static int init_acm_audio_codec(ad_private_t *priv){
    sh_audio_t* sh_audio = priv->sh;
    HRESULT ret;
    WAVEFORMATEX *in_fmt=sh_audio->wf;
    unsigned int srcsize=0;

    MSG_V("======= Win32 (ACM) AUDIO Codec init =======\n");

    priv->srcstream=0;

    priv->o_wf.nChannels=sh_audio->nch;
    priv->o_wf.nSamplesPerSec=sh_audio->rate;
    priv->o_wf.nAvgBytesPerSec=2*priv->o_wf.nSamplesPerSec*priv->o_wf.nChannels;
    priv->o_wf.wFormatTag=WAVE_FORMAT_PCM;
    priv->o_wf.nBlockAlign=2*sh_audio->nch;
    priv->o_wf.wBitsPerSample=sh_audio->afmt?afmt2bps(sh_audio->afmt)*8:16;
    priv->o_wf.cbSize=0;
    if(!in_fmt) {
	in_fmt=sh_audio->wf=new(zeromem) WAVEFORMATEX;
	memcpy(in_fmt,&priv->o_wf,sizeof(WAVEFORMATEX));
	in_fmt->wFormatTag=sh_audio->wtag;
    }

    if(mp_conf.verbose) {
	MSG_V("Output fmt:\n");
	print_wave_header(&priv->o_wf,sizeof(WAVEFORMATEX));
    }
    MSACM_RegisterDriver((const char *)sh_audio->codec->dll_name, sh_audio->wtag, 0);
    ret=acmStreamOpen(&priv->srcstream,(HACMDRIVER)NULL,in_fmt,&priv->o_wf,NULL,0,0,0);
    if(ret){
	if(ret==ACMERR_NOTPOSSIBLE)
	    MSG_ERR("ACM_Decoder: Unappropriate audio format\n");
	else
	    MSG_ERR("ACM_Decoder: acmStreamOpen error: %d", (int)ret);
	priv->srcstream=0;
	return 0;
    }
    MSG_V("Audio codec opened OK! ;-)\n");

    acmStreamSize(priv->srcstream, in_fmt->nBlockAlign, (LPDWORD)&srcsize, ACM_STREAMSIZEF_SOURCE);
    srcsize*=2;
    //if(srcsize<MAX_OUTBURST) srcsize=MAX_OUTBURST;
    if(!srcsize){
	MSG_WARN("Warning! ACM codec reports srcsize=0\n");
	srcsize=16384;
    }
    // limit srcsize to 4-16kb
    //while(srcsize && srcsize<4096) srcsize*=2;
    //while(srcsize>16384) srcsize/=2;
    sh_audio->audio_out_minsize=srcsize; // audio output min. size
    MSG_V("Audio ACM output buffer min. size: %ld\n",srcsize);

    acmStreamSize(priv->srcstream, srcsize, (LPDWORD)&srcsize, ACM_STREAMSIZEF_DESTINATION);
    if(srcsize<in_fmt->nBlockAlign) srcsize=2*in_fmt->nBlockAlign;
    sh_audio->audio_in_minsize=srcsize; // audio input min. size
    MSG_V("Audio ACM input buffer min. size: %ld\n",srcsize);
    sh_audio->i_bps=sh_audio->wf->nAvgBytesPerSec;
    sh_audio->nch=priv->o_wf.nChannels;
    sh_audio->rate=priv->o_wf.nSamplesPerSec;
    sh_audio->afmt=bps2afmt((priv->o_wf.wBitsPerSample+7)/8);
    sh_audio->a_in_buffer_size=2*sh_audio->audio_in_minsize;
    sh_audio->a_in_buffer=new char [sh_audio->a_in_buffer_size];
    sh_audio->a_in_buffer_len=0;

    return 1;
}

static int close_acm_audio_codec(ad_private_t *priv)
{
    sh_audio_t* sh_audio = priv->sh;
    HRESULT ret;

    ret = acmStreamClose(priv->srcstream, 0);

    if (ret)
    switch(ret)
    {
	case ACMERR_BUSY:
	case ACMERR_CANCELED:
	    MSG_DBG2( "ACM_Decoder: stream busy, waiting..\n");
	    sleep(100);
	    return close_acm_audio_codec(priv);
	case ACMERR_UNPREPARED:
	case ACMERR_NOTPOSSIBLE:
	    return 0;
	default:
	    MSG_WARN( "ACM_Decoder: unknown error occured: %d\n", ret);
	    return 0;
    }
/*    MSACM_UnregisterAllDrivers();*/
    return 1;
}

MPXP_Rc init(ad_private_t *priv)
{
    sh_audio_t* sh_audio = priv->sh;
    float pts;
    int ret=decode(priv,reinterpret_cast<unsigned char*>(sh_audio->a_buffer),4096,sh_audio->a_buffer_size,&pts);
    if(ret<0){
	MSG_INFO("ACM decoding error: %d\n",ret);
	return MPXP_False;
    }
    sh_audio->a_buffer_len=ret;
    return MPXP_Ok;
}

ad_private_t* preinit(const audio_probe_t* probe,sh_audio_t *sh_audio,audio_filter_info_t* afi)
{
  UNUSED(probe);
  UNUSED(afi);
  /* Win32 ACM audio codec: */
  ad_private_t *priv;
  if(!(priv=new(zeromem) ad_private_t)) return NULL;
  priv->sh = sh_audio;
  if(!init_acm_audio_codec(priv)){
    MSG_ERR(MSGTR_ACMiniterror);
    delete priv;
    return NULL;
  }
  MSG_V("INFO: Win32/ACM init OK!\n");
  return priv;
}

void uninit(ad_private_t *p)
{
  close_acm_audio_codec(p);
  delete p;
}

MPXP_Rc control_ad(ad_private_t *priv,int cmd,any_t* arg, ...)
{
    sh_audio_t* sh_audio = priv->sh;
    int skip;
    switch(cmd) {
//	case ADCTRL_RESYNC_STREAM:
//	     sh_audio->a_in_buffer_len=0;/* reset ACM/DShow audio buffer */
//	  return MPXP_True;
	case ADCTRL_SKIP_FRAME: {
	    float pts;
	    skip=sh_audio->wf->nBlockAlign;
	    if(skip<16){
		skip=(sh_audio->wf->nAvgBytesPerSec/16)&(~7);
		if(skip<16) skip=16;
	    }
	    demux_read_data_r(sh_audio->ds,NULL,skip,&pts);
	    return MPXP_True;
	}
	default:
	    return MPXP_Unknown;
    }
    return MPXP_Unknown;
}

unsigned decode(ad_private_t *priv,unsigned char *buf,unsigned minlen,unsigned maxlen,float *pts)
{
    sh_audio_t* sh_audio = priv->sh;
	ACMSTREAMHEADER ash;
	HRESULT hr;
	DWORD srcsize=0;
	DWORD len=minlen;

	acmStreamSize(priv->srcstream,len , &srcsize, ACM_STREAMSIZEF_DESTINATION);
	MSG_V("acm says: srcsize=%ld  (bufflen=%d size=%d)  out_size=%d\n",srcsize,sh_audio->a_in_buffer_len,sh_audio->a_in_buffer_size,len);

	if(srcsize<sh_audio->wf->nBlockAlign){
		srcsize=sh_audio->wf->nBlockAlign;
		acmStreamSize(priv->srcstream, srcsize, &len, ACM_STREAMSIZEF_SOURCE);
		MSG_V("acm says: srcsize=%ld  (bufflen=%d size=%d)  out_size=%d\n",srcsize,sh_audio->a_in_buffer_len,sh_audio->a_in_buffer_size,len);
		if(len>maxlen) len=maxlen;
	}
	//if(srcsize==0) srcsize=priv->o_wf.nBlockAlign;
	if(srcsize>sh_audio->a_in_buffer_size) srcsize=sh_audio->a_in_buffer_size; // !!!!!!
	if(sh_audio->a_in_buffer_len<srcsize){
		int l;
		l = demux_read_data_r(sh_audio->ds,reinterpret_cast<unsigned char*>(&sh_audio->a_in_buffer[sh_audio->a_in_buffer_len]),
		srcsize-sh_audio->a_in_buffer_len,pts);
		*pts=FIX_APTS(sh_audio,*pts,-sh_audio->a_in_buffer_len);
		sh_audio->a_in_buffer_len+=l;
		priv->pts=*pts;
	}
	else *pts=priv->pts;
	/* !!! NK: FINALY WE MUST KNOW size of output buffer after a_in_buffer_len modifying !!!! */
	acmStreamSize(priv->srcstream,sh_audio->a_in_buffer_len, &len, ACM_STREAMSIZEF_SOURCE);
	MSG_DBG2("acm convert %d -> %d bytes\n",sh_audio->a_in_buffer_len,len);

	memset(&ash, 0, sizeof(ash));
	ash.cbStruct=sizeof(ash);
	ash.fdwStatus=0;
	ash.dwUser=0;
	ash.pbSrc=reinterpret_cast<BYTE*>(sh_audio->a_in_buffer);
	ash.cbSrcLength=sh_audio->a_in_buffer_len;
	ash.pbDst=buf;
	ash.cbDstLength=len;
	hr=acmStreamPrepareHeader(priv->srcstream,&ash,0);
	if(hr){
	    MSG_V("ACM_Decoder: acmStreamPrepareHeader error %d\n",(int)hr);
					return -1;
	}
	hr=acmStreamConvert(priv->srcstream,&ash,0);
	if(hr){
	    MSG_DBG2("ACM_Decoder: acmStreamConvert error %d\n",(int)hr);
	    switch(hr)
	    {
		case ACMERR_NOTPOSSIBLE:
		case ACMERR_UNPREPARED:
		    MSG_DBG2( "ACM_Decoder: acmStreamConvert error: probarly not initialized!\n");
	    }
	}
	MSG_DBG2("acm converted %d -> %d\n",ash.cbSrcLengthUsed,ash.cbDstLengthUsed);
	if(ash.cbSrcLengthUsed>=sh_audio->a_in_buffer_len){
	    sh_audio->a_in_buffer_len=0;
	} else {
	    sh_audio->a_in_buffer_len-=ash.cbSrcLengthUsed;
	    memcpy(sh_audio->a_in_buffer,&sh_audio->a_in_buffer[ash.cbSrcLengthUsed],sh_audio->a_in_buffer_len);
	    priv->pts=FIX_APTS(sh_audio,priv->pts,ash.cbSrcLengthUsed);
	}
	len=ash.cbDstLengthUsed;
	hr=acmStreamUnprepareHeader(priv->srcstream,&ash,0);
	if(hr){
	    MSG_V("ACM_Decoder: acmStreamUnprepareHeader error %d\n",(int)hr);
	}
	return len;
}
