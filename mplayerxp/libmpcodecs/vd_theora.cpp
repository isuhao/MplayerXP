#include "mp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <dlfcn.h>

#include <theora/theora.h>

#include "help_mp.h"
#include "codecs_ld.h"

#include "vd_internal.h"
#include "vd_msg.h"
#include "osdep/bswap.h"

static const vd_info_t info = {
   "Theora/VP3 video decoder",
   "theora",
   "David Kuehling (www.theora.org)",
   "build-in"
};

static const config_t options[] = {
  { NULL, NULL, 0, 0, 0, 0, NULL}
};

LIBVD_EXTERN(theora)

static const video_probe_t probes[] = {
    { "theora", "libtheora", FOURCC_TAG('T','H','E','O'), VCodecStatus_Problems, {IMGFMT_YV12,IMGFMT_422P,IMGFMT_444P}, {VideoFlag_None, VideoFlag_None } },
    { "theora", "libtheora", FOURCC_TAG('T','H','R','A'), VCodecStatus_Problems, {IMGFMT_YV12,IMGFMT_422P,IMGFMT_444P}, {VideoFlag_None, VideoFlag_None } },
    { NULL, NULL, 0x0, VCodecStatus_NotWorking, {0x0}, { VideoFlag_None }}
};


#define THEORA_NUM_HEADER_PACKETS 3

struct vd_private_t {
    theora_state	st;
    theora_comment	cc;
    theora_info		inf;
    sh_video_t*		sh;
    video_decoder_t*	parent;
};

static const video_probe_t* __FASTCALL__ probe(vd_private_t *priv,uint32_t fourcc) {
    UNUSED(priv);
    unsigned i;
    for(i=0;probes[i].driver;i++)
	if(fourcc==probes[i].fourcc)
	    return &probes[i];
    return NULL;
}

// to set/get/query special features/parameters
static MPXP_Rc control_vd(vd_private_t *priv,int cmd,any_t* arg,...){
    UNUSED(priv);
    switch(cmd) {
	case VDCTRL_QUERY_FORMAT:
	    if (*((int*)arg) == IMGFMT_YV12)
			return MPXP_True;
	    else	return MPXP_False;
	default: break;
    }
    return MPXP_Unknown;
}

static vd_private_t* preinit(sh_video_t *sh,put_slice_info_t* psi){
    UNUSED(psi);
    vd_private_t* priv = new(zeromem) vd_private_t;
    priv->sh=sh;
    return priv;
}

/*
 * init driver
 */
static MPXP_Rc init(vd_private_t *priv,video_decoder_t* opaque){
    sh_video_t* sh = priv->sh;
    int failed = 1;
    int errorCode = 0;
    ogg_packet op;
    int i;
    float pts;

    /* check whether video output format is supported */
    switch(sh->codec->outfmt[sh->outfmtidx]) {
	case IMGFMT_YV12: /* well, this should work... */ break;
	default:
	    MSG_ERR("Unsupported out_fmt: 0x%X\n", sh->codec->outfmt[sh->outfmtidx]);
	return MPXP_False;
    }

    /* this is not a loop, just a context, from which we can break on error */
    do {
	priv->parent = opaque;

	theora_info_init(&priv->inf);
	theora_comment_init(&priv->cc);

	/* Read all header packets, pass them to theora_decode_header. */
	for (i = 0; i < THEORA_NUM_HEADER_PACKETS; i++) {
	    op.bytes = ds_get_packet_r (sh->ds, &op.packet,&pts);
	    op.b_o_s = 1;
	    if ( (errorCode = theora_decode_header (&priv->inf, &priv->cc, &op))) {
		MSG_ERR("Broken Theora header; errorCode=%i!\n", errorCode);
		break;
	    }
	}
	if (errorCode) break;

	/* now init codec */
	errorCode = theora_decode_init (&priv->st, &priv->inf);
	if (errorCode) {
	    MSG_ERR("Theora decode init failed: %i \n", errorCode);
	    break;
	}
	failed = 0;
    } while (0);

    if (failed) return MPXP_False;

    if(sh->aspect==0.0 && priv->inf.aspect_denominator!=0) {
	sh->aspect = (float)(priv->inf.aspect_numerator * priv->inf.frame_width)/
		(priv->inf.aspect_denominator * priv->inf.frame_height);
    }

    MSG_V("INFO: Theora video init ok!\n");

    return mpcodecs_config_vf(opaque,sh->src_w,sh->src_h);
}

/*
 * uninit driver
 */
static void uninit(vd_private_t *priv)
{
    theora_clear (&priv->st);
    delete priv;
}

/*
 * decode frame
 */
static mp_image_t* decode(vd_private_t *priv,const enc_frame_t* frame)
{
    sh_video_t* sh = priv->sh;
    int errorCode = 0;
    ogg_packet op;
    yuv_buffer yuv;
    mp_image_t* mpi;

    bzero (&op, sizeof (op));
    op.bytes = frame->len;
    op.packet = reinterpret_cast<unsigned char*>(frame->data);
    op.granulepos = -1;

    errorCode = theora_decode_packetin (&priv->st, &op);
    if (errorCode) {
	MSG_ERR("Theora decode packetin failed: %i \n",
	     errorCode);
	return NULL;
    }

    errorCode = theora_decode_YUVout (&priv->st, &yuv);
    if (errorCode) {
	MSG_ERR("Theora decode YUVout failed: %i \n",
	     errorCode);
	return NULL;
    }

    mpi = mpcodecs_get_image(priv->parent, MP_IMGTYPE_EXPORT, 0,sh->src_w, sh->src_h);

    mpi->planes[0]=yuv.y;
    mpi->stride[0]=yuv.y_stride;
    mpi->planes[1]=yuv.u;
    mpi->stride[1]=yuv.uv_stride;
    mpi->planes[2]=yuv.v;
    mpi->stride[2]=yuv.uv_stride;

    return mpi;
}
