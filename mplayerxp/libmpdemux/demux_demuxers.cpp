#include "mp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;
#include <stdlib.h>
#include <stdio.h>

#include "libmpstream/stream.h"
#include "demuxer.h"
#include "stheader.h"
#include "demux_msg.h"

typedef struct dd_priv {
  demuxer_t* vd;
  demuxer_t* ad;
  demuxer_t* sd;
} dd_priv_t;


demuxer_t*  new_demuxers_demuxer(demuxer_t* vd, demuxer_t* ad, demuxer_t* sd) {
  demuxer_t* ret;
  dd_priv_t* priv;

  ret = (demuxer_t*)mp_calloc(1,sizeof(demuxer_t));

  priv = (dd_priv_t*)mp_malloc(sizeof(dd_priv_t));
  priv->vd = vd;
  priv->ad = ad;
  priv->sd = sd;
  ret->priv = priv;

  ret->file_format = DEMUXER_TYPE_DEMUXERS;
  // Video is the most important :-)
  ret->stream = vd->stream;
  ret->flags = (vd->flags&DEMUXF_SEEKABLE) && (ad->flags&DEMUXF_SEEKABLE) && (sd->flags&DEMUXF_SEEKABLE);

  ret->video = vd->video;
  ret->audio = ad->audio;
  ret->sub = sd->sub;

    check_pin("demuxer",ad->pin,DEMUX_PIN);
    check_pin("demuxer",vd->pin,DEMUX_PIN);
    check_pin("demuxer",sd->pin,DEMUX_PIN);
    return ret;
}

static int demux_demuxers_fill_buffer(demuxer_t *demux,demux_stream_t *ds) {
  dd_priv_t* priv=reinterpret_cast<dd_priv_t*>(demux->priv);

  if(ds->demuxer == priv->vd)
    return demux_fill_buffer(priv->vd,ds);
  else if(ds->demuxer == priv->ad)
    return demux_fill_buffer(priv->ad,ds);
  else if(ds->demuxer == priv->sd)
    return demux_fill_buffer(priv->sd,ds);

  MSG_ERR("Demux demuxers fill_buffer error : bad demuxer : not vd, ad nor sd\n");
  return 0;
}

static void demux_demuxers_seek(demuxer_t *demuxer,const seek_args_t* seeka) {
  dd_priv_t* priv=reinterpret_cast<dd_priv_t*>(demuxer->priv);
  float pos;

  seek_args_t seek_p = { seeka->secs, 1 };

  stream_set_eof(priv->ad->stream,0);
  stream_set_eof(priv->sd->stream,0);

  // Seek video
  demux_seek(priv->vd,seeka);
  // Get the new pos
  pos = demuxer->video->pts;

  if(priv->ad != priv->vd) {
    sh_audio_t* sh = (sh_audio_t*)demuxer->audio->sh;
    demux_seek(priv->ad,&seek_p);
    // In case the demuxer don't set pts
    if(!demuxer->audio->pts)
      demuxer->audio->pts = pos-((ds_tell_pts(demuxer->audio)-sh->a_in_buffer_len)/(float)sh->i_bps);
    if(sh->timer)
      sh->timer = 0;
  }

  if(priv->sd != priv->vd)
      demux_seek(priv->sd,&seek_p);

}

static void demux_close_demuxers(demuxer_t* demuxer) {
  dd_priv_t* priv = reinterpret_cast<dd_priv_t*>(demuxer->priv);

  if(priv->vd)
    FREE_DEMUXER(priv->vd);
  if(priv->ad && priv->ad != priv->vd)
    FREE_DEMUXER(priv->ad);
  if(priv->sd && priv->sd != priv->vd && priv->sd != priv->ad)
    FREE_DEMUXER(priv->sd);

  delete priv;
  demux_info_free(demuxer);
  delete demuxer;
}
