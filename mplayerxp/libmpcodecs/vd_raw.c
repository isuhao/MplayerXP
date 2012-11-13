#include <stdio.h>
#include <stdlib.h>

#include "mp_config.h"

#include "vd_internal.h"

static const vd_info_t info = {
    "RAW Uncompressed Video",
    "raw",
    "A'rpi & Alex",
    "build-in"
};

static const config_t options[] = {
  { NULL, NULL, 0, 0, 0, 0, NULL}
};

LIBVD_EXTERN(raw)

// to set/get/query special features/parameters
static MPXP_Rc control(sh_video_t *sh,int cmd,any_t* arg,...){
    switch(cmd) {
	case VDCTRL_QUERY_FORMAT:
	    return MPXP_True;
	default: break;
    }
    return MPXP_Unknown;
}

// init driver
static MPXP_Rc init(sh_video_t *sh,any_t* libinput){
    // set format fourcc for raw RGB:
    if(sh->fourcc==0){
	switch(sh->bih->biBitCount){
	case 8: sh->fourcc=IMGFMT_BGR8; break;
	case 15:
	case 16: sh->fourcc=IMGFMT_BGR15; break;
	case 24: sh->fourcc=IMGFMT_BGR24; break;
	case 32: sh->fourcc=IMGFMT_BGR32; break;
	default:
	    MSG_WARN("RAW: depth %d not supported\n",sh->bih->biBitCount);
	}
    }
    return mpcodecs_config_vo(sh,sh->src_w,sh->src_h,libinput);
}

// uninit driver
static void uninit(sh_video_t *sh) { UNUSED(sh); }

// decode a frame
static mp_image_t* decode(sh_video_t *sh,const enc_frame_t* frame){
    mp_image_t* mpi;
    if(frame->len<=0) return NULL; // skipped frame

    mpi=mpcodecs_get_image(sh, MP_IMGTYPE_EXPORT, 0, sh->src_w, sh->src_h);
    if(mpi->flags&MP_IMGFLAG_DIRECT) mpi->flags|=MP_IMGFLAG_RENDERED;

    if(mpi->flags&MP_IMGFLAG_PLANAR){
	mpi->planes[0]=frame->data;
	mpi->stride[0]=mpi->width;
	switch(sh->codec->outfmt[sh->outfmtidx])
	{
	    default:
	    case IMGFMT_I420:
	    case IMGFMT_IYUV:
	    case IMGFMT_YV12:
		mpi->planes[1]=frame->data+mpi->width*mpi->height;
		mpi->stride[1]=mpi->width/2;
		mpi->planes[2]=mpi->planes[1]+(mpi->width/2)*(mpi->height/2);
		mpi->stride[2]=mpi->width/2;
		break;
	    case IMGFMT_IF09:
		/*
		skipped direction level:
		mpi->planes[3]=data+mpi->width*mpi->height*10/8;
		mpi->stride[3]=mpi->width/4;
		*/
	    case IMGFMT_YVU9:
		mpi->planes[1]=frame->data+mpi->width*mpi->height;
		mpi->stride[1]=mpi->width/4;
		mpi->planes[2]=mpi->planes[1]+(mpi->width/4)*(mpi->height/4);
		mpi->stride[2]=mpi->width/4;
		break;
	    case IMGFMT_Y800:
		break;
	}
    } else {
	mpi->planes[0]=frame->data;
	mpi->stride[0]=mpi->width*((mpi->bpp+7)/8);
    }
    return mpi;
}

