#include "mp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;
/*
    Copyright (C) 2003 Daniel Moreno <comac@comac.darktech.org>

    This program is mp_free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>

#include "libvo2/img_format.h"
#include "xmpcore/mp_image.h"
#include "vf.h"
#include "vf_internal.h"
#include "osdep/fastmemcpy.h"
#include "pp_msg.h"

#define PARAM1_DEFAULT 4.0
#define PARAM2_DEFAULT 3.0
#define PARAM3_DEFAULT 6.0
#define PARAM4_DEFAULT 8.0

//===========================================================================//

struct vf_priv_t {
	int Coefs[4][512*16];
	unsigned char *Line;
	unsigned short *Frame[3];
	mp_image_t *pmpi;
};


/***************************************************************************/
static void __FASTCALL__ uninit(vf_instance_t* vf)
{
	if(vf->priv->Line){delete vf->priv->Line;vf->priv->Line=NULL;}
	if(vf->priv->Frame[0]){delete vf->priv->Frame[0];vf->priv->Frame[0]=NULL;}
	if(vf->priv->Frame[1]){delete vf->priv->Frame[1];vf->priv->Frame[1]=NULL;}
	if(vf->priv->Frame[2]){delete vf->priv->Frame[2];vf->priv->Frame[2]=NULL;}
}

static int __FASTCALL__ vf_config(vf_instance_t* vf,
	int width, int height, int d_width, int d_height,
	vo_flags_e flags, unsigned int outfmt){

	uninit(vf);
	vf->priv->Line = reinterpret_cast<unsigned char*>(new int [width]);
	vf->priv->pmpi=NULL;
//        vf->default_caps &= !VFCAP_ACCEPT_STRIDE;

	return vf_next_config(vf,width,height,d_width,d_height,flags,outfmt);
}

#define LowPass(Prev, Curr, Coef) (Curr + Coef[Prev - Curr])

static inline unsigned int LowPassMul(unsigned int PrevMul, unsigned int CurrMul, int* Coef){
//    int dMul= (PrevMul&0xFFFFFF)-(CurrMul&0xFFFFFF);
    int dMul= PrevMul-CurrMul;
    int d=((dMul+0x10007FF)/(65536/16));
    return CurrMul + Coef[d];
}

static void __FASTCALL__ deNoise(unsigned char *Frame,        // mpi->planes[x]
		    unsigned char *FramePrev,    // pmpi->planes[x]
		    unsigned char *FrameDest,    // dmpi->planes[x]
		    unsigned char *LineAnt,      // vf->priv->Line (width bytes)
		    int W, int H, int sStride, int pStride, int dStride,
		    int *Horizontal, int *Vertical, int *Temporal)
{
    int X, Y;
    int sLineOffs = 0, pLineOffs = 0, dLineOffs = 0;
    unsigned char PixelAnt;

    /* First pixel has no left nor top neightbour. Only previous frame */
    LineAnt[0] = PixelAnt = Frame[0];
    FrameDest[0] = LowPass(FramePrev[0], LineAnt[0], Temporal);

    /* Fist line has no top neightbour. Only left one for each pixel and
     * last frame */
    for (X = 1; X < W; X++)
    {
	PixelAnt = LowPass(PixelAnt, Frame[X], Horizontal);
	LineAnt[X] = PixelAnt;
	FrameDest[X] = LowPass(FramePrev[X], LineAnt[X], Temporal);
    }

    for (Y = 1; Y < H; Y++)
    {
	sLineOffs += sStride, pLineOffs += pStride, dLineOffs += dStride;
	/* First pixel on each line doesn't have previous pixel */
	PixelAnt = Frame[sLineOffs];
	LineAnt[0] = LowPass(LineAnt[0], PixelAnt, Vertical);
	FrameDest[dLineOffs] = LowPass(FramePrev[pLineOffs], LineAnt[0], Temporal);

	for (X = 1; X < W; X++)
	{
	    /* The rest are normal */
	    PixelAnt = LowPass(PixelAnt, Frame[sLineOffs+X], Horizontal);
	    LineAnt[X] = LowPass(LineAnt[X], PixelAnt, Vertical);
	    FrameDest[dLineOffs+X] = LowPass(FramePrev[pLineOffs+X], LineAnt[X], Temporal);
	}
    }
}

static void __FASTCALL__ hqDeNoise(unsigned char *Frame,        // mpi->planes[x]
		    unsigned char *FrameDest,    // dmpi->planes[x]
		    unsigned int *LineAnt,      // vf->priv->Line (width bytes)
		    unsigned short **FrameAntPtr,
		    int W, int H, int sStride, int dStride,
		    int *Horizontal, int *Vertical, int *Temporal)
{
    int X, Y;
    int sLineOffs = 0, dLineOffs = 0;
    unsigned int PixelAnt;
    int PixelDst;
    unsigned short* FrameAnt=(*FrameAntPtr);

    if(!FrameAnt){
	(*FrameAntPtr)=FrameAnt=new unsigned short [W*H];
	for (Y = 0; Y < H; Y++){
	    unsigned short* dst=&FrameAnt[Y*W];
	    unsigned char* src=Frame+Y*sStride;
	    for (X = 0; X < W; X++) dst[X]=src[X]<<8;
	}
    }

    /* First pixel has no left nor top neightbour. Only previous frame */
    LineAnt[0] = PixelAnt = Frame[0]<<16;
    PixelDst = LowPassMul(FrameAnt[0]<<8, PixelAnt, Temporal);
    FrameAnt[0] = ((PixelDst+0x1000007F)/256);
    FrameDest[0]= ((PixelDst+0x10007FFF)/65536);

    /* Fist line has no top neightbour. Only left one for each pixel and
     * last frame */
    for (X = 1; X < W; X++){
	LineAnt[X] = PixelAnt = LowPassMul(PixelAnt, Frame[X]<<16, Horizontal);
	PixelDst = LowPassMul(FrameAnt[X]<<8, PixelAnt, Temporal);
	FrameAnt[X] = ((PixelDst+0x1000007F)/256);
	FrameDest[X]= ((PixelDst+0x10007FFF)/65536);
    }

    for (Y = 1; Y < H; Y++){
	unsigned short* LinePrev=&FrameAnt[Y*W];
	sLineOffs += sStride, dLineOffs += dStride;
	/* First pixel on each line doesn't have previous pixel */
	PixelAnt = Frame[sLineOffs]<<16;
	LineAnt[0] = LowPassMul(LineAnt[0], PixelAnt, Vertical);
	PixelDst = LowPassMul(LinePrev[0]<<8, LineAnt[0], Temporal);
	LinePrev[0] = ((PixelDst+0x1000007F)/256);
	FrameDest[dLineOffs]= ((PixelDst+0x10007FFF)/65536);

	for (X = 1; X < W; X++){
	    /* The rest are normal */
	    PixelAnt = LowPassMul(PixelAnt, Frame[sLineOffs+X]<<16, Horizontal);
	    LineAnt[X] = LowPassMul(LineAnt[X], PixelAnt, Vertical);
	    PixelDst = LowPassMul(LinePrev[X]<<8, LineAnt[X], Temporal);
	    LinePrev[X] = ((PixelDst+0x1000007F)/256);
	    FrameDest[dLineOffs+X]= ((PixelDst+0x10007FFF)/65536);
	}
    }
}

static int __FASTCALL__ hq_put_slice(vf_instance_t* vf, mp_image_t *mpi){
	int cw= mpi->w >> mpi->chroma_x_shift;
	int ch= mpi->h >> mpi->chroma_y_shift;
	int W = mpi->w, H = mpi->h;

	mp_image_t *dmpi=vf_get_new_temp_genome(vf->next,mpi);

	if(!dmpi) return 0;
#ifdef _OPENMP
#pragma omp parallel sections
{
#pragma omp section
#endif
	hqDeNoise(mpi->planes[0], dmpi->planes[0],
		reinterpret_cast<unsigned*>(vf->priv->Line),
		&vf->priv->Frame[0], W, H,
		mpi->stride[0], dmpi->stride[0],
		vf->priv->Coefs[0],
		vf->priv->Coefs[0],
		vf->priv->Coefs[1]);
#ifdef _OPENMP
#pragma omp section
#endif
	hqDeNoise(mpi->planes[1], dmpi->planes[1],
		reinterpret_cast<unsigned*>(vf->priv->Line),
		&vf->priv->Frame[1], cw, ch,
		mpi->stride[1], dmpi->stride[1],
		vf->priv->Coefs[2],
		vf->priv->Coefs[2],
		vf->priv->Coefs[3]);
#ifdef _OPENMP
#pragma omp section
#endif
	hqDeNoise(mpi->planes[2], dmpi->planes[2],
		reinterpret_cast<unsigned*>(vf->priv->Line),
		&vf->priv->Frame[2], cw, ch,
		mpi->stride[2], dmpi->stride[2],
		vf->priv->Coefs[2],
		vf->priv->Coefs[2],
		vf->priv->Coefs[3]);
#ifdef _OPENMP
}
#endif
	return vf_next_put_slice(vf,dmpi);
}

static int __FASTCALL__ put_slice(vf_instance_t* vf, mp_image_t *mpi){
	int cw= mpi->w >> mpi->chroma_x_shift;
	int ch= mpi->h >> mpi->chroma_y_shift;
	int W = mpi->w, H = mpi->h;

	mp_image_t *dmpi=vf_get_new_temp_genome(vf->next,mpi);

	if(!dmpi) return 0;
	if (!vf->priv->pmpi) vf->priv->pmpi=mpi;

#ifdef _OPENMP
#pragma omp parallel sections
{
#pragma omp section
#endif
	deNoise(mpi->planes[0], vf->priv->pmpi->planes[0], dmpi->planes[0],
		vf->priv->Line, W, H,
		mpi->stride[0], vf->priv->pmpi->stride[0], dmpi->stride[0],
		vf->priv->Coefs[0] + 256,
		vf->priv->Coefs[0] + 256,
		vf->priv->Coefs[1] + 256);
#ifdef _OPENMP
#pragma omp section
#endif
	deNoise(mpi->planes[1], vf->priv->pmpi->planes[1], dmpi->planes[1],
		vf->priv->Line, cw, ch,
		mpi->stride[1], vf->priv->pmpi->stride[1], dmpi->stride[1],
		vf->priv->Coefs[2] + 256,
		vf->priv->Coefs[2] + 256,
		vf->priv->Coefs[3] + 256);
#ifdef _OPENMP
#pragma omp section
#endif
	deNoise(mpi->planes[2], vf->priv->pmpi->planes[2], dmpi->planes[2],
		vf->priv->Line, cw, ch,
		mpi->stride[2], vf->priv->pmpi->stride[2], dmpi->stride[2],
		vf->priv->Coefs[2] + 256,
		vf->priv->Coefs[2] + 256,
		vf->priv->Coefs[3] + 256);
#ifdef _OPENMP
}
#endif
	vf->priv->pmpi=dmpi; // save reference image
	return vf_next_put_slice(vf,dmpi);
}

//===========================================================================//

static int __FASTCALL__ query_format(vf_instance_t* vf, unsigned int fmt,unsigned w,unsigned h){
	switch(fmt)
	{
	case IMGFMT_YV12:
	case IMGFMT_I420:
	case IMGFMT_IYUV:
	case IMGFMT_YVU9:
	case IMGFMT_444P:
	case IMGFMT_422P:
	case IMGFMT_411P:
		return vf_next_query_format(vf, fmt, w, h);
	}
	return 0;
}


#define ABS(A) ( (A) > 0 ? (A) : -(A) )

static void __FASTCALL__ PrecalcCoefs(int *Ct, double Dist25)
{
    int i;
    double Gamma, Simil, C;

    Gamma = log(0.25) / log(1.0 - Dist25/255.0);

    for (i = -256; i <= 255; i++)
    {
	Simil = 1.0 - ABS(i) / 255.0;
	C = pow(Simil, Gamma) * (double)i;
	Ct[256+i] = (C<0) ? (C-0.5) : (C+0.5);
    }
}

static void __FASTCALL__ hqPrecalcCoefs(int *Ct, double Dist25)
{
    int i;
    double Gamma, Simil, C;

    Gamma = log(0.25) / log(1.0 - Dist25/255.0 - 0.00001);

    for (i = -256*16; i < 256*16; i++)
    {
	Simil = 1.0 - ABS(i) / (16*255.0);
	C = pow(Simil, Gamma) * 65536.0 * (double)i / 16.0;
	Ct[16*256+i] = (C<0) ? (C-0.5) : (C+0.5);
    }
}

static MPXP_Rc __FASTCALL__ vf_open(vf_instance_t *vf,const char* args){
    double LumSpac, LumTmp, ChromSpac, ChromTmp;
    double Param1, Param2, Param3, Param4;
    int e;

    vf->config_vf=vf_config;
    vf->put_slice=put_slice;
    vf->query_format=query_format;
    vf->uninit=uninit;
    vf->priv=new(zeromem) vf_priv_t;

    e=0;
    if (args) {
	sscanf(args, "%lf:%lf:%lf:%lf", &Param1, &Param2, &Param3, &Param4 );
	switch(e) {
	    default:
	    case 0:
		LumSpac = PARAM1_DEFAULT;
		LumTmp = PARAM3_DEFAULT;

		ChromSpac = PARAM2_DEFAULT;
		ChromTmp = LumTmp * ChromSpac / LumSpac;
		break;

	    case 1:
		LumSpac = Param1;
		LumTmp = PARAM3_DEFAULT * Param1 / PARAM1_DEFAULT;

		ChromSpac = PARAM2_DEFAULT * Param1 / PARAM1_DEFAULT;
		ChromTmp = LumTmp * ChromSpac / LumSpac;
		break;

	    case 2:
		LumSpac = Param1;
		LumTmp = PARAM3_DEFAULT * Param1 / PARAM1_DEFAULT;

		ChromSpac = Param2;
		ChromTmp = LumTmp * ChromSpac / LumSpac;
		break;

	    case 3:
		LumSpac = Param1;
		LumTmp = Param3;

		ChromSpac = Param2;
		ChromTmp = LumTmp * ChromSpac / LumSpac;
		break;

	    case 4:
		LumSpac = Param1;
		LumTmp = Param3;

		ChromSpac = Param2;
		ChromTmp = Param4;
		vf->put_slice=hq_put_slice;
		break;

	}
    } else {
	LumSpac = PARAM1_DEFAULT;
	LumTmp = PARAM3_DEFAULT;

	ChromSpac = PARAM2_DEFAULT;
	ChromTmp = LumTmp * ChromSpac / LumSpac;
    }

    if(e==4) {
	hqPrecalcCoefs(vf->priv->Coefs[0], LumSpac);
	hqPrecalcCoefs(vf->priv->Coefs[1], LumTmp);
	hqPrecalcCoefs(vf->priv->Coefs[2], ChromSpac);
	hqPrecalcCoefs(vf->priv->Coefs[3], ChromTmp);
    } else {
	PrecalcCoefs(vf->priv->Coefs[0], LumSpac);
	PrecalcCoefs(vf->priv->Coefs[1], LumTmp);
	PrecalcCoefs(vf->priv->Coefs[2], ChromSpac);
	PrecalcCoefs(vf->priv->Coefs[3], ChromTmp);
    }
    check_pin("vfilter",vf->pin,VF_PIN);
    return MPXP_Ok;
}

extern const vf_info_t vf_info_denoise3d = {
    "3D Denoiser (variable lowpass filter)",
    "denoise3d",
    "Daniel Moreno",
    "",
    VF_FLAGS_THREADS,
    vf_open
};

//===========================================================================//
