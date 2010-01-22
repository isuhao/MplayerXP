#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mp_config.h"

#include "mp_image.h"
#include "vf.h"
#include "vfcap.h"
#include "../libvo/video_out.h"
#include "../libvo/dri_vo.h"
#include "pp_msg.h"

//===========================================================================//
struct vf_priv_s {
    int is_planar;
    int sw,sh,dw,dh,sflg;
    int ofmt;
    vo_format_desc vd;
};
static int vo_config_count;
static int __FASTCALL__ query_format(struct vf_instance_s* vf, unsigned int fmt,unsigned w,unsigned h); /* forward declaration */

static void __FASTCALL__ print_conf(struct vf_instance_s* vf)
{
    const vo_info_t *info = vo_get_info();
    MSG_INFO("VO-CONF: [%s] %dx%d => %dx%d %s %s%s%s%s\n",info->short_name,
         vf->priv->sw, vf->priv->sh,
         vf->priv->dw, vf->priv->dh,
	 vo_format_name(vf->priv->ofmt),
         (vf->priv->sflg&1)?" [fs]":"",
         (vf->priv->sflg&2)?" [vm]":"",
         (vf->priv->sflg&4)?" [zoom]":"",
         (vf->priv->sflg&8)?" [flip]":"");
    MSG_V("VO: Description: %s\n",info->name);
    MSG_V("VO: Author: %s\n", info->author);
    if(info->comment && strlen(info->comment) > 0)
        MSG_V("VO: Comment: %s\n", info->comment);
}

static int __FASTCALL__ config(struct vf_instance_s* vf,
        int width, int height, int d_width, int d_height,
	unsigned int flags, unsigned int outfmt,void *tune){

    if ((width <= 0) || (height <= 0) || (d_width <= 0) || (d_height <= 0))
    {
	MSG_ERR("VO: invalid dimensions!\n");
	return 0;
    }

    vf->dw=width;
    vf->dh=height;
    vf->dfourcc=outfmt;
    vf->priv->ofmt=outfmt;
    vf->priv->sw=width;
    vf->priv->sh=height;
    vf->priv->dw=d_width;
    vf->priv->dh=d_height;
    vf->priv->sflg=flags;
    // save vo's stride capability for the wanted colorspace:
    vf->default_caps=query_format(vf,outfmt,d_width,d_height);// & VFCAP_ACCEPT_STRIDE;

    if(vo_config(width,height,d_width,d_height,flags,"MPlayerXP",outfmt,tune))
	return 0;
    vf->priv->is_planar=vo_describe_fourcc(outfmt,&vf->priv->vd);
    vf->dw=d_width;
    vf->dh=d_height;
    vf->dfourcc=outfmt;
    ++vo_config_count;
    return 1;
}

static int __FASTCALL__ control(struct vf_instance_s* vf, int request, void* data)
{
    MSG_DBG2("vf_control: %u\n",request);
    switch(request){
    case VFCTRL_CHANGE_FRAME:
    {
	if(!vo_config_count) return CONTROL_FALSE; // vo not configured?
	vo_change_frame();
	return CONTROL_TRUE;
    }
    case VFCTRL_SET_EQUALIZER:
    {
	vf_equalizer_t *eq=data;
	if(!vo_config_count) return CONTROL_FALSE; // vo not configured?
	return((vo_control(VOCTRL_SET_EQUALIZER, eq) == VO_TRUE) ? CONTROL_TRUE : CONTROL_FALSE);
    }
    case VFCTRL_GET_EQUALIZER:
    {
	vf_equalizer_t *eq=data;
	if(!vo_config_count) return CONTROL_FALSE; // vo not configured?
	return((vo_control(VOCTRL_GET_EQUALIZER, eq) == VO_TRUE) ? CONTROL_TRUE : CONTROL_FALSE);
    }
    }
    // return video_out->control(request,data);
    return CONTROL_UNKNOWN;
}

static int __FASTCALL__ query_format(struct vf_instance_s* vf, unsigned int fmt,unsigned w,unsigned h){
    dri_surface_cap_t dcaps;
    int rflags,flags=vo_query_format(&fmt,w,h);
    MSG_DBG2("[vf_vo] %i=query_format(%s)\n",flags,vo_format_name(fmt));
    rflags=0;
    if(flags)
    {
	vo_control(DRI_GET_SURFACE_CAPS,&dcaps);
	if(dcaps.caps&DRI_CAP_UPSCALER) rflags |=VFCAP_HWSCALE_UP;
	if(dcaps.caps&DRI_CAP_DOWNSCALER) rflags |=VFCAP_HWSCALE_DOWN;
	if(rflags&(VFCAP_HWSCALE_UP|VFCAP_HWSCALE_DOWN)) rflags |= VFCAP_SWSCALE;
	if(dcaps.caps&DRI_CAP_HWOSD) rflags |=VFCAP_OSD;
	if(flags&0x1) rflags|= VFCAP_CSP_SUPPORTED;
	if(flags&0x2) rflags|= VFCAP_CSP_SUPPORTED_BY_HW;
    }
    return rflags;
}

static void __FASTCALL__ get_image(struct vf_instance_s* vf,
        mp_image_t *mpi){
    int retval;
    unsigned i;
    int finalize = vo_is_final();
    struct vf_priv_s *priv = vf->priv;
    retval=vo_get_surface(mpi);
    if(retval==VO_TRUE) {
	mpi->flags |= MP_IMGFLAG_FINAL|MP_IMGFLAG_DIRECT;
	if(finalize) mpi->flags |= MP_IMGFLAG_FINALIZED;
	MSG_DBG2("vf_vo_get_image was called successfully\n");
    }
    MSG_DBG2("vf_vo_get_image was called failed\n");
}

static int __FASTCALL__ put_slice(struct vf_instance_s* vf,
        mp_image_t *mpi){
  if(!vo_config_count) return 0; // vo not configured?
  if(!(mpi->flags & MP_IMGFLAG_FINAL) || (vf->sh->vfilter==vf && !(mpi->flags & MP_IMGFLAG_RENDERED)))
  {
    MSG_DBG2("vf_vo_put_slice was called(%u): %u %u %u %u\n",mpi->xp_idx,mpi->x,mpi->y,mpi->w,mpi->h);
    vo_draw_slice(mpi);
  }
  return 1;
}

static void __FASTCALL__ uninit( struct vf_instance_s* vf ) {
    free( vf->priv );
    vf->priv = NULL;
}

//===========================================================================//

static int __FASTCALL__ vf_open(vf_instance_t *vf,const char* args){
    vf->priv = malloc(sizeof(struct vf_priv_s));
    memset(vf->priv, 0, sizeof(struct vf_priv_s));
    vf->config=config;
    vf->control=control;
    vf->uninit=uninit;
    vf->print_conf=print_conf;
    vf->query_format=query_format;
    vf->get_image=get_image;
    vf->put_slice=put_slice;
    return 1;
}

const vf_info_t vf_info_vo = {
    "libvo wrapper",
    "vo",
    "A'rpi",
    "for internal use",
    VF_FLAGS_THREADS|VF_FLAGS_SLICES,
    vf_open
};

//===========================================================================//
