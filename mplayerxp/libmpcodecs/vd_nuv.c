#include <stdio.h>
#include <stdlib.h>
#include "mp_config.h"
#include "libnuppelvideo/nuppelvideo.h"
#include "vd_internal.h"
#include "codecs_ld.h"

static const vd_info_t info = {
	"NuppelVideo decoder",
	"nuv",
	"A'rpi (Alex & Panagiotis Issaris <takis@lumumba.luc.ac.be>)",
	"build-in"
};

static const config_t options[] = {
  { NULL, NULL, 0, 0, 0, 0, NULL}
};

LIBVD_EXTERN(nuv)

// to set/get/query special features/parameters
static MPXP_Rc control(sh_video_t *sh,int cmd,any_t* arg,...){
    switch(cmd) {
      case VDCTRL_QUERY_FORMAT:
	    if (*((int*)arg) == IMGFMT_I420 ||
		*((int*)arg) == IMGFMT_IYUV) 
			return MPXP_True;
	    else 	return MPXP_False;
      default: break;
    }
    return MPXP_Unknown;
}

// init driver
static int init(sh_video_t *sh){
    return mpcodecs_config_vo(sh,sh->src_w,sh->src_h,NULL);
}

// uninit driver
static void uninit(sh_video_t *sh){
}

// decode a frame
static mp_image_t* decode(sh_video_t *sh,any_t* data,int len,int flags){
    mp_image_t* mpi;
    if(len<=0) return NULL; // skipped frame

    mpi=mpcodecs_get_image(sh, MP_IMGTYPE_TEMP, 0,
	sh->src_w, sh->src_h);
    if(mpi->flags&MP_IMGFLAG_DIRECT) mpi->flags|=MP_IMGFLAG_RENDERED;

    decode_nuv(data, len, mpi->planes[0], sh->src_w, sh->src_h);

    return mpi;
}
