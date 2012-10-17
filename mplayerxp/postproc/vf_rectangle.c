#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mp_image.h"
#include "vf.h"

#include "../libvo/fastmemcpy.h"
#include "pp_msg.h"

struct vf_priv_s {
    int x, y, w, h;
};

static int __FASTCALL__ config(struct vf_instance_s* vf,
       int width, int height, int d_width, int d_height,
       unsigned int flags, unsigned int outfmt,any_t*tune)
{
    if (vf->priv->w < 0 || width < vf->priv->w)
	vf->priv->w = width;
    if (vf->priv->h < 0 || height < vf->priv->h)
	vf->priv->h = height;
    if (vf->priv->x < 0)
	vf->priv->x = (width - vf->priv->w) / 2;
    if (vf->priv->y < 0)
	vf->priv->y = (height - vf->priv->h) / 2;
    if (vf->priv->w + vf->priv->x > width
	|| vf->priv->h + vf->priv->y > height) {
	MSG_WARN("rectangle: bad position/width/height - rectangle area is out of the original!\n");
	return 0;
    }
    return vf_next_config(vf, width, height, d_width, d_height, flags, outfmt,tune);
}

static int __FASTCALL__ control(struct vf_instance_s* vf, int request, any_t*data)
{
    const int *const tmp = data;
    switch(request){
    case VFCTRL_CHANGE_RECTANGLE:
	switch (tmp[0]){
	case 0:
	    vf->priv->w += tmp[1];
	    return 1;
	    break;
	case 1:
	    vf->priv->h += tmp[1];
	    return 1;
	    break;
	case 2:
	    vf->priv->x += tmp[1];
	    return 1;
	    break;
	case 3:
	    vf->priv->y += tmp[1];
	    return 1;
	    break;
	default:
	    MSG_FATAL("Unknown param %d \n", tmp[0]);
	    return 0;
	}
    }
    return vf_next_control(vf, request, data);
    return 0;
}
static int __FASTCALL__ put_slice(struct vf_instance_s* vf, mp_image_t* mpi){
    mp_image_t* dmpi;
    int finalize;
    unsigned int bpp = mpi->bpp / 8;
    unsigned int x, y, w, h;
    dmpi = vf_get_image(vf->next, mpi->imgfmt, MP_IMGTYPE_TEMP,
			MP_IMGFLAG_ACCEPT_STRIDE | MP_IMGFLAG_PREFER_ALIGNED_STRIDE,
			mpi->w, mpi->h);
    finalize = dmpi->flags&MP_IMGFLAG_FINALIZED;

    if(finalize)
    stream_copy_pic(dmpi->planes[0],mpi->planes[0],mpi->w*bpp, mpi->h,
		    dmpi->stride[0],mpi->stride[0]);
    else
    memcpy_pic(dmpi->planes[0],mpi->planes[0],mpi->w*bpp, mpi->h,
	       dmpi->stride[0],mpi->stride[0]);
    if(mpi->flags&MP_IMGFLAG_PLANAR && mpi->flags&MP_IMGFLAG_YUV){
	if(finalize) {
	stream_copy_pic(dmpi->planes[1],mpi->planes[1],
		   mpi->w>>mpi->chroma_x_shift, mpi->h>>mpi->chroma_y_shift,
	           dmpi->stride[1],mpi->stride[1]);
	stream_copy_pic(dmpi->planes[2],mpi->planes[2],
		   mpi->w>>mpi->chroma_x_shift, mpi->h>>mpi->chroma_y_shift,
	           dmpi->stride[2],mpi->stride[2]);
	} else {
	memcpy_pic(dmpi->planes[1],mpi->planes[1],
		   mpi->w>>mpi->chroma_x_shift, mpi->h>>mpi->chroma_y_shift,
	           dmpi->stride[1],mpi->stride[1]);
	memcpy_pic(dmpi->planes[2],mpi->planes[2],
		   mpi->w>>mpi->chroma_x_shift, mpi->h>>mpi->chroma_y_shift,
	           dmpi->stride[2],mpi->stride[2]);
	}
    }

    /* Draw the rectangle */

    MSG_DBG2( "rectangle: -vf rectangle=%d:%d:%d:%d \n", vf->priv->w, vf->priv->h, vf->priv->x, vf->priv->y);

    if (vf->priv->x < 0)
	x = 0;
    else if (dmpi->width < vf->priv->x)
	x = dmpi->width;
    else
	x = vf->priv->x;
    if (vf->priv->x + vf->priv->w - 1 < 0)
	w = vf->priv->x + vf->priv->w - 1 - x;
    else if (dmpi->width < vf->priv->x + vf->priv->w - 1)
	w = dmpi->width - x;
    else
	w = vf->priv->x + vf->priv->w - 1 - x;
    if (vf->priv->y < 0)
	y = 0;
    else if (dmpi->height < vf->priv->y)
	y = dmpi->height;
    else
	y = vf->priv->y;
    if (vf->priv->y + vf->priv->h - 1 < 0)
	h = vf->priv->y + vf->priv->h - 1 - y;
    else if (dmpi->height < vf->priv->y + vf->priv->h - 1)
	h = dmpi->height - y;
    else
	h = vf->priv->y + vf->priv->h - 1 - y;

    if (0 <= vf->priv->y && vf->priv->y <= dmpi->height) {
	unsigned char *p = dmpi->planes[0] + y * dmpi->stride[0] + x * bpp;
	unsigned int count = w * bpp;
	while (count--)
	    p[count] = 0xff - p[count];
    }
    if (h != 1 && vf->priv->y + vf->priv->h - 1 <= mpi->height) {
	unsigned char *p = dmpi->planes[0] + (vf->priv->y + vf->priv->h - 1) * dmpi->stride[0] + x * bpp;
	unsigned int count = w * bpp;
	while (count--)
	    p[count] = 0xff - p[count];
    }
    if (0 <= vf->priv->x  && vf->priv->x <= dmpi->width) {
	unsigned char *p = dmpi->planes[0] + y * dmpi->stride[0] + x * bpp;
	unsigned int count = h;
	while (count--) {
	    unsigned int i = bpp;
	    while (i--)
		p[i] = 0xff - p[i];
	    p += dmpi->stride[0];
	}
    }
    if (w != 1 && vf->priv->x + vf->priv->w - 1 <= mpi->width) {
	unsigned char *p = dmpi->planes[0] + y * dmpi->stride[0] + (vf->priv->x + vf->priv->w - 1) * bpp;
	unsigned int count = h;
	while (count--) {
	    unsigned int i = bpp;
	    while (i--)
		p[i] = 0xff - p[i];
	    p += dmpi->stride[0];
	}
    }
    return vf_next_put_slice(vf, dmpi);
}

static int __FASTCALL__ vf_open(vf_instance_t* vf,const char* args) {
    vf->config = config;
    vf->control = control;
    vf->put_slice = put_slice;
    vf->priv = malloc(sizeof(struct vf_priv_s));
    vf->priv->x = -1;
    vf->priv->y = -1;
    vf->priv->w = -1;
    vf->priv->h = -1;
    if (args)
	sscanf(args, "%d:%d:%d:%d", 
	       &vf->priv->w, &vf->priv->h, &vf->priv->x, &vf->priv->y);
    return 1;
}

const vf_info_t vf_info_rectangle = {
    "draw rectangle",
    "rectangle",
    "Kim Minh Kaplan",
    "",
    VF_FLAGS_THREADS,
    vf_open
};
