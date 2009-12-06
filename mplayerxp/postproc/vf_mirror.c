#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "../config.h"
#include "pp_msg.h"

#include "../libvo/img_format.h"
#include "mp_image.h"
#include "vf.h"

#include "../libvo/fastmemcpy.h"
#include "../postproc/swscale.h"

typedef void (* __FASTCALL__ mirror_f)(unsigned char* dst,unsigned char* src,unsigned dststride,unsigned srcstride,unsigned w,unsigned h,unsigned bpp,unsigned int fmt);
struct vf_priv_s {
    unsigned dw,dh;
    int dir;
    mirror_f method;
};

static void __FASTCALL__ mirror_y(unsigned char* dst,unsigned char* src,unsigned dststride,unsigned srcstride,unsigned w,unsigned h,unsigned bpp,unsigned int fmt){
    int y;
    src+=srcstride*(h-1);
    for(y=0;y<h;y++){
	memcpy(dst,src,w*bpp);
	src-=srcstride;
	dst+=dststride;
    }
}

static void __FASTCALL__ mirror_x(unsigned char* dst,unsigned char* src,unsigned dststride,unsigned srcstride,unsigned w,unsigned h,unsigned bpp,unsigned int fmt){
    int y;
    for(y=0;y<h;y++){
	int x;
	switch(bpp){
	case 1:
	    for(x=0;x<w;x++) dst[x]=src[w-x-1];
	    break;
	case 2:
	    switch(fmt){
	    case IMGFMT_UYVY: {
		// packed YUV is tricky. U,V are 32bpp while Y is 16bpp:
		int w2=w>>1;
		for(x=0;x<w2;x++){
		    // TODO: optimize this...
		    dst[x*4+0]=src[0+(w2-x-1)*4];
		    dst[x*4+1]=src[3+(w2-x-1)*4];
		    dst[x*4+2]=src[2+(w2-x-1)*4];
		    dst[x*4+3]=src[1+(w2-x-1)*4];
		}
		break; }
	    case IMGFMT_YUY2:
	    case IMGFMT_YVYU: {
		// packed YUV is tricky. U,V are 32bpp while Y is 16bpp:
		int w2=w>>1;
		for(x=0;x<w2;x++){
		    // TODO: optimize this...
		    dst[x*4+0]=src[2+(w2-x-1)*4];
		    dst[x*4+1]=src[1+(w2-x-1)*4];
		    dst[x*4+2]=src[0+(w2-x-1)*4];
		    dst[x*4+3]=src[3+(w2-x-1)*4];
		}
		break; }
	    default:
		for(x=0;x<w;x++) *((short*)(dst+x*2))=*((short*)(src+(w-x-1)*2));
	    }
	    break;
	case 3:
	    for(x=0;x<w;x++){
		dst[x*3+0]=src[0+(w-x-1)*3];
		dst[x*3+1]=src[1+(w-x-1)*3];
		dst[x*3+2]=src[2+(w-x-1)*3];
	    }
	    break;
	case 4:
	    for(x=0;x<w;x++) *((int*)(dst+x*4))=*((int*)(src+(w-x-1)*4));
	}
	src+=srcstride;
	dst+=dststride;
    }
}

//===========================================================================//
static int __FASTCALL__ config(struct vf_instance_s* vf,
        int width, int height, int d_width, int d_height,
	unsigned int flags, unsigned int outfmt,void *tune){
    vf->priv->dw=width;
    vf->priv->dh=height;
    return vf_next_config(vf,width,height,d_width,d_height,flags,outfmt,tune);
}

static int __FASTCALL__ put_slice(struct vf_instance_s* vf, mp_image_t *mpi){
    mp_image_t *dmpi;

    // hope we'll get DR buffer:
    dmpi=vf_get_image(vf->next,mpi->imgfmt,
	MP_IMGTYPE_TEMP, MP_IMGFLAG_ACCEPT_STRIDE,
	mpi->w, mpi->h);
    if(mpi->flags&MP_IMGFLAG_PLANAR){
	    vf->priv->method(dmpi->planes[0],mpi->planes[0],
	       dmpi->stride[0],mpi->stride[0],
	       dmpi->w,dmpi->h,1,mpi->imgfmt);
	    vf->priv->method(dmpi->planes[1],mpi->planes[1],
	       dmpi->stride[1],mpi->stride[1],
	       dmpi->w>>mpi->chroma_x_shift,dmpi->h>>mpi->chroma_y_shift,1,mpi->imgfmt);
	    vf->priv->method(dmpi->planes[2],mpi->planes[2],
	       dmpi->stride[2],mpi->stride[2],
	       dmpi->w>>mpi->chroma_x_shift,dmpi->h>>mpi->chroma_y_shift,1,mpi->imgfmt);
    } else {
	    vf->priv->method(dmpi->planes[0],mpi->planes[0],
	       dmpi->stride[0],mpi->stride[0],
	       dmpi->w,dmpi->h,dmpi->bpp>>3,mpi->imgfmt);
	    dmpi->planes[1]=mpi->planes[1]; // passthrough rgb8 palette
    }
    return vf_next_put_slice(vf,dmpi);
}

//===========================================================================//

static int __FASTCALL__ vf_open(vf_instance_t *vf,const char* args){
    int dir;
    vf->config=config;
    vf->put_slice=put_slice;
    vf->priv=malloc(sizeof(struct vf_priv_s));
    dir=1;
    if(args)  dir=args[0]=='x'?1:args[0]=='y'?0:-1;
    if(dir==-1)
    {
	MSG_ERR("[vf_mirror] unknown directoin: %c\n",args[0]);
	return 0;
    }
    if(dir==0)	vf->priv->method=mirror_x;
    else	vf->priv->method=mirror_y;
    vf->priv->dir=dir;
    return 1;
}

const vf_info_t vf_info_mirror = {
    "horizontal mirror",
    "mirror",
    "Eyck",
    "",
    VF_FLAGS_THREADS,
    vf_open
};

//===========================================================================//
