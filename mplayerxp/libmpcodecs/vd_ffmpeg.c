#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <dlfcn.h> /* GLIBC specific. Exists under cygwin too! */

#include "config.h"
#include "help_mp.h"

#include "bswap.h"

#include "vd_internal.h"
#include "codecs_ld.h"
#include "../postproc/postprocess.h"
#include "../postproc/vf.h"
#include "../libvo/video_out.h"

static const vd_info_t info = {
	"FFmpeg's libavcodec codec family",
	"ffmpeg",
	"A'rpi",
	"http://ffmpeg.sf.net",
	"native codecs"
};

static int lavc_param_error_resilience=2;
static int lavc_param_error_concealment=3;
static int lavc_param_vstats=0;
static int lavc_param_idct_algo=0;
static int lavc_param_debug=0;
static int lavc_param_vismv=0;
static int lavc_param_skip_top=0;
static int lavc_param_skip_bottom=0;
static int lavc_param_lowres=0;
static char *lavc_param_lowres_str=NULL;
static char *lavc_param_skip_loop_filter_str = NULL;
static char *lavc_param_skip_idct_str = NULL;
static char *lavc_param_skip_frame_str = NULL;
static int lavc_param_threads=-1;
static char *lavc_avopt = NULL;

static const config_t options[] = {
	{"ffmpeg_er", &lavc_param_error_resilience, CONF_TYPE_INT, CONF_RANGE, 0, 99, NULL},
	{"ffmpeg_idct", &lavc_param_idct_algo, CONF_TYPE_INT, CONF_RANGE, 0, 99, NULL},
	{"ffmpeg_ec", &lavc_param_error_concealment, CONF_TYPE_INT, CONF_RANGE, 0, 99, NULL},
	{"ffmpeg_vstats", &lavc_param_vstats, CONF_TYPE_FLAG, 0, 0, 1, NULL},
	{"ffmpeg_debug", &lavc_param_debug, CONF_TYPE_INT, CONF_RANGE, 0, 9999999, NULL},
	{"ffmpeg_vismv", &lavc_param_vismv, CONF_TYPE_INT, CONF_RANGE, 0, 9999999, NULL},
	{"ffmpeg_st", &lavc_param_skip_top, CONF_TYPE_INT, CONF_RANGE, 0, 999, NULL},
	{"ffmpeg_sb", &lavc_param_skip_bottom, CONF_TYPE_INT, CONF_RANGE, 0, 999, NULL},
	{"ffmpeg_lowres", &lavc_param_lowres_str, CONF_TYPE_STRING, 0, 0, 0, NULL},
	{"ffmpeg_skiploopfilter", &lavc_param_skip_loop_filter_str, CONF_TYPE_STRING, 0, 0, 0, NULL},
	{"ffmpeg_skipidct", &lavc_param_skip_idct_str, CONF_TYPE_STRING, 0, 0, 0, NULL},
	{"ffmpeg_skipframe", &lavc_param_skip_frame_str, CONF_TYPE_STRING, 0, 0, 0, NULL},
	{"ffmpeg_threads", &lavc_param_threads, CONF_TYPE_INT, CONF_RANGE, 1, 8, NULL},
	{"ffmpeg_o", &lavc_avopt, CONF_TYPE_STRING, 0, 0, 0, NULL},
	{ NULL, NULL, 0, 0, 0, 0, NULL}
};

LIBVD_EXTERN(ffmpeg)

#include "interface/ffmpeg/avcodec.h"
#include "libvo/video_out.h"

extern char *npp_options;
static int vcodec_inited=0;
typedef struct priv_s
{
    int use_slices;
    int cap_slices;
    int use_dr1;
    int cap_dr1;
    AVCodec *lavc_codec;
    AVCodecContext *ctx;
    AVFrame *lavc_picture;
    int b_age;
    int ip_age[2];
    int qp_stat[32];
//    double qp_sum;
//    double inv_qp_sum;
    int ip_count;
    int b_count;
    int vo_inited;
}priv_t;
static pp_context_t* ppContext=NULL;
static void draw_slice(struct AVCodecContext *s,
                        const AVFrame *src, int offset[4],
                        int y, int type, int height);

static enum AVDiscard str2AVDiscard(char *str) {
    if (!str)                               return AVDISCARD_DEFAULT;
    if (strcasecmp(str, "none"   ) == 0)    return AVDISCARD_NONE;
    if (strcasecmp(str, "default") == 0)    return AVDISCARD_DEFAULT;
    if (strcasecmp(str, "nonref" ) == 0)    return AVDISCARD_NONREF;
    if (strcasecmp(str, "bidir"  ) == 0)    return AVDISCARD_BIDIR;
    if (strcasecmp(str, "nonkey" ) == 0)    return AVDISCARD_NONKEY;
    if (strcasecmp(str, "all"    ) == 0)    return AVDISCARD_ALL;
    MSG_ERR("Unknown discard value %s\n", str);
    return AVDISCARD_DEFAULT;
}

/* stupid workaround for current version of ffmpeg */
const __attribute((used)) uint8_t last_coeff_flag_offset_8x8[63] = {
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
    5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8
};

unsigned (*avcodec_version_ptr)(void);
void (*avcodec_init_ptr)(void);
void (*avcodec_register_all_ptr)(void);
AVCodec * (*avcodec_find_decoder_by_name_ptr)(const char *name);
int (*avcodec_open_ptr)(AVCodecContext *avctx, AVCodec *codec);
int (*avcodec_close_ptr)(AVCodecContext *avctx);
int (*avcodec_decode_video_ptr)(AVCodecContext *avctx, AVFrame *picture,
                         int *got_picture_ptr,
                         uint8_t *buf, int buf_size);
AVCodecContext* (*avcodec_alloc_context_ptr)(void);
AVFrame* (*avcodec_alloc_frame_ptr)(void);
int (*avcodec_default_get_buffer_ptr)(AVCodecContext *s, AVFrame *pic);
void (*avcodec_default_release_buffer_ptr)(AVCodecContext *s, AVFrame *pic);
void (*avcodec_flush_buffers_ptr)(AVCodecContext *avctx);
int (*avcodec_thread_init_ptr)(AVCodecContext *s, int thread_count);

static void *dll_handle;
static int load_dll(const char *libname)
{
  if(!(dll_handle=ld_codec(libname,"http://ffmpeg.sf.net"))) return 0;
  avcodec_version_ptr = ld_sym(dll_handle,"avcodec_version");
  avcodec_init_ptr = ld_sym(dll_handle,"avcodec_init");
  avcodec_register_all_ptr = ld_sym(dll_handle,"avcodec_register_all");
  avcodec_find_decoder_by_name_ptr = ld_sym(dll_handle,"avcodec_find_decoder_by_name");
  avcodec_open_ptr = ld_sym(dll_handle,"avcodec_open");
  avcodec_close_ptr = ld_sym(dll_handle,"avcodec_close");
  avcodec_decode_video_ptr = ld_sym(dll_handle,"avcodec_decode_video");
  avcodec_alloc_context_ptr = ld_sym(dll_handle,"avcodec_alloc_context");
  avcodec_alloc_frame_ptr = ld_sym(dll_handle,"avcodec_alloc_frame");
  avcodec_default_get_buffer_ptr = ld_sym(dll_handle,"avcodec_default_get_buffer");
  avcodec_default_release_buffer_ptr = ld_sym(dll_handle,"avcodec_default_release_buffer");
  avcodec_flush_buffers_ptr = ld_sym(dll_handle,"avcodec_flush_buffers");
  avcodec_thread_init_ptr = ld_sym(dll_handle,"avcodec_thread_init");
  return avcodec_version_ptr && avcodec_init_ptr && avcodec_register_all_ptr &&
	avcodec_find_decoder_by_name_ptr && avcodec_open_ptr && avcodec_close_ptr &&
	avcodec_decode_video_ptr && avcodec_alloc_context_ptr &&
	avcodec_alloc_frame_ptr && avcodec_default_get_buffer_ptr &&
	avcodec_default_release_buffer_ptr && avcodec_flush_buffers_ptr &&
	avcodec_thread_init_ptr;
}

#define MIN_LIBAVCODEC_VERSION_INT	((51<<16)+(0<<8)+0)

/* to set/get/query special features/parameters */
static int control(sh_video_t *sh,int cmd,void* arg,...){
    priv_t *ctx = sh->context;
    AVCodecContext *avctx = ctx->ctx;
    switch(cmd){
	case VDCTRL_QUERY_MAX_PP_LEVEL:
	    return PP_QUALITY_MAX;
	case VDCTRL_SET_PP_LEVEL: {
	    int quality=*((int*)arg);
	    if(quality<0 || quality>PP_QUALITY_MAX) quality=PP_QUALITY_MAX;
	    divx_quality = quality;
	    return CONTROL_OK;
	}
	case VDCTRL_QUERY_FORMAT:
        {
            int format =(*((int*)arg));
//	    if( format == ctx->best_csp ) return CONTROL_TRUE;//supported
	// possible conversions:
	    switch( format ){
		case IMGFMT_YV12:
		case IMGFMT_IYUV:
		case IMGFMT_I420:
		    // "converted" using pointer/stride modification
		    if(	avctx->pix_fmt==PIX_FMT_YUV420P || // u/v swap
			avctx->pix_fmt==PIX_FMT_YUV422P) return CONTROL_TRUE;// half stride
		    break;
#ifdef HAVE_XVMC
		case IMGFMT_XVMC_IDCT_MPEG2:
		case IMGFMT_XVMC_MOCO_MPEG2:
		    if(avctx->pix_fmt==PIX_FMT_XVMC_MPEG2_IDCT) return CONTROL_TRUE;
#endif
	    }
	    return CONTROL_FALSE;
        }
	break;
	case VDCTRL_RESYNC_STREAM:
	    (*avcodec_flush_buffers_ptr)(avctx);
	    return CONTROL_TRUE;
    }
    return CONTROL_UNKNOWN;
}

static int ff_config_vo(sh_video_t *sh,uint32_t w,uint32_t h)
{
    priv_t *vdff_ctx=sh->context;
    vo_tune_info_t vfi;
    if(!vdff_ctx->vo_inited)
    {
	unsigned halign=15,valign=15;
	vfi.pitch[0]=32;
	vfi.pitch[1]=
	vfi.pitch[2]=16;
	if(vdff_ctx->ctx->pix_fmt == PIX_FMT_YUV410P && vdff_ctx->cap_dr1)
	{
	    //yes seriously, its really needed (16x16 chroma blocks in SVQ1 -> 64x64)
	    valign=63;
	    vfi.pitch[0]=64;
	    vfi.pitch[1]=
	    vfi.pitch[2]=16;
	}
	sh->disp_w=w;//(w+valign)&(~valign);
	sh->disp_h=(h+halign)&(~halign);
	vdff_ctx->vo_inited=1;
	return mpcodecs_config_vo(sh,sh->disp_w,sh->disp_h,&vfi);
    }
    return 1;
}

extern unsigned xp_num_cpu;
static int init(sh_video_t *sh){
    unsigned avc_version=0;
    priv_t *vdff_ctx;
    int pp_flags;
    if(npp_options) pp2_init();
    if(!vcodec_inited){
     if(!load_dll(codec_name("libavcodec"SLIBSUFFIX))) /* try local copy first */
      if(!load_dll("libavcodec-0.4.9"SLIBSUFFIX))
	if(!load_dll("libavcodec"SLIBSUFFIX))
	{
	    MSG_ERR("Detected error during loading libffmpeg"SLIBSUFFIX"! Try to upgrade this codec\n");
	    return 0;
	}
	avc_version = (*avcodec_version_ptr)();
	if(avc_version < MIN_LIBAVCODEC_VERSION_INT)
	{
	    MSG_ERR("You have wrong version of libavcodec %06X < %06X\n",
		    avc_version,MIN_LIBAVCODEC_VERSION_INT);
	    return 0;
	}
        (*avcodec_init_ptr)();
        (*avcodec_register_all_ptr)();
        vcodec_inited=1;
    }
    vdff_ctx=malloc(sizeof(priv_t));
    memset(vdff_ctx,0,sizeof(priv_t));
    sh->context = vdff_ctx;
    vdff_ctx->lavc_codec = (AVCodec *)(*avcodec_find_decoder_by_name_ptr)(sh->codec->dll_name);
    if(!vdff_ctx->lavc_codec){
	MSG_ERR(MSGTR_MissingLAVCcodec,sh->codec->dll_name);
	return 0;
    }
    
    vdff_ctx->ctx = (*avcodec_alloc_context_ptr)();
    vdff_ctx->lavc_picture = (*avcodec_alloc_frame_ptr)();
    if(!(vdff_ctx->ctx && vdff_ctx->lavc_picture))
    {
        MSG_ERR(MSGTR_OutOfMemory);
        return 0;
    }
#ifdef CODEC_FLAG_NOT_TRUNCATED
    vdff_ctx->ctx->flags|= CODEC_FLAG_NOT_TRUNCATED;
#endif
    vdff_ctx->ctx->width = sh->disp_w;
    vdff_ctx->ctx->height= sh->disp_h;
    vdff_ctx->ctx->error_recognition= lavc_param_error_resilience;
    vdff_ctx->ctx->error_concealment= lavc_param_error_concealment;
    vdff_ctx->ctx->debug= lavc_param_debug;
    vdff_ctx->ctx->codec_tag= sh->format;
    vdff_ctx->ctx->stream_codec_tag=sh->video.fccHandler;
    vdff_ctx->ctx->idct_algo=0; /*auto*/
#if 0
    if (lavc_param_debug)
        av_log_set_level(AV_LOG_DEBUG);
#endif
    vdff_ctx->ctx->debug_mv= lavc_param_vismv;
    vdff_ctx->ctx->skip_top   = lavc_param_skip_top;
    vdff_ctx->ctx->skip_bottom= lavc_param_skip_bottom;
    if(lavc_param_lowres_str != NULL)
    {
	int lowres_w=0;
        sscanf(lavc_param_lowres_str, "%d,%d", &lavc_param_lowres, &lowres_w);
        if(lavc_param_lowres < 1 || lavc_param_lowres > 16 || (lowres_w > 0 && vdff_ctx->ctx->width < lowres_w))
            lavc_param_lowres = 0;
        vdff_ctx->ctx->lowres = lavc_param_lowres;
    }
    vdff_ctx->ctx->skip_loop_filter = str2AVDiscard(lavc_param_skip_loop_filter_str);
    vdff_ctx->ctx->skip_idct = str2AVDiscard(lavc_param_skip_idct_str);
    vdff_ctx->ctx->skip_frame = str2AVDiscard(lavc_param_skip_frame_str);
    if(sh->bih)
	vdff_ctx->ctx->bits_per_coded_sample= sh->bih->biBitCount;
    MSG_DBG2("libavcodec.size: %d x %d\n",vdff_ctx->ctx->width,vdff_ctx->ctx->height);
    /* AVRn stores huffman table in AVI header */
    /* Pegasus MJPEG stores it also in AVI header, but it uses the common
       MJPG fourcc :( */
    if (sh->bih && (sh->bih->biSize != sizeof(BITMAPINFOHEADER)) &&
	(sh->format == mmioFOURCC('A','V','R','n') ||
	sh->format == mmioFOURCC('M','J','P','G')))
    {
	vdff_ctx->ctx->flags |= CODEC_FLAG_EXTERN_HUFF;
	vdff_ctx->ctx->extradata_size = sh->bih->biSize-sizeof(BITMAPINFOHEADER);
	vdff_ctx->ctx->extradata = malloc(vdff_ctx->ctx->extradata_size);
	memcpy(vdff_ctx->ctx->extradata, sh->bih+sizeof(BITMAPINFOHEADER),
	    vdff_ctx->ctx->extradata_size);
    }
    if(   sh->format == mmioFOURCC('R', 'V', '1', '0')
       || sh->format == mmioFOURCC('R', 'V', '1', '3')
       || sh->format == mmioFOURCC('R', 'V', '2', '0')
       || sh->format == mmioFOURCC('R', 'V', '3', '0')
       || sh->format == mmioFOURCC('R', 'V', '4', '0'))
       {
        vdff_ctx->ctx->extradata_size= 8;
        vdff_ctx->ctx->extradata = malloc(vdff_ctx->ctx->extradata_size);
        if(sh->bih->biSize!=sizeof(*sh->bih)+8){
            /* only 1 packet per frame & sub_id from fourcc */
	    ((uint32_t*)vdff_ctx->ctx->extradata)[0] = 0;
	    vdff_ctx->ctx->sub_id=
	    ((uint32_t*)vdff_ctx->ctx->extradata)[1] =
        	(sh->format == mmioFOURCC('R', 'V', '1', '3')) ? 0x10003001 : 0x10000000;
        } else {
	    /* has extra slice header (demux_rm or rm->avi streamcopy) */
	    unsigned int* extrahdr=(unsigned int*)(sh->bih+1);
	    ((uint32_t*)vdff_ctx->ctx->extradata)[0] = extrahdr[0];
	    vdff_ctx->ctx->sub_id=
	    ((uint32_t*)vdff_ctx->ctx->extradata)[1] = extrahdr[1];
	}
    }
    if (sh->bih && (sh->bih->biSize != sizeof(BITMAPINFOHEADER)) &&
	(sh->format == mmioFOURCC('M','4','S','2') ||
	 sh->format == mmioFOURCC('M','P','4','S') ||
	 sh->format == mmioFOURCC('H','F','Y','U') ||
	 sh->format == mmioFOURCC('F','F','V','H') ||
	 sh->format == mmioFOURCC('W','M','V','2') ||
	 sh->format == mmioFOURCC('W','M','V','3') ||
	 sh->format == mmioFOURCC('A','S','V','1') ||
	 sh->format == mmioFOURCC('A','S','V','2') ||
	 sh->format == mmioFOURCC('V','S','S','H') ||
	 sh->format == mmioFOURCC('M','S','Z','H') ||
	 sh->format == mmioFOURCC('Z','L','I','B') ||
	 sh->format == mmioFOURCC('M','P','4','V') ||
	 sh->format == mmioFOURCC('F','L','I','C') ||
	 sh->format == mmioFOURCC('S','N','O','W') ||
	 sh->format == mmioFOURCC('a','v','c','1') ||
	 sh->format == mmioFOURCC('L','O','C','O') ||
	 sh->format == mmioFOURCC('t','h','e','o')
         ))
    {
	vdff_ctx->ctx->extradata_size = sh->bih->biSize-sizeof(BITMAPINFOHEADER);
	vdff_ctx->ctx->extradata = malloc(vdff_ctx->ctx->extradata_size);
	memcpy(vdff_ctx->ctx->extradata, sh->bih+1, vdff_ctx->ctx->extradata_size);
    }
    if (sh->ImageDesc &&
	 sh->format == mmioFOURCC('S','V','Q','3')){
	vdff_ctx->ctx->extradata_size = *(int*)sh->ImageDesc;
	vdff_ctx->ctx->extradata = malloc(vdff_ctx->ctx->extradata_size);
	memcpy(vdff_ctx->ctx->extradata, ((int*)sh->ImageDesc)+1, vdff_ctx->ctx->extradata_size);
    }
    
    /* Pass palette to codec */
#if LIBAVCODEC_BUILD >= 4689
    if (sh->bih && (sh->bih->biBitCount <= 8)) {
        vdff_ctx->ctx->palctrl = (AVPaletteControl*)calloc(1,sizeof(AVPaletteControl));
        vdff_ctx->ctx->palctrl->palette_changed = 1;
        if (sh->bih->biSize-sizeof(BITMAPINFOHEADER))
            /* Palette size in biSize */
            memcpy(vdff_ctx->ctx->palctrl->palette, sh->bih+1,
                   min(sh->bih->biSize-sizeof(BITMAPINFOHEADER), AVPALETTE_SIZE));
        else
            /* Palette size in biClrUsed */
            memcpy(vdff_ctx->ctx->palctrl->palette, sh->bih+1,
                   min(sh->bih->biClrUsed * 4, AVPALETTE_SIZE));
	}
#endif
    if(sh->bih)
	vdff_ctx->ctx->bits_per_coded_sample= sh->bih->biBitCount;
    if(vdff_ctx->lavc_codec->capabilities&CODEC_CAP_DRAW_HORIZ_BAND) vdff_ctx->cap_slices=1;
/* enable DR1 method */
    if(vdff_ctx->lavc_codec->capabilities&CODEC_CAP_DR1) vdff_ctx->cap_dr1=1;
    vdff_ctx->ctx->flags|= CODEC_FLAG_EMU_EDGE;

    if(lavc_param_threads < 0) lavc_param_threads = xp_num_cpu;
    if(lavc_param_threads > 1) {
        (*avcodec_thread_init_ptr)(vdff_ctx->ctx, lavc_param_threads);
        MSG_STATUS("Using %i threads in FFMPEG\n",lavc_param_threads);
    }
    /* open it */
    if ((*avcodec_open_ptr)(vdff_ctx->ctx, vdff_ctx->lavc_codec) < 0) {
        MSG_ERR( MSGTR_CantOpenCodec);
        return 0;
    }
    MSG_V("INFO: libavcodec.so (%06X) video codec init OK!\n",avc_version);
    if(npp_options)
    {
	pp_flags=0;
	switch(sh->codec->outfmt[sh->outfmtidx])
	{
	    case IMGFMT_YV12:
	    case IMGFMT_I420:
	    case IMGFMT_IYUV: pp_flags = PP_FORMAT_420;
			      break;
	    case IMGFMT_YVYU:
	    case IMGFMT_YUY2: pp_flags = PP_FORMAT_422;
			      break;
	    case IMGFMT_411P: pp_flags = PP_FORMAT_411;
			      break;
	    default:
	    {
		const char *fmt;
		fmt = (const char *)&sh->codec->outfmt[sh->outfmtidx];
		MSG_WARN("Can't apply postprocessing for");
		if(isprint(fmt[0]) && isprint(fmt[1]) && isprint(fmt[2]) && isprint(fmt[3]))
		    MSG_WARN(" '%c%c%c%c'!\n",fmt[0],fmt[1],fmt[2],fmt[3]);
		else MSG_ERR(" 0x%08X!\n",sh->codec->outfmt[sh->outfmtidx]);
		break;
	    }
	}
	if(pp_flags)
	{
	    ppContext=pp2_get_context(sh->disp_w,sh->disp_h,pp_flags);
	}
    }
    return ff_config_vo(sh,sh->disp_w,sh->disp_h);
}

// uninit driver
static void uninit(sh_video_t *sh){
    priv_t *vdff_ctx=sh->context;
    if ((*avcodec_close_ptr)(vdff_ctx->ctx) < 0)
    	    MSG_ERR( MSGTR_CantCloseCodec);
    if (vdff_ctx->ctx->extradata_size)
	free(vdff_ctx->ctx->extradata);
    free(vdff_ctx->ctx);
    free(vdff_ctx->lavc_picture);
    free(vdff_ctx);
    if(ppContext) pp2_free_context(ppContext);
    ppContext=NULL;
    pp2_uninit();
    dlclose(dll_handle);
    vcodec_inited=0;
}

static int get_buffer(AVCodecContext *avctx, AVFrame *pic){
    sh_video_t * sh = avctx->opaque;
    priv_t *vdff_ctx = sh->context;
    mp_image_t* mpi=NULL;
    int flags= MP_IMGFLAG_ACCEPT_STRIDE | MP_IMGFLAG_PREFER_ALIGNED_STRIDE;
    int type= MP_IMGTYPE_IPB;
    int width= avctx->width;
    int height= avctx->height;
    int align=15;
//printf("get_buffer %d %d %d\n", pic->reference, ctx->ip_count, ctx->b_count);
    if(avctx->pix_fmt == PIX_FMT_YUV410P)
        align=63; //yes seriously, its really needed (16x16 chroma blocks in SVQ1 -> 64x64)

#if LIBAVCODEC_BUILD >= 4691
  if (pic->buffer_hints) {
    MSG_DBG2( "Buffer hints: %u\n", pic->buffer_hints);
    type = MP_IMGTYPE_TEMP;
    if (pic->buffer_hints & FF_BUFFER_HINTS_READABLE)
        flags |= MP_IMGFLAG_READABLE;
    if (pic->buffer_hints & FF_BUFFER_HINTS_PRESERVE) {
        type = MP_IMGTYPE_STATIC;
        flags |= MP_IMGFLAG_PRESERVE;
    }
    if (pic->buffer_hints & FF_BUFFER_HINTS_REUSABLE) {
        type = MP_IMGTYPE_STATIC;
        flags |= MP_IMGFLAG_PRESERVE;
    }
    flags|=(!avctx->hurry_up && vdff_ctx->use_slices) ?
            MP_IMGFLAG_DRAW_CALLBACK:0;
    MSG_DBG2( type == MP_IMGTYPE_STATIC ? "using STATIC\n" : "using TEMP\n");
  } else {
#endif
    if(!pic->reference){
        vdff_ctx->b_count++;
        flags|=(!avctx->hurry_up && vdff_ctx->use_slices) ?
                MP_IMGFLAG_DRAW_CALLBACK:0;
    }else{
        vdff_ctx->ip_count++;
        flags|= MP_IMGFLAG_PRESERVE|MP_IMGFLAG_READABLE
                | (vdff_ctx->use_slices ? MP_IMGFLAG_DRAW_CALLBACK : 0);
    }
#if LIBAVCODEC_BUILD >= 4691
  }

  if (!pic->buffer_hints) {
#endif
    if(vdff_ctx->b_count>1 || vdff_ctx->ip_count>2){
        MSG_WARN("DR1 failure\n");

        vdff_ctx->use_dr1=0; //FIXME
        avctx->get_buffer= avcodec_default_get_buffer_ptr;
        return avctx->get_buffer(avctx, pic);
    }

    if(avctx->has_b_frames){
        type= MP_IMGTYPE_IPB;
    }else{
        type= MP_IMGTYPE_IP;
    }
    MSG_DBG2( type== MP_IMGTYPE_IPB ? "using IPB\n" : "using IP\n");
#if LIBAVCODEC_BUILD >= 4691
  }
#endif

    MSG_V("ff width=%i height=%i\n",width,height);
    mpi= mpcodecs_get_image(sh,type, flags, (width+align)&(~align), (height+align)&(~align));
    if(mpi->flags & MP_IMGFLAG_DIRECT) mpi->flags |= MP_IMGFLAG_RENDERED;
#if LIBAVCODEC_BUILD >= 4689
	// Palette support: libavcodec copies palette to *data[1]
	if (mpi->bpp == 8)
		mpi->planes[1] = malloc(AVPALETTE_SIZE);
#endif

    pic->data[0]= mpi->planes[0];
    pic->data[1]= mpi->planes[1];
    pic->data[2]= mpi->planes[2];

    /* Note, some (many) codecs in libavcodec must have stride1==stride2 && no changes between frames
     * lavc will check that and die with an error message, if its not true
     */
    pic->linesize[0]= mpi->stride[0];
    pic->linesize[1]= mpi->stride[1];
    pic->linesize[2]= mpi->stride[2];

    pic->opaque = mpi;

    if(pic->reference){
        pic->age= vdff_ctx->ip_age[0];
        
        vdff_ctx->ip_age[0]= vdff_ctx->ip_age[1]+1;
        vdff_ctx->ip_age[1]= 1;
        vdff_ctx->b_age++;
    }else{
        pic->age= vdff_ctx->b_age;
    
        vdff_ctx->ip_age[0]++;
	vdff_ctx->ip_age[1]++;
        vdff_ctx->b_age=1;
    }
#if LIBAVCODEC_BUILD >= 4644
    pic->type= FF_BUFFER_TYPE_USER;
#endif
    return 0;
}

static void release_buffer(struct AVCodecContext *avctx, AVFrame *pic){
    mp_image_t* mpi= pic->opaque;
    sh_video_t * sh = avctx->opaque;
    priv_t *vdff_ctx = sh->context;
    int i;

  if(vdff_ctx->ip_count <= 2 && vdff_ctx->b_count<=1){
    if(mpi->flags&MP_IMGFLAG_PRESERVE)
        vdff_ctx->ip_count--;
    else
        vdff_ctx->b_count--;
  }

	// Palette support: free palette buffer allocated in get_buffer
	if ( mpi && (mpi->bpp == 8) && (mpi->planes[1] != NULL))
		free(mpi->planes[1]);

#if LIBAVCODEC_BUILD >= 4644
    if(pic->type!=FF_BUFFER_TYPE_USER){
        (*avcodec_default_release_buffer_ptr)(avctx, pic);
        return;
    }
#endif

    for(i=0; i<4; i++){
        pic->data[i]= NULL;
    }
//printf("R%X %X\n", pic->linesize[0], pic->data[0]);
}


static void draw_slice(struct AVCodecContext *s,
                        const AVFrame *src, int offset[4],
                        int y, int type, int height)
{
    sh_video_t *sh=s->opaque;
    priv_t *vdff_ctx=sh->context;
    mp_image_t *mpi;
    if(vdff_ctx->use_dr1) { MSG_DBG2("Ignoring draw_slice due dr1\n"); return; } /* we may call vo_start_slice() here */
    mpi=mpcodecs_get_image(sh,MP_IMGTYPE_EXPORT, MP_IMGFLAG_ACCEPT_STRIDE|MP_IMGFLAG_DRAW_CALLBACK|MP_IMGFLAG_DIRECT,s->width,height);

    mpi->stride[0]=src->linesize[0];
    mpi->stride[1]=src->linesize[1];
    mpi->stride[2]=src->linesize[2];
    mpi->planes[0] = src->base[0]+offset[0];
    mpi->planes[1] = src->base[1]+offset[1];
    mpi->planes[2] = src->base[2]+offset[2];
    mpi->w=s->width;
    mpi->y=y;
    mpi->h=height;
    /* provide info for pp */
    mpi->qscale=(QP_STORE_T *)vdff_ctx->lavc_picture->qscale_table;
    mpi->qstride=vdff_ctx->lavc_picture->qstride;
    mpi->pict_type=vdff_ctx->lavc_picture->pict_type;
    mpi->qscale_type=vdff_ctx->lavc_picture->qscale_type;
    
    if(sh->codec->outfmt[sh->outfmtidx] == IMGFMT_I420 ||
       sh->codec->outfmt[sh->outfmtidx] == IMGFMT_IYUV)
    {
	uint8_t *tmp;
	unsigned ls;
	tmp=mpi->planes[2];
	mpi->planes[2]=mpi->planes[1];
	mpi->planes[1]=tmp;
	ls=mpi->stride[2];
	mpi->stride[2]=mpi->stride[1];
	mpi->stride[1]=ls;
    }
    MSG_DBG2("ff_draw_callback %i %i %i %i\n",mpi->x,mpi->y,mpi->w,mpi->h);
    mpcodecs_draw_slice (sh, mpi);
}

/* copypaste from demux_real.c - it should match to get it working!*/

typedef struct __attribute__((__packed__)) dp_hdr_s {
    uint32_t chunks;
    uint32_t timestamp;
    uint32_t len;
    uint32_t chunktab;
} dp_hdr_t;

// decode a frame
static mp_image_t* decode(sh_video_t *sh,void* data,int len,int flags){
    int got_picture=0;
    int ret,has_b_frames;
    priv_t *vdff_ctx=sh->context;
    mp_image_t* mpi=NULL;
    void *next_put_slice;
    vf_instance_t *vf;

    vdff_ctx->ctx->opaque=sh;
    if(len<=0) return NULL; // skipped frame

    vdff_ctx->ctx->hurry_up=(flags&3)?((flags&2)?2:1):0;
    if(vdff_ctx->cap_slices)	vdff_ctx->use_slices=(divx_quality&&npp_options)?0:vdff_ctx->ctx->hurry_up?0:1;
    else          		vdff_ctx->use_slices=0;
/*
    if codec is capable DR1
    if sh->vfilter==vf_vo (DR1 is meaningless into temp buffer)
    It always happens with (vidix+bus mastering), (if (disp_w%16==0)) with xv
*/
    vf=sh->vfilter;
    next_put_slice=NULL;
    while(vf)
    {
	/* exclude vf_aspect and similar */
	if(vf->put_slice != vf_next_put_slice && vf->next) next_put_slice=vf->put_slice;
	vf=vf->next;
    }
    has_b_frames=vdff_ctx->ctx->has_b_frames||
		 sh->format==0x10000001 || /* mpeg1 may have b frames */
		 vdff_ctx->lavc_codec->id==CODEC_ID_SVQ3||
		 1;
    mpi= mpcodecs_get_image(sh,has_b_frames?MP_IMGTYPE_IPB:MP_IMGTYPE_IP,MP_IMGFLAG_ACCEPT_STRIDE|MP_IMGFLAG_PREFER_ALIGNED_STRIDE|MP_IMGFLAG_READABLE|MP_IMGFLAG_PRESERVE,
			    16,16);
    if(vdff_ctx->cap_dr1 &&
       vdff_ctx->lavc_codec->id != CODEC_ID_H264 &&
       !next_put_slice && mpi->flags&MP_IMGFLAG_DIRECT)
		vdff_ctx->use_dr1=1;
    if(   sh->format == mmioFOURCC('R', 'V', '1', '0')
       || sh->format == mmioFOURCC('R', 'V', '1', '3')
       || sh->format == mmioFOURCC('R', 'V', '2', '0')
       || sh->format == mmioFOURCC('R', 'V', '3', '0')
       || sh->format == mmioFOURCC('R', 'V', '4', '0'))
    if(sh->bih->biSize==sizeof(*sh->bih)+8){
        int i;
        dp_hdr_t *hdr= (dp_hdr_t*)data;

        if(vdff_ctx->ctx->slice_offset==NULL) 
            vdff_ctx->ctx->slice_offset= malloc(sizeof(int)*1000);

//        for(i=0; i<25; i++) printf("%02X ", ((uint8_t*)data)[i]);

        vdff_ctx->ctx->slice_count= hdr->chunks+1;
        for(i=0; i<vdff_ctx->ctx->slice_count; i++)
            vdff_ctx->ctx->slice_offset[i]= ((uint32_t*)(data+hdr->chunktab))[2*i+1];
	len=hdr->len;
        data+= sizeof(dp_hdr_t);
    }
    if(vdff_ctx->use_dr1){
	vdff_ctx->b_age= vdff_ctx->ip_age[0]= vdff_ctx->ip_age[1]= 256*256*256*64;
	vdff_ctx->ip_count= vdff_ctx->b_count= 0;
        vdff_ctx->ctx->get_buffer= get_buffer;
        vdff_ctx->ctx->release_buffer= release_buffer;
#if LIBAVCODEC_BUILD >= 4693
        vdff_ctx->ctx->reget_buffer= get_buffer;
#endif
    }
    if(!(flags&3) && vdff_ctx->use_slices)
    {
	mpi=mpcodecs_get_image(sh, MP_IMGTYPE_EXPORT, MP_IMGFLAG_ACCEPT_STRIDE|MP_IMGFLAG_DRAW_CALLBACK|MP_IMGFLAG_DIRECT,sh->disp_w, sh->disp_h);
	vdff_ctx->ctx->draw_horiz_band=draw_slice;
    }
    else vdff_ctx->ctx->draw_horiz_band=NULL; /* skip draw_slice on framedropping */

    ret = (*avcodec_decode_video_ptr)(vdff_ctx->ctx, vdff_ctx->lavc_picture,
	     &got_picture, data, len);
    
    if(ret<0) MSG_WARN("Error while decoding frame!\n");
    if(!got_picture) return NULL;	// skipped image
    if(!vdff_ctx->ctx->draw_horiz_band)
    {
	mpi=mpcodecs_get_image(sh, MP_IMGTYPE_EXPORT, MP_IMGFLAG_ACCEPT_STRIDE,sh->disp_w,sh->disp_h);
	if(!mpi){	// temporary!
	    MSG_ERR("couldn't allocate image for ffmpeg codec\n");
	    return NULL;
	}
	mpi->planes[0]=vdff_ctx->lavc_picture->data[0];
	mpi->planes[1]=vdff_ctx->lavc_picture->data[1];
	mpi->planes[2]=vdff_ctx->lavc_picture->data[2];
	mpi->stride[0]=vdff_ctx->lavc_picture->linesize[0];
	mpi->stride[1]=vdff_ctx->lavc_picture->linesize[1];
	mpi->stride[2]=vdff_ctx->lavc_picture->linesize[2];
	/* provide info for pp */
	mpi->qscale=(QP_STORE_T *)vdff_ctx->lavc_picture->qscale_table;
	mpi->qstride=vdff_ctx->lavc_picture->qstride;
	mpi->pict_type=vdff_ctx->lavc_picture->pict_type;
	mpi->qscale_type=vdff_ctx->lavc_picture->qscale_type;
	if(sh->codec->outfmt[sh->outfmtidx] == IMGFMT_I420 ||
	   sh->codec->outfmt[sh->outfmtidx] == IMGFMT_IYUV)
	{
	    uint8_t *tmp;
	    unsigned ls;
	    tmp=mpi->planes[2];
	    mpi->planes[2]=mpi->planes[1];
	    mpi->planes[1]=tmp;
	    ls=mpi->stride[2];
	    mpi->stride[2]=mpi->stride[1];
	    mpi->stride[1]=ls;
	}
	if(vdff_ctx->ctx->pix_fmt==PIX_FMT_YUV422P){
	    mpi->stride[1]*=2;
	    mpi->stride[2]*=2;
	}
    } /* endif use_slices */
    return mpi;
}

