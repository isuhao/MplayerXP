#include "mp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;
/* This file contains reenterable interface to demuxer */
#include <stdlib.h>
#define __USE_ISOC99 1 /* for lrint */
#include <math.h>

#include "libmpstream/stream.h"
#include "demuxer.h"
#include "stheader.h"
#include "demuxer_r.h"
#include "libmpsub/vobsub.h"
#include "osdep/timer.h"

#include "mplayerxp.h"
#include "xmpcore/xmp_core.h"
#include "demux_msg.h"

enc_frame_t*	new_enc_frame(enc_frame_type_e type,unsigned len,float pts,float duration) {
    enc_frame_t* frame=(enc_frame_t*)mp_mallocz(sizeof(enc_frame_t));
    frame->type=type;
    frame->pts=pts;
    frame->duration=duration;
    frame->len=len;
    return frame;
}
void free_enc_frame(enc_frame_t* frame) {
    if(frame->data && frame->type!=VideoFrame) delete frame->data;
    delete frame;
}

pthread_mutex_t demuxer_mutex=PTHREAD_MUTEX_INITIALIZER;
#define LOCK_DEMUXER() { pthread_mutex_lock(&demuxer_mutex); }
#define UNLOCK_DEMUXER() { pthread_mutex_unlock(&demuxer_mutex); }

static float get_ds_stream_pts(Demuxer_Stream *ds,int nbytes)
{
    float retval;
    demuxer_t*demuxer=ds->demuxer;
    xp_core->initial_apts_corr.need_correction=0;
    MSG_DBG2("initial_apts from: stream_pts=%f pts_bytes=%u got_bytes=%u i_bps=%u\n"
    ,ds->pts,ds->tell_pts(),nbytes,((sh_audio_t*)ds->demuxer->audio->sh)->i_bps);
    /* FIXUP AUDIO PTS*/
    if((demuxer->file_format == DEMUXER_TYPE_MPEG_ES ||
	demuxer->file_format == DEMUXER_TYPE_MPEG4_ES ||
	demuxer->file_format == DEMUXER_TYPE_H264_ES ||
	demuxer->file_format == DEMUXER_TYPE_MPEG_PS ||
	demuxer->file_format == DEMUXER_TYPE_MPEG_TS ||
	mp_conf.av_force_pts_fix) && mp_conf.av_sync_pts && mp_conf.av_force_pts_fix2!=1)
    {
	if(ds->pts_flags && ds->pts < 1.0 && ds->prev_pts > 2.0)
	{
	    float spts;
	    spts=ds->demuxer->stream->stream_pts;
	    ds->pts_corr=spts>0?spts:ds->prev_pts;
	    ds->pts_flags=0;
	    MSG_V("***PTS discontinuity happens*** correct audio %f pts as %f\n",ds->pts,ds->pts_corr);
	}
	if(ds->pts>1.0) ds->pts_flags=1;
	if(!ds->eof) ds->prev_pts=ds->pts+ds->pts_corr;
    }
    if(((sh_audio_t*)ds->demuxer->audio->sh)->i_bps)
	retval = ds->pts+ds->pts_corr+((float)(ds->tell_pts()-nbytes))/(float)(((sh_audio_t*)ds->demuxer->audio->sh)->i_bps);
    else
    {
	xp_core->initial_apts_corr.need_correction=1;
	xp_core->initial_apts_corr.pts_bytes=ds->tell_pts();
	xp_core->initial_apts_corr.nbytes=nbytes;
	retval = ds->pts;
    }
    MSG_DBG2("initial_apts is: %f\n",retval);
    return retval;
}

int demux_getc_r(Demuxer_Stream *ds,float *pts)
{
    int retval;
    unsigned int t=0;
    unsigned int t2=0;
    double tt;
    LOCK_DEMUXER();
    if(mp_conf.benchmark) t=GetTimer();
    retval = ds->getch();
    *pts=get_ds_stream_pts(ds,1);
    if(xp_core->initial_apts == HUGE) xp_core->initial_apts=*pts;
    if(mp_conf.benchmark)
    {
	t2=GetTimer();t=t2-t;
	tt = t*0.000001f;
	MPXPCtx->bench->demux+=tt;
	MPXPCtx->bench->audio_decode_correction=tt;
	if(tt > MPXPCtx->bench->max_demux) MPXPCtx->bench->max_demux=tt;
	if(tt < MPXPCtx->bench->min_demux) MPXPCtx->bench->min_demux=tt;
    }
    UNLOCK_DEMUXER();
    return retval;
}

enc_frame_t* video_read_frame_r(sh_video_t* sh_video,int force_fps)
{
    enc_frame_t* frame;
    float frame_time,v_pts;
    unsigned char* start;
    int retval;
    unsigned int t=0;
    unsigned int t2=0;
    double tt;
    LOCK_DEMUXER();
    if(mp_conf.benchmark) t=GetTimer();
    retval = video_read_frame(sh_video,&frame_time,&v_pts,&start,force_fps);
    if(retval<=0) return NULL;
    frame=new_enc_frame(VideoFrame,retval,v_pts,frame_time);
    frame->data=start;
    if(mp_conf.benchmark)
    {
	t2=GetTimer();t=t2-t;
	tt = t*0.000001f;
	MPXPCtx->bench->demux+=tt;
	if(tt > MPXPCtx->bench->max_demux) MPXPCtx->bench->max_demux=tt;
	if(tt < MPXPCtx->bench->min_demux) MPXPCtx->bench->min_demux=tt;
    }
    UNLOCK_DEMUXER();
    return frame;
}

int demux_read_data_r(Demuxer_Stream *ds,unsigned char* mem,int len,float *pts)
{
    int retval;
    unsigned int t=0;
    unsigned int t2=0;
    double tt;
    LOCK_DEMUXER();
    if(mp_conf.benchmark) t=GetTimer();
    retval = ds->read_data(mem,len);
    *pts=get_ds_stream_pts(ds,retval);
    if(xp_core->initial_apts == HUGE) xp_core->initial_apts=*pts;
    if(mp_conf.benchmark)
    {
	t2=GetTimer();t=t2-t;
	tt = t*0.000001f;
	MPXPCtx->bench->demux+=tt;
	MPXPCtx->bench->audio_decode_correction=tt;
	if(tt > MPXPCtx->bench->max_demux) MPXPCtx->bench->max_demux=tt;
	if(tt < MPXPCtx->bench->min_demux) MPXPCtx->bench->min_demux=tt;
    }
    UNLOCK_DEMUXER();
    return retval;
}

int ds_get_packet_r(Demuxer_Stream *ds,unsigned char **start,float *pts)
{
    int retval;
    unsigned int t=0;
    unsigned int t2=0;
    double tt;
    LOCK_DEMUXER();
    if(mp_conf.benchmark) t=GetTimer();
    retval = ds->get_packet(start);
    *pts=get_ds_stream_pts(ds,retval);
    if(xp_core->initial_apts == HUGE) xp_core->initial_apts=*pts;
    if(mp_conf.benchmark)
    {
	t2=GetTimer();t=t2-t;
	tt = t*0.000001f;
	MPXPCtx->bench->demux+=tt;
	MPXPCtx->bench->audio_decode_correction=tt;
	if(tt > MPXPCtx->bench->max_demux) MPXPCtx->bench->max_demux=tt;
	if(tt < MPXPCtx->bench->min_demux) MPXPCtx->bench->min_demux=tt;
    }
    UNLOCK_DEMUXER();
    return retval;
}

int ds_get_packet_sub_r(Demuxer_Stream *ds,unsigned char **start) {
    int rc;
    LOCK_DEMUXER();
    rc=ds->get_packet_sub(start);
    UNLOCK_DEMUXER();
    return rc;
}

/* TODO : FIXME we need to redesign blocking of mutexes before enabling this function*/
int demux_seek_r(demuxer_t *demuxer,const seek_args_t* seeka)
{
    int retval;
    unsigned int t=0;
    unsigned int t2=0;
    double tt;
    LOCK_DEMUXER();
    if(mp_conf.benchmark) t=GetTimer();
    retval = demux_seek(demuxer,seeka);
    if(mp_conf.benchmark)
    {
	t2=GetTimer();t=t2-t;
	tt = t*0.000001f;
	MPXPCtx->bench->demux+=tt;
	if(tt > MPXPCtx->bench->max_demux) MPXPCtx->bench->max_demux=tt;
	if(tt < MPXPCtx->bench->min_demux) MPXPCtx->bench->min_demux=tt;
    }
    UNLOCK_DEMUXER();
    return retval;
}

void vobsub_seek_r(any_t* vobhandle,const seek_args_t* seeka) {
    LOCK_DEMUXER();
    vobsub_seek(vobhandle,seeka);
    UNLOCK_DEMUXER();
}

int demuxer_switch_audio_r(demuxer_t *d, int id)
{
    int retval;
    __MP_SYNCHRONIZE(demuxer_mutex,retval=demuxer_switch_audio(d,id));
    return retval;
}

int demuxer_switch_video_r(demuxer_t *d, int id)
{
    int retval;
    __MP_SYNCHRONIZE(demuxer_mutex,retval=demuxer_switch_video(d,id));
    return retval;
}

int demuxer_switch_subtitle_r(demuxer_t *d, int id)
{
    int retval;
    __MP_SYNCHRONIZE(demuxer_mutex,retval=demuxer_switch_subtitle(d,id));
    return retval;
}
