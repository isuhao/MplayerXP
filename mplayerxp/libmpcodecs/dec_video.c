#include "mp_config.h"

#ifdef _OPENMP
#include <omp.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "mplayerxp.h"
#include "help_mp.h"

#include "osdep/timer.h"
#include "osdep/shmem.h"
#include "osdep/mplib.h"

#include "libmpdemux/stream.h"
#include "libmpdemux/demuxer.h"
#include "libmpdemux/parse_es.h"
#include "libmpdemux/stheader.h"
#include "xmpcore/sig_hand.h"

#include "libmpconf/codec-cfg.h"

#include "libvo/video_out.h"
#include "postproc/vf.h"

#include "vd.h"

#include "xmpcore/xmp_core.h"
#include "dec_video.h"
#include "libmpsub/spudec.h"
#include "libmpsub/vobsub.h"

// ===================================================================
vf_cfg_t vf_cfg; // Configuration for audio filters

#include "postproc/postprocess.h"
#include "osdep/cpudetect.h"
#include "vd_msg.h"

extern int v_bright;
extern int v_cont;
extern int v_hue;
extern int v_saturation;

int divx_quality=PP_QUALITY_MAX;

const vd_functions_t* mpvdec=NULL;

int mpcv_get_quality_max(sh_video_t *sh_video){
  if(mpvdec){
    int ret=mpvdec->control(sh_video,VDCTRL_QUERY_MAX_PP_LEVEL,NULL);
    if(ret>=0) return ret;
  }
 return 0;
}

void mpcv_set_quality(sh_video_t *sh_video,int quality){
  if(mpvdec)
    mpvdec->control(sh_video,VDCTRL_SET_PP_LEVEL, (any_t*)(&quality));
}

int mpcv_set_colors(sh_video_t *sh_video,char *item,int value)
{
    vf_instance_t* vf=sh_video->vfilter;
    vf_equalizer_t eq;
    eq.item=item;
    eq.value=value*10;
    if(vf->control(vf,VFCTRL_SET_EQUALIZER,&eq)!=MPXP_True)
    {
	if(mpvdec) return mpvdec->control(sh_video,VDCTRL_SET_EQUALIZER,item,(int)value)==MPXP_Ok?1:0;
    }
    return 1;
}

void mpcv_uninit(sh_video_t *sh_video){
    if(!sh_video->inited) return;
    MSG_V("uninit video: %s\n",sh_video->codec->driver_name);
    if(sh_video->vfilter && sh_video->vfilter_inited==1) vf_uninit_filter_chain(sh_video->vfilter);
    mpvdec->uninit(sh_video);
    sh_video->inited=0;
}

#include "libvo/video_out.h"
extern vo_data_t*vo_data;
#define MPDEC_THREAD_COND (VF_FLAGS_THREADS|VF_FLAGS_SLICES)
static unsigned smp_num_cpus=1;
static unsigned use_vf_threads=0;

static const vd_functions_t* mpcv_find_driver_by_name(const char *name) {
    unsigned i;
    for (i=0; mpcodecs_vd_drivers[i] != NULL; i++)
	if(strcmp(mpcodecs_vd_drivers[i]->info->driver_name,name)==0) break;
    return mpcodecs_vd_drivers[i];
}

static void mpcv_print_codec_info(sh_video_t* sh_video) {
    MSG_OK("[VC] %s decoder: [%s] drv:%s.%s (%dx%d (aspect %g) %4.2ffps\n"
	,mp_conf.video_codec?"Forcing":"Selected"
	,sh_video->codec->codec_name
	,mpvdec->info->driver_name
	,sh_video->codec->dll_name
	,sh_video->src_w
	,sh_video->src_h
	,sh_video->aspect
	,sh_video->fps);
    // Yeah! We got it!
    sh_video->inited=1;
    sh_video->vf_flags=vf_query_flags(sh_video->vfilter);
#ifdef _OPENMP
    if(mp_conf.gomp) {
	smp_num_cpus=omp_get_num_procs();
	use_vf_threads=0;
	if(((sh_video->vf_flags&MPDEC_THREAD_COND)==MPDEC_THREAD_COND) && (smp_num_cpus>1)) use_vf_threads=1;
	if(use_vf_threads)
	    MSG_STATUS("[mpdec] will perform parallel video-filter on %u CPUs\n",smp_num_cpus);
    }
#else
    MSG_V("[mpdec] GOMP was not compiled-in! Using single threaded video filtering!\n");
#endif
}

int mpcv_ffmpeg_init(sh_video_t*sh_video) {
    /* Use ffmpeg's drivers  as last hope */
    mpvdec=mpcv_find_driver_by_name("ffmpeg");
    if(!mpvdec->init(sh_video)){
	    MSG_ERR(MSGTR_CODEC_CANT_INITV);
	    return 0;
    }
    mpcv_print_codec_info(sh_video);
    return 1;
}

int mpcv_init(sh_video_t *sh_video,const char* codecname,const char * vfm,int status){
    sh_video->codec=NULL;
    MSG_DBG3("mpcv_init(%p, %s, %s, %i)\n",sh_video,codecname,vfm,status);
    while((sh_video->codec=find_codec(sh_video->fourcc,
	    sh_video->bih?((unsigned int*) &sh_video->bih->biCompression):NULL,
	    sh_video->codec,0) )){
	// ok we found one codec
	if(sh_video->codec->flags&CODECS_FLAG_SELECTED) {
	    MSG_DBG3("mpcv_init: %s already tried and failed\n",sh_video->codec->codec_name);
	    continue;
	}
	if(codecname && strcmp(sh_video->codec->codec_name,codecname)) {
	    MSG_DBG3("mpcv_init: %s != %s [-vc]\n",sh_video->codec->codec_name,codecname);
	    continue;
	}
	if(vfm && strcmp(sh_video->codec->driver_name,vfm)!=0) {
	    MSG_DBG3("mpcv_init: vfm doesn't match %s != %s\n",vfm,sh_video->codec->driver_name);
	    continue; // vfm doesn't match
	}
	if(sh_video->codec->status<status) {
	    MSG_DBG3("mpcv_init: %s too unstable\n",sh_video->codec->codec_name);
	    continue;
	}
	sh_video->codec->flags|=CODECS_FLAG_SELECTED; // tagging it
	// ok, it matches all rules, let's find the driver!
	if(!(mpvdec=mpcv_find_driver_by_name(sh_video->codec->driver_name))) continue;
	else    MSG_DBG3("mpcv_init: mpcodecs_vd_drivers[%s]->mpvdec==0\n",mpcodecs_vd_drivers[i]->info->driver_name);
	// it's available, let's try to init!
	if(!mpvdec->init(sh_video)){
	    MSG_ERR(MSGTR_CODEC_CANT_INITV);
	    continue; // try next...
	}
	mpcv_print_codec_info(sh_video);
	return 1;
    }
    return 0;
}

void mpcodecs_draw_image(sh_video_t* sh,mp_image_t *mpi)
{
  vf_instance_t* vf;
  const unsigned h_step=16;
  unsigned num_slices = mpi->h/h_step;
  vf=sh->vfilter;

  if(!(mpi->flags&(MP_IMGFLAG_DRAW_CALLBACK))){
    if(mpi->h%h_step) num_slices++;
    if(sh->vf_flags&VF_FLAGS_SLICES)
    {
	unsigned j,i,y;
	mp_image_t ampi[num_slices];
	static int hello_printed=0;
	if(!hello_printed) {
		MSG_OK("[VC] using %u threads for video filters\n",smp_num_cpus);
		hello_printed=1;
	}
	y=0;
	for(i=0;i<num_slices;i++) {
	    mpi_fake_slice(&ampi[i],mpi,y,h_step);
	    y+=h_step;
	}
#ifdef _OPENMP
	if(use_vf_threads && (num_slices>smp_num_cpus)) {
	    for(j=0;j<num_slices;j+=smp_num_cpus) {
#pragma omp parallel for shared(vf) private(i)
		for(i=j;i<smp_num_cpus;i++) {
		    MSG_DBG2("parallel: dec_video.put_slice[%ux%u] %i %i %i %i\n",ampi[i].width,ampi[i].height,ampi[i].x,ampi[i].y,ampi[i].w,ampi[i].h);
		    vf->put_slice(vf,&ampi[i]);
		}
	    }
	    for(;j<num_slices;j++) {
		MSG_DBG2("par_tail: dec_video.put_slice[%ux%u] %i %i %i %i\n",ampi[i].width,ampi[i].height,ampi[i].x,ampi[i].y,ampi[i].w,ampi[i].h);
		vf->put_slice(vf,&ampi[j]);
	    }
	}
	else
#endif
	{
	    /* execute slices instead of whole frame make faster multiple filters */
	    for(i=0;i<num_slices;i++) {
		MSG_DBG2("dec_video.put_slice[%ux%u] %i %i %i %i -> [%i]\n",ampi[i].width,ampi[i].height,ampi[i].x,ampi[i].y,ampi[i].w,ampi[i].h,ampi[i].xp_idx);
		vf->put_slice(vf,&ampi[i]);
	    }
	}
    } else {
	MSG_DBG2("Put whole frame[%ux%u]\n",mpi->width,mpi->height);
	vf->put_slice(vf,mpi);
    }
  }
}

extern vo_data_t* vo_data;
static void update_subtitle(sh_video_t *sh_video,float v_pts,unsigned idx);
int mpcv_decode(sh_video_t *sh_video,unsigned char *start,int in_size,int drop_frame, float pts){
    vf_instance_t* vf;
    mp_image_t *mpi=NULL;
    unsigned int t;
    unsigned int t2;
    double tt;

    vf=sh_video->vfilter;

    t=GetTimer();
    vf->control(vf,VFCTRL_START_FRAME,NULL);

    sh_video->active_slices=0;
    mpi=mpvdec->decode(sh_video, start, in_size, drop_frame);
    MSG_DBG2("decvideo: decoding video %u bytes\n",in_size);
    while(sh_video->active_slices!=0) usleep(0);
/* ------------------------ frame decoded. -------------------- */

    if(!mpi) return 0; // error / skipped frame
    mpcodecs_draw_image(sh_video,mpi);

    t2=GetTimer();t=t2-t;
    tt = t*0.000001f;
    mp_data->bench->video+=tt;
    if(mp_conf.benchmark || mp_conf.frame_dropping) {
	if(tt > mp_data->bench->max_video) mp_data->bench->max_video=tt;
	if(tt < mp_data->bench->min_video) mp_data->bench->min_video=tt;
	mp_data->bench->cur_video=tt;
    }

    if(drop_frame) return 0;
    update_subtitle(sh_video,pts,mpi->xp_idx);
    vo_flush_page(vo_data,dae_curr_vdecoded(xp_core));

    t2=GetTimer()-t2;
    tt=t2*0.000001f;
    mp_data->bench->vout+=tt;
    if(mp_conf.benchmark || mp_conf.frame_dropping)
    {
	if(tt > mp_data->bench->max_vout) mp_data->bench->max_vout = tt;
	if(tt < mp_data->bench->min_vout) mp_data->bench->min_vout = tt;
	mp_data->bench->cur_vout=tt;
    }

    return 1;
}

void mpcv_resync_stream(sh_video_t *sh_video)
{
  if(sh_video)
  if(sh_video->inited && mpvdec) mpvdec->control(sh_video,VDCTRL_RESYNC_STREAM,NULL);
}

#ifdef USE_SUB
static float sub_last_pts = -303;
#endif
static void update_subtitle(sh_video_t *sh_video,float v_pts,unsigned xp_idx)
{
    demux_stream_t *d_dvdsub=sh_video->ds->demuxer->sub;
#ifdef USE_SUB
  // find sub
  if(mp_data->subtitles && v_pts>0){
      float pts=v_pts;
      if(mp_conf.sub_fps==0) mp_conf.sub_fps=sh_video->fps;
      MP_UNIT("find_sub");
      if (pts > sub_last_pts || pts < sub_last_pts-1.0 ) {
         find_sub(mp_data->subtitles,sub_uses_time?(100*pts):(pts*mp_conf.sub_fps),vo_data); // FIXME! frame counter...
         sub_last_pts = pts;
      }
      MP_UNIT(NULL);
  }
#endif

  // DVD sub:
#if 0
  if(vo_flags & 0x08){
    static vo_mpegpes_t packet;
    static vo_mpegpes_t *pkg=&packet;
    packet.timestamp=v_pts*90000.0;
    packet.id=0x20; /* Subpic */
    while((packet.size=ds_get_packet_sub_r(d_dvdsub,&packet.data))>0){
      MSG_V("\rDVD sub: len=%d  v_pts=%5.3f  s_pts=%5.3f  \n",packet.size,v_pts,d_dvdsub->pts);
      vo_draw_frame(&pkg);
    }
  }else
#endif
   if(vo_data->spudec){
    unsigned char* packet=NULL;
    int len,timestamp;
    MP_UNIT("spudec");
    spudec_now_pts(vo_data->spudec,90000*v_pts);
    if(spudec_visible(vo_data->spudec)) {
	vo_draw_spudec_direct(vo_data,xp_idx);
    } else {
	spudec_heartbeat(vo_data->spudec,90000*v_pts);
	if (vo_data->vobsub) {
	    if (v_pts >= 0) {
		while((len=vobsub_get_packet(vo_data->vobsub, v_pts,(any_t**)&packet, &timestamp))>0){
		    timestamp -= v_pts*90000;
		    MSG_V("\rVOB sub: len=%d v_pts=%5.3f sub=%5.3f ts=%d \n",len,v_pts,timestamp / 90000.0,timestamp);
		    spudec_assemble(vo_data->spudec,packet,len,90000*d_dvdsub->pts);
		}
	    }
	} else {
	    while((len=ds_get_packet_sub_r(d_dvdsub,&packet))>0){
		MSG_V("\rDVD sub: len=%d  v_pts=%5.3f  s_pts=%5.3f  \n",len,v_pts,d_dvdsub->pts);
		spudec_assemble(vo_data->spudec,packet,len,90000*d_dvdsub->pts);
	    }
	}
	/* detect wether the sub has changed or not */
	if(spudec_changed(vo_data->spudec)) vo_draw_spudec_direct(vo_data,xp_idx);
	MP_UNIT(NULL);
    }
  }
}
