#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "mp_config.h"

#include "libvo/img_format.h"
#include "xmpcore/mp_image.h"
#include "vf.h"

#include "osdep/fastmemcpy.h"
#include "osdep/mplib.h"
#include "mp_conf_lavc.h"
#include "pp_msg.h"

//===========================================================================//

struct vf_priv_s {
    unsigned int fmt;
    int forced;
};

static unsigned int __FASTCALL__ getfmt(unsigned int outfmt,int forced){
    if(forced) switch(outfmt){
    case IMGFMT_RGB24:
    case IMGFMT_RGB32:
    case IMGFMT_BGR24:
    case IMGFMT_BGR32:
	return outfmt;
    }
    switch(outfmt){
    case IMGFMT_RGB24: return IMGFMT_BGR24;
    case IMGFMT_RGB32: return IMGFMT_BGR32;
    case IMGFMT_BGR24: return IMGFMT_RGB24;
    case IMGFMT_BGR32: return IMGFMT_RGB32;
    }
    return 0;
}

static int __FASTCALL__ vf_config(struct vf_instance_s* vf,
	int width, int height, int d_width, int d_height,
	unsigned int flags, unsigned int outfmt){
    vf->priv->fmt=getfmt(outfmt,vf->priv->forced);
    return vf_next_config(vf,width,height,d_width,d_height,flags,vf->priv->fmt);
}

#define rgb32tobgr32(a,b,c) shuffle_bytes_3210(a,b,c)

static int __FASTCALL__ put_slice(struct vf_instance_s* vf, mp_image_t *mpi){
    mp_image_t *dmpi;

    // hope we'll get DR buffer:
    dmpi=vf_get_new_image(vf->next,vf->priv->fmt,
	MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE,
	mpi->w, mpi->h, mpi->xp_idx);

    if(mpi->stride[0]!=dmpi->stride[0] || mpi->stride[0]!=mpi->w*(mpi->bpp/8)){
	int y;
	unsigned char* src=mpi->planes[0];
	unsigned char* dst=dmpi->planes[0];
	int srcsize=mpi->w*mpi->bpp/8;
	for(y=0;y<mpi->h;y++){
	    if(mpi->bpp==32)
		rgb32tobgr32(src,dst,srcsize);
	    else
		rgb24tobgr24(src,dst,srcsize);
	    src+=mpi->stride[0];
	    dst+=dmpi->stride[0];
	}
    } else {
	if(mpi->bpp==32)
	    rgb32tobgr32(mpi->planes[0],dmpi->planes[0],mpi->w*mpi->h*4);
	else
	    rgb24tobgr24(mpi->planes[0],dmpi->planes[0],mpi->w*mpi->h*3);
    }

    return vf_next_put_slice(vf,dmpi);
}

//===========================================================================//

static int __FASTCALL__ query_format(struct vf_instance_s* vf, unsigned int outfmt,unsigned w,unsigned h){
    unsigned int fmt=getfmt(outfmt,vf->priv->forced);
    if(!fmt) return 0;
    return vf_next_query_format(vf,fmt,w,h) & (~VFCAP_CSP_SUPPORTED_BY_HW);
}

static MPXP_Rc __FASTCALL__ vf_open(vf_instance_t *vf,const char* args){
    vf->config=vf_config;
    vf->put_slice=put_slice;
    vf->query_format=query_format;
    vf->priv=new(zeromem) struct vf_priv_s;
    vf->priv->forced=args && !strcasecmp(args,"swap");
    check_pin("vfilter",vf->pin,VF_PIN);
    return MPXP_Ok;
}

extern const vf_info_t vf_info_rgb2bgr = {
    "fast 24/32bpp RGB<->BGR conversion",
    "rgb2bgr",
    "A'rpi",
    "",
    VF_FLAGS_THREADS,
    vf_open
};

//===========================================================================//