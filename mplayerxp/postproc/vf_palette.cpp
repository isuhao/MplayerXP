#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace	usr;
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "libvo2/img_format.h"
#include "xmpcore/xmp_image.h"
#include "vf.h"
#include "vf_internal.h"

#include "postproc/swscale.h"
#include "pp_msg.h"
//===========================================================================//

// commented out 16 and 15 bit output support, because the conversion
// routines are incorrrect.  they assume the palette to be of the same
// depth as the output, which is incorrect. --Joey

static unsigned int bgr_list[]={
    IMGFMT_BGR15,
    IMGFMT_BGR16,
    IMGFMT_BGR24,
    IMGFMT_BGR32,
    0
};
static unsigned int rgb_list[]={
    IMGFMT_RGB15,
    IMGFMT_RGB16,
    IMGFMT_RGB24,
    IMGFMT_RGB32,
    0
};

static unsigned int gray_pal[256];

static unsigned int __FASTCALL__ find_best(vf_instance_t* vf, unsigned int fmt,unsigned w,unsigned h){
    unsigned int best=0;
    int ret;
    unsigned int* p;
    if(fmt==IMGFMT_BGR8) p=bgr_list;
    else if(fmt==IMGFMT_RGB8) p=rgb_list;
    else return 0;
    while(*p){
	ret=vf_next_query_format(vf,*p,w,h);
	mpxp_dbg2<<"["<<vf->info->name<<"] query("<<vo_format_name(*p)<<") -> "<<(ret&3)<<std::endl;
	if(ret&VFCAP_CSP_SUPPORTED_BY_HW){ best=*p; break;} // no conversion -> bingo!
	if(ret&VFCAP_CSP_SUPPORTED && !best) best=*p; // best with conversion
	++p;
    }
    return best;
}

//===========================================================================//

struct vf_priv_t {
    unsigned int fmt;
    int pal_msg;
};

static int __FASTCALL__ vf_config(vf_instance_t* vf,
	int width, int height, int d_width, int d_height,
	vo_flags_e flags, unsigned int outfmt){
    if (!vf->priv->fmt)
	vf->priv->fmt=find_best(vf,outfmt,d_width,d_height);
    if(!vf->priv->fmt){
	// no matching fmt, so force one...
	if(outfmt==IMGFMT_RGB8) vf->priv->fmt=IMGFMT_RGB32;
	else if(outfmt==IMGFMT_BGR8) vf->priv->fmt=IMGFMT_BGR32;
	else return 0;
    }
    return vf_next_config(vf,width,height,d_width,d_height,flags,vf->priv->fmt);
}

static int __FASTCALL__ put_slice(vf_instance_t* vf,const mp_image_t& mpi){
    mp_image_t *dmpi;
    mp_image_t smpi = mpi;

    // hope we'll get DR buffer:
    dmpi=vf_get_new_image(vf->next,vf->priv->fmt,
	MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE,
	smpi.w, smpi.h, smpi.xp_idx);

    if (!smpi.planes[1])
    {
	if(!vf->priv->pal_msg){
	    mpxp_v<<"["<<vf->info->name<<"] no palette given, assuming builtin grayscale one"<<std::endl;
	    vf->priv->pal_msg=1;
	}
	smpi.planes[1] = (unsigned char*)gray_pal;
    }

    if(smpi.w==smpi.stride[0] && dmpi->w*(dmpi->bpp>>3)==dmpi->stride[0]){
	// no stride conversion needed
	switch(dmpi->imgfmt&255){
	case 15:
	    if (dmpi->flags & MP_IMGFLAG_SWAPPED)
		palette8tobgr15(smpi.planes[0],dmpi->planes[0],smpi.h*smpi.w,smpi.planes[1]);
	    else
		palette8torgb15(smpi.planes[0],dmpi->planes[0],smpi.h*smpi.w,smpi.planes[1]);
	    break;
	case 16:
	    if (dmpi->flags & MP_IMGFLAG_SWAPPED)
		palette8tobgr16(smpi.planes[0],dmpi->planes[0],smpi.h*smpi.w,smpi.planes[1]);
	    else
		palette8torgb16(smpi.planes[0],dmpi->planes[0],smpi.h*smpi.w,smpi.planes[1]);
	    break;
	case 24:
	    if (dmpi->flags & MP_IMGFLAG_SWAPPED)
		palette8tobgr24(smpi.planes[0],dmpi->planes[0],smpi.h*smpi.w,smpi.planes[1]);
	    else
		palette8torgb24(smpi.planes[0],dmpi->planes[0],smpi.h*smpi.w,smpi.planes[1]);
	    break;
	case 32:
	    if (dmpi->flags & MP_IMGFLAG_SWAPPED)
		palette8tobgr32(smpi.planes[0],dmpi->planes[0],smpi.h*smpi.w,smpi.planes[1]);
	    else
		palette8torgb32(smpi.planes[0],dmpi->planes[0],smpi.h*smpi.w,smpi.planes[1]);
	    break;
	}
    } else {
	int y;
	for(y=0;y<smpi.h;y++){
	    unsigned char* src=smpi.planes[0]+y*smpi.stride[0];
	    unsigned char* dst=dmpi->planes[0]+y*dmpi->stride[0];
	    switch(dmpi->imgfmt&255){
	    case 15:
		if (dmpi->flags & MP_IMGFLAG_SWAPPED)
		    palette8tobgr15(src,dst,smpi.w,smpi.planes[1]);
		else
		    palette8torgb15(src,dst,smpi.w,smpi.planes[1]);
		break;
	    case 16:
		if (dmpi->flags & MP_IMGFLAG_SWAPPED)
		    palette8tobgr16(src,dst,smpi.w,smpi.planes[1]);
		else
		    palette8torgb16(src,dst,smpi.w,smpi.planes[1]);
		break;
	    case 24:
		if (dmpi->flags & MP_IMGFLAG_SWAPPED)
		    palette8tobgr24(src,dst,smpi.w,smpi.planes[1]);
		else
		    palette8torgb24(src,dst,smpi.w,smpi.planes[1]);
		break;
	    case 32:
		if (dmpi->flags & MP_IMGFLAG_SWAPPED)
		    palette8tobgr32(src,dst,smpi.w,smpi.planes[1]);
		else
		    palette8torgb32(src,dst,smpi.w,smpi.planes[1]);
		break;
	    }
	}
    }

    return vf_next_put_slice(vf,*dmpi);
}

//===========================================================================//

static int __FASTCALL__ query_format(vf_instance_t* vf, unsigned int fmt,unsigned w,unsigned h){
    int best=find_best(vf,fmt,w,h);
    if(!best) return 0; // no match
    return vf_next_query_format(vf->next,best,w,h);
}

static MPXP_Rc __FASTCALL__ vf_open(vf_instance_t *vf,const char* args){
    unsigned int i;
    vf->config_vf=vf_config;
    vf->put_slice=put_slice;
    vf->query_format=query_format;
    vf->priv=new(zeromem) vf_priv_t;
    for(i=0;i<256;i++) gray_pal[i]=0x01010101*i;
    if (args) {
	if (!strcasecmp(args,"rgb15")) vf->priv->fmt=IMGFMT_RGB15; else
	if (!strcasecmp(args,"rgb16")) vf->priv->fmt=IMGFMT_RGB16; else
	if (!strcasecmp(args,"rgb24")) vf->priv->fmt=IMGFMT_RGB24; else
	if (!strcasecmp(args,"rgb32")) vf->priv->fmt=IMGFMT_RGB32; else
	if (!strcasecmp(args,"bgr15")) vf->priv->fmt=IMGFMT_BGR15; else
	if (!strcasecmp(args,"bgr16")) vf->priv->fmt=IMGFMT_BGR16; else
	if (!strcasecmp(args,"bgr24")) vf->priv->fmt=IMGFMT_BGR24; else
	if (!strcasecmp(args,"bgr32")) vf->priv->fmt=IMGFMT_BGR32; else
	{
	    printf("Unknown forced format name: '%s'\n", args);
	    return MPXP_False;
	}
    }
    check_pin("vfilter",vf->pin,VF_PIN);
    return MPXP_Ok;
}

extern const vf_info_t vf_info_palette = {
    "8bpp indexed (using palette) -> BGR 15/16/24/32 conversion",
    "palette",
    "A'rpi & Alex",
    "",
    VF_FLAGS_THREADS,
    vf_open
};

//===========================================================================//
