#include "mp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "ad_internal.h"
#include "osdep/bswap.h"
#include "libao2/afmt.h"

struct ad_private_t {
    sh_audio_t* sh;
};

static const ad_info_t info = {
    "Uncompressed DVD/VOB LPCM audio decoder",
    "dvdpcm",
    "Nickols_K",
    "build-in"
};

static const config_t options[] = {
  { NULL, NULL, 0, 0, 0, 0, NULL}
};

LIBAD_EXTERN(dvdpcm)

static const audio_probe_t probes[] = {
    { "dvdpcm", "dvdpcm", 0x10001,  ACodecStatus_Working, {AFMT_S24_LE, AFMT_S16_LE} },
    { NULL, NULL, 0x0, ACodecStatus_NotWorking, {AFMT_S8}}
};

static const audio_probe_t* __FASTCALL__ probe(uint32_t wtag) {
    unsigned i;
    for(i=0;probes[i].driver;i++)
	if(wtag==probes[i].wtag)
	    return &probes[i];
    return NULL;
}

MPXP_Rc init(ad_private_t *priv)
{
/* DVD PCM Audio:*/
    sh_audio_t* sh = priv->sh;
    sh->i_bps = 0;
    if(sh->codecdata_len==3){
	// we have LPCM header:
	unsigned char h=sh->codecdata[1];
	sh->nch=1+(h&7);
	switch((h>>4)&3){
	    case 0: sh->rate=48000;break;
	    case 1: sh->rate=96000;break;
	    case 2: sh->rate=44100;break;
	    case 3: sh->rate=32000;break;
	}
	switch ((h >> 6) & 3) {
	    case 0:
		sh->afmt = AFMT_S16_BE;
		break;
	    case 1:
		//sh->afmt = AFMT_S20_BE;
		sh->i_bps = sh->nch * sh->rate * 5 / 2;
	    case 2:
		sh->afmt = AFMT_S24_BE;
		break;
	    default:
		sh->afmt = AFMT_S16_BE;
	}
    } else {
	// use defaults:
	sh->nch=2;
	sh->rate=48000;
	sh->afmt = AFMT_S16_BE;
    }
    if (!sh->i_bps)
    sh->i_bps = afmt2bps(sh->afmt) * sh->nch * sh->rate;
    return MPXP_Ok;
}

ad_private_t* preinit(const audio_probe_t* probe,sh_audio_t *sh,audio_filter_info_t* afi)
{
    UNUSED(probe);
    UNUSED(afi);
    sh->audio_out_minsize=2048;
    ad_private_t* priv = new(zeromem) ad_private_t;
    priv->sh = sh;
    return priv;
}

void uninit(ad_private_t *priv)
{
    delete priv;
}

MPXP_Rc control_ad(ad_private_t *priv,int cmd,any_t* arg, ...)
{
    sh_audio_t* sh = priv->sh;
    int skip;
    UNUSED(arg);
    switch(cmd) {
	case ADCTRL_SKIP_FRAME: {
	    float pts;
	    skip=sh->i_bps/16;
	    skip=skip&(~3);
	    demux_read_data_r(sh->ds,NULL,skip,&pts);
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
  unsigned j;
  unsigned len;
  float null_pts;
  UNUSED(maxlen);
  if (afmt2bps(sh_audio->afmt) == 3) {
    if (((sh_audio->codecdata[1] >> 6) & 3) == 1) {
      // 20 bit
      // not sure if the "& 0xf0" and "<< 4" are the right way around
      // can somebody clarify?
      for (j = 0; j < minlen; j += 12) {
	unsigned char tmp[10];
	len = demux_read_data_r(sh_audio->ds, tmp, 10,j?&null_pts:pts);
	if (len < 10) break;
	// first sample
	buf[j + 0] = tmp[0];
	buf[j + 1] = tmp[1];
	buf[j + 2] = tmp[8] & 0xf0;
	// second sample
	buf[j + 3] = tmp[2];
	buf[j + 4] = tmp[3];
	buf[j + 5] = tmp[8] << 4;
	// third sample
	buf[j + 6] = tmp[4];
	buf[j + 7] = tmp[5];
	buf[j + 8] = tmp[9] & 0xf0;
	// fourth sample
	buf[j + 9] = tmp[6];
	buf[j + 10] = tmp[7];
	buf[j + 11] = tmp[9] << 4;
      }
      len = j;
    } else {
      // 24 bit
      for (j = 0; j < minlen; j += 12) {
	unsigned char tmp[12];
	len = demux_read_data_r(sh_audio->ds, tmp, 12, j?&null_pts:pts);
	if (len < 12) break;
	// first sample
	buf[j + 0] = tmp[0];
	buf[j + 1] = tmp[1];
	buf[j + 2] = tmp[8];
	// second sample
	buf[j + 3] = tmp[2];
	buf[j + 4] = tmp[3];
	buf[j + 5] = tmp[9];
	// third sample
	buf[j + 6] = tmp[4];
	buf[j + 7] = tmp[5];
	buf[j + 8] = tmp[10];
	// fourth sample
	buf[j + 9] = tmp[6];
	buf[j + 10] = tmp[7];
	buf[j + 11] = tmp[11];
      }
      len = j;
    }
  } else
  len=demux_read_data_r(sh_audio->ds,buf,(minlen+3)&(~3),pts);
  return len;
}
