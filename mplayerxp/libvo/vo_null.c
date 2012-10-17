/* 
 *  video_out_null.c
 *
 *	Copyright (C) Aaron Holtzman - June 2000
 *
 *  This file is part of mpeg2dec, a free MPEG-2 video stream decoder.
 *	
 *  mpeg2dec is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  mpeg2dec is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *   
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 */
#include "mp_config.h"

#include <errno.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#ifdef HAVE_MEMALIGN
#include <malloc.h>
#endif
#include "video_out.h"
#include "video_out_internal.h"
#include "dri_vo.h"
#ifdef CONFIG_VIDIX
#include <vidix/vidixlib.h>
#endif
#include "vo_msg.h"

LIBVO_EXTERN(null)

static vo_info_t vo_info =
{
	"Null video output",
	"null",
	"Aaron Holtzman <aholtzma@ess.engr.uvic.ca>",
	""
};

typedef struct null_priv_s {
    uint32_t	image_width, image_height,frame_size,fourcc;
    uint8_t *	bm_buffs[MAX_DRI_BUFFERS];
    uint32_t	num_frames;
    uint32_t	pitch_y,pitch_u,pitch_v;
    uint32_t	offset_y,offset_u,offset_v;
}null_priv_t;
static null_priv_t null;

static void __FASTCALL__ change_frame(unsigned idx)
{
    UNUSED(idx);
}

static uint32_t __FASTCALL__ config(uint32_t width, uint32_t height, uint32_t d_width, uint32_t d_height, uint32_t fullscreen, char *title, uint32_t format,const vo_tune_info_t *info)
{
    unsigned awidth;
    size_t i;
    null.image_width = width;
    null.image_height = height;
    null.num_frames = vo.da_buffs;
    null.fourcc=format;
    UNUSED(d_width);
    UNUSED(d_height);
    UNUSED(fullscreen);
    UNUSED(title);
    null.pitch_y=null.pitch_u=null.pitch_v=1;
    if(info)
    {
	switch(((const vo_tune_info_t *)info)->pitch[0])
	{
	    case 2:
	    case 4:
	    case 8:
	    case 16:
	    case 32:
	    case 64:
	    case 128:
	    case 256: null.pitch_y = ((const vo_tune_info_t *)info)->pitch[0];
		      break;
	    default: break;
	}
	switch(((const vo_tune_info_t *)info)->pitch[1])
	{
	    case 2:
	    case 4:
	    case 8:
	    case 16:
	    case 32:
	    case 64:
	    case 128:
	    case 256: null.pitch_u = ((const vo_tune_info_t *)info)->pitch[1];
		      break;
	    default: break;
	}
	switch(((const vo_tune_info_t *)info)->pitch[2])
	{
	    case 2:
	    case 4:
	    case 8:
	    case 16:
	    case 32:
	    case 64:
	    case 128:
	    case 256: null.pitch_v = ((const vo_tune_info_t *)info)->pitch[2];
		      break;
	    default: break;
	}
    }
    null.offset_y=null.offset_u=null.offset_v=0;
    switch(format)
    {
    case IMGFMT_Y800:
		awidth = (width + (null.pitch_y-1)) & ~(null.pitch_y-1);
		null.frame_size = awidth*height;
		break;
    case IMGFMT_YVU9:
    case IMGFMT_IF09:
		awidth = (width + (null.pitch_y-1)) & ~(null.pitch_y-1);
		null.frame_size = awidth*(height+height/8);
		null.offset_u=awidth*height;
		null.offset_v=awidth*height/16;
		break;
    case IMGFMT_I420:
    case IMGFMT_YV12:
    case IMGFMT_IYUV:
		awidth = (width + (null.pitch_y-1)) & ~(null.pitch_y-1);
		null.frame_size = awidth*(height+height/2);
		null.offset_u=awidth*height;
		null.offset_v=awidth*height/4;
		break;
    case IMGFMT_RGB32:
    case IMGFMT_BGR32:
		awidth = (width*4 + (null.pitch_y-1)) & ~(null.pitch_y-1);
		null.frame_size = awidth*height;
		break;
    /* YUY2 YVYU, RGB15, RGB16 */
    default:	
		awidth = (width*2 + (null.pitch_y-1)) & ~(null.pitch_y-1);
		null.frame_size = awidth*height;
		break;
    }
    for(i=0;i<null.num_frames;i++)
    {
	if(!null.bm_buffs[i])
#ifdef HAVE_MEMALIGN
	    null.bm_buffs[i] = memalign(getpagesize(),null.frame_size);
#else
	    null.bm_buffs[i] = malloc(null.frame_size);
#endif
	if(!(null.bm_buffs[i]))
	{
		MSG_ERR("Can't allocate memory for busmastering\n");
		return -1;
	}
    }
    return 0;
}

static const vo_info_t* get_info(void)
{
	return &vo_info;
}

static void uninit(void)
{
    size_t i;
    for(i=0;i<null.num_frames;i++)
    {
	free(null.bm_buffs[i]);
	null.bm_buffs[i]=NULL;
    }
}

static uint32_t __FASTCALL__ preinit(const char *arg)
{
    if(arg)
    {
	MSG_ERR("vo_null: Unknown subdevice: %s\n",arg);
	return ENOSYS;
    }
    return 0;
}

static void __FASTCALL__ null_dri_get_surface_caps(dri_surface_cap_t *caps)
{
    caps->caps =DRI_CAP_TEMP_VIDEO |
		DRI_CAP_HORZSCALER | DRI_CAP_VERTSCALER |
		DRI_CAP_DOWNSCALER | DRI_CAP_UPSCALER;
    caps->fourcc = null.fourcc;
    caps->width=null.image_width;
    caps->height=null.image_height;
    /* in case of vidix movie fit surface */
    caps->x = caps->y=0;
    caps->w=caps->width;
    caps->h=caps->height;
    caps->strides[0] = null.pitch_y;
    caps->strides[1] = null.pitch_v;
    caps->strides[2] = null.pitch_u;
    caps->strides[3] = 0;
}

static void __FASTCALL__ null_dri_get_surface(dri_surface_t *surf)
{
	surf->planes[0] = null.bm_buffs[surf->idx] + null.offset_y;
	surf->planes[1] = null.bm_buffs[surf->idx] + null.offset_v;
	surf->planes[2] = null.bm_buffs[surf->idx] + null.offset_u;
	surf->planes[3] = 0;
}

static int __FASTCALL__ null_query_format(vo_query_fourcc_t* format) {
    /* we must avoid compressed-fourcc here */
    switch(format->fourcc) {
    case IMGFMT_444P16_LE:
    case IMGFMT_444P16_BE:
    case IMGFMT_422P16_LE:
    case IMGFMT_422P16_BE:
    case IMGFMT_420P16_LE:
    case IMGFMT_420P16_BE:
    case IMGFMT_420A:
    case IMGFMT_444P:
    case IMGFMT_422P:
    case IMGFMT_YV12:
    case IMGFMT_I420:
    case IMGFMT_IYUV:
    case IMGFMT_YVU9:
    case IMGFMT_IF09:
    case IMGFMT_411P:
    case IMGFMT_YUY2:
    case IMGFMT_UYVY:
// RGB and grayscale (Y8 and Y800):
    case IMGFMT_RGB48LE:
    case IMGFMT_RGB48BE:
    case IMGFMT_BGR32:
    case IMGFMT_RGB32:
    case IMGFMT_BGR24:
    case IMGFMT_RGB24:
    case IMGFMT_BGR16:
    case IMGFMT_RGB16:
    case IMGFMT_BGR15:
    case IMGFMT_RGB15:
    case IMGFMT_Y800:
    case IMGFMT_Y8:
    case IMGFMT_BGR8:
    case IMGFMT_RGB8:
    case IMGFMT_BGR4:
    case IMGFMT_RGB4:
    case IMGFMT_BG4B:
    case IMGFMT_RG4B:
    case IMGFMT_BGR1:
    case IMGFMT_RGB1: return VO_TRUE;
    }
    return VO_FALSE;
}

static uint32_t __FASTCALL__ control(uint32_t request, any_t*data)
{
  switch (request) {
    case VOCTRL_QUERY_FORMAT:
	return null_query_format(data);
    case VOCTRL_GET_NUM_FRAMES:
	*(uint32_t *)data = null.num_frames;
	return VO_TRUE;
    case DRI_GET_SURFACE_CAPS:
	null_dri_get_surface_caps(data);
	return VO_TRUE;
    case DRI_GET_SURFACE:
	null_dri_get_surface(data);
	return VO_TRUE;
    case VOCTRL_FLUSH_PAGES:
	return VO_TRUE;
  }
  return VO_NOTIMPL;
}
