/* This file contains reenterable interface to demuxer */
#include <stdlib.h>
#define __USE_ISOC99 1 /* for lrint */
#include <math.h>

#include "stream.h"
#include "demuxer.h"
#include "stheader.h"
#include "demuxer_r.h"
#include "vobsub.h"
#include "../osdep/timer.h"
#include "../mplayer.h"
#include "../dec_ahead.h"

pthread_mutex_t demuxer_mutex=PTHREAD_MUTEX_INITIALIZER;
#define LOCK_DEMUXER() { pthread_mutex_lock(&demuxer_mutex); }
#define UNLOCK_DEMUXER() { pthread_mutex_unlock(&demuxer_mutex); }

static float get_ds_stream_pts(demux_stream_t *ds,int nbytes)
{
    float retval;
    demuxer_t*demuxer=ds->demuxer;
    initial_audio_pts_corr.need_correction=0;
    MSG_DBG2("initial_apts from: stream_pts=%f pts_bytes=%u got_bytes=%u i_bps=%u\n"
    ,ds->pts,ds_tell_pts(ds),nbytes,((sh_audio_t*)ds->demuxer->audio->sh)->i_bps);
    /* FIXUP AUDIO PTS*/
    if((demuxer->file_format == DEMUXER_TYPE_MPEG_ES ||
	demuxer->file_format == DEMUXER_TYPE_MPEG4_ES ||
	demuxer->file_format == DEMUXER_TYPE_H264_ES ||
	demuxer->file_format == DEMUXER_TYPE_MPEG_PS ||
	demuxer->file_format == DEMUXER_TYPE_MPEG_TS ||
	av_force_pts_fix) && av_sync_pts && av_force_pts_fix2!=1)
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
	retval = ds->pts+ds->pts_corr+((float)(ds_tell_pts(ds)-nbytes))/(float)(((sh_audio_t*)ds->demuxer->audio->sh)->i_bps);
    else
    {
	initial_audio_pts_corr.need_correction=1;
	initial_audio_pts_corr.pts_bytes=ds_tell_pts(ds);
	initial_audio_pts_corr.nbytes=nbytes;
	retval = ds->pts;
    }
    MSG_DBG2("initial_apts is: %f\n",retval);
    return retval;
}

int demux_getc_r(demux_stream_t *ds,float *pts)
{
    int retval;
    unsigned int t=0;
    unsigned int t2=0;
    double tt;
    LOCK_DEMUXER();
    if(benchmark) t=GetTimer();
    retval = demux_getc(ds);
    *pts=get_ds_stream_pts(ds,1);
    if(initial_audio_pts == HUGE) initial_audio_pts=*pts;
    if(benchmark)
    {
	t2=GetTimer();t=t2-t;
	tt = t*0.000001f;
	demux_time_usage+=tt;
	audio_decode_time_usage_correction=tt;
	if(tt > max_demux_time_usage) max_demux_time_usage=tt;
	if(tt < min_demux_time_usage) min_demux_time_usage=tt;
    }
    UNLOCK_DEMUXER();
    return retval;
}

int video_read_frame_r(sh_video_t* sh_video,float* frame_time_ptr,float *v_pts,unsigned char** start,int force_fps)
{
    int retval;
    unsigned int t=0;
    unsigned int t2=0;
    double tt;
    LOCK_DEMUXER();
    if(benchmark) t=GetTimer();
    retval = video_read_frame(sh_video,frame_time_ptr,v_pts,start,force_fps);
    if(benchmark)
    {
	t2=GetTimer();t=t2-t;
	tt = t*0.000001f;
	demux_time_usage+=tt;
	if(tt > max_demux_time_usage) max_demux_time_usage=tt;
	if(tt < min_demux_time_usage) min_demux_time_usage=tt;
    }
    UNLOCK_DEMUXER();
    return retval;
}

int demux_read_data_r(demux_stream_t *ds,unsigned char* mem,int len,float *pts)
{
    int retval;
    unsigned int t=0;
    unsigned int t2=0;
    double tt;
    LOCK_DEMUXER();
    if(benchmark) t=GetTimer();
    retval = demux_read_data(ds,mem,len);
    *pts=get_ds_stream_pts(ds,retval);
    if(initial_audio_pts == HUGE) initial_audio_pts=*pts;
    if(benchmark)
    {
	t2=GetTimer();t=t2-t;
	tt = t*0.000001f;
	demux_time_usage+=tt;
	audio_decode_time_usage_correction=tt;
	if(tt > max_demux_time_usage) max_demux_time_usage=tt;
	if(tt < min_demux_time_usage) min_demux_time_usage=tt;
    }
    UNLOCK_DEMUXER();
    return retval;
}

int ds_get_packet_r(demux_stream_t *ds,unsigned char **start,float *pts)
{
    int retval;
    unsigned int t=0;
    unsigned int t2=0;
    double tt;
    LOCK_DEMUXER();
    if(benchmark) t=GetTimer();
    retval = ds_get_packet(ds,start);
    *pts=get_ds_stream_pts(ds,retval);
    if(initial_audio_pts == HUGE) initial_audio_pts=*pts;
    if(benchmark)
    {
	t2=GetTimer();t=t2-t;
	tt = t*0.000001f;
	demux_time_usage+=tt;
	audio_decode_time_usage_correction=tt;
	if(tt > max_demux_time_usage) max_demux_time_usage=tt;
	if(tt < min_demux_time_usage) min_demux_time_usage=tt;
    }
    UNLOCK_DEMUXER();
    return retval;
}


/* TODO : FIXME we need to redesign blocking of mutexes before enabling this function*/
int demux_seek_r(demuxer_t *demuxer,const seek_args_t* seeka)
{
    int retval;
    unsigned int t=0;
    unsigned int t2=0;
    double tt;
    LOCK_DEMUXER();
    if(benchmark) t=GetTimer();
    retval = demux_seek(demuxer,seeka);
    if(benchmark)
    {
	t2=GetTimer();t=t2-t;
	tt = t*0.000001f;
	demux_time_usage+=tt;
	if(tt > max_demux_time_usage) max_demux_time_usage=tt;
	if(tt < min_demux_time_usage) min_demux_time_usage=tt;
    }
    UNLOCK_DEMUXER();
    return retval;
}

void vobsub_seek_r(any_t* vobhandle, float pts) {
    LOCK_DEMUXER();
    vobsub_seek(vobhandle,pts);
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
