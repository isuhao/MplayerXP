/* DSP acceleration routines */
#include "pvector/pvector.h"

static void __FASTCALL__ PVECTOR_RENAME(int8_to_int16)(const int8_t* in_data, int16_t* out_data, unsigned len, int final)
{
#ifdef HAVE_INT_PVECTOR
    unsigned len_mm;
#endif
    unsigned i;
    i = 0;
#ifdef HAVE_INT_PVECTOR
    len_mm=len&(~(__IVEC_SIZE-1));
    if(!IVEC_ALIGNED(out_data))
    for(;i<len;i++) {
	((uint16_t*)out_data)[i]=((uint16_t)((const uint8_t*)in_data)[i])<<8;
	if(IVEC_ALIGNED(out_data)) break;
    }
    if((len_mm-i)>=__IVEC_SIZE)
    for(;i<len_mm;i+=__IVEC_SIZE){
	__ivec ind,itmp[2];
	if(IVEC_ALIGNED(in_data))
	    ind = _ivec_loada(&((const uint8_t *)in_data)[i]);
	else
	    ind = _ivec_loadu(&((const uint8_t *)in_data)[i]);
	itmp[0] = _ivec_scale_u16_from_u8(ind,&itmp[1]);
	if(final) {
	    _ivec_stream(&((uint16_t*)out_data)[i],itmp[0]);
	    _ivec_stream(&((uint16_t*)out_data)[i+__IVEC_SIZE/2],itmp[1]);
	} else {
	    _ivec_storea(&((uint16_t*)out_data)[i],itmp[0]);
	    _ivec_storea(&((uint16_t*)out_data)[i+__IVEC_SIZE/2],itmp[1]);
	}
    }
    _ivec_empty();
    if(final) _ivec_sfence();
#endif
	for(;i<len;i++)
	  ((uint16_t*)out_data)[i]=((uint16_t)((const uint8_t*)in_data)[i])<<8;
}

static void __FASTCALL__ PVECTOR_RENAME(int16_to_int8)(const int16_t* in_data, int8_t* out_data, unsigned len, int final)
{
#ifdef HAVE_INT_PVECTOR
    unsigned len_mm;
#endif
    unsigned i;
    i = 0;
#ifdef HAVE_INT_PVECTOR
    len_mm=len&(~(__IVEC_SIZE-1));
    if(!IVEC_ALIGNED(out_data))
    for(;i<len;i++) {
	((uint8_t*)out_data)[i]=(uint8_t)((((const uint16_t*)in_data)[i])>>8);
	if(IVEC_ALIGNED(out_data)) break;
    }
    if((len_mm-i)>=__IVEC_SIZE)
    for(;i<len_mm;i+=__IVEC_SIZE){
	__ivec outd,itmp[2];
	if(IVEC_ALIGNED(in_data)) {
	    itmp[0] = _ivec_loada(&((const uint16_t*)in_data)[i]);
	    itmp[1] = _ivec_loada(&((const uint16_t*)in_data)[i+__IVEC_SIZE/2]);
	}
	else {
	    itmp[0] = _ivec_loadu(&((const uint16_t*)in_data)[i]);
	    itmp[1] = _ivec_loadu(&((const uint16_t*)in_data)[i+__IVEC_SIZE/2]);
	}
	outd     = _ivec_scale_s8_from_s16(itmp[0],itmp[1]);
	if(final)
	    _ivec_stream(&((uint8_t*)out_data)[i],outd);
	else
	    _ivec_storea(&((uint8_t*)out_data)[i],outd);
    }
    _ivec_empty();
    if(final) _ivec_sfence();
#endif
    for(;i<len;i++)
	((uint8_t*)out_data)[i]=(uint8_t)((((const uint16_t*)in_data)[i])>>8);
}

static void __FASTCALL__ PVECTOR_RENAME(int16_to_int32)(const int16_t* in_data, int32_t* out_data, unsigned len, int final)
{
#ifdef HAVE_INT_PVECTOR
    unsigned len_mm;
#endif
    unsigned i;
    i=0;
#ifdef HAVE_INT_PVECTOR
    len_mm=len&(~(__IVEC_SIZE-1));
    if(!IVEC_ALIGNED(out_data))
    for(;i<len;i++){
	((uint32_t*)out_data)[i]=((uint32_t)((const uint16_t*)in_data)[i])<<16;
	if(IVEC_ALIGNED(out_data)) break;
    }
    if((len_mm-i)>=__IVEC_SIZE)
    for(;i<len_mm;i+=__IVEC_SIZE)
    {
	__ivec ind,tmp[2];
	if(IVEC_ALIGNED(in_data))
	    ind = _ivec_loada(&((const uint8_t *)in_data)[i]);
	else
	    ind = _ivec_loadu(&((const uint8_t *)in_data)[i]);
	tmp[0]= _ivec_scale_u32_from_u16(ind,&tmp[1]);
	if(final) {
	    _ivec_stream(&((uint8_t *)out_data)[i*2],tmp[0]);
	    _ivec_stream(&((uint8_t *)out_data)[i*2+__IVEC_SIZE],tmp[1]);
	} else {
	    _ivec_storea(&((uint8_t *)out_data)[i*2],tmp[0]);
	    _ivec_storea(&((uint8_t *)out_data)[i*2+__IVEC_SIZE],tmp[1]);
	}
    }
    if(final) _ivec_sfence();
    _ivec_empty();
#endif
    for(;i<len;i++)
	((uint32_t*)out_data)[i]=((uint32_t)((const uint16_t*)in_data)[i])<<16;
}

static void __FASTCALL__ PVECTOR_RENAME(int32_to_int16)(const int32_t* in_data, int16_t* out_data, unsigned len, int final)
{
#ifdef HAVE_INT_PVECTOR
    unsigned len_mm;
#endif
    unsigned i;
    i=0;
#ifdef HAVE_INT_PVECTOR
    len_mm=len&(~(__IVEC_SIZE-1));
    if(!IVEC_ALIGNED(out_data))
    for(;i<len;i++){
	((uint16_t*)out_data)[i]=(uint16_t)((((const uint32_t*)in_data)[i])>>16);
	if(IVEC_ALIGNED(out_data)) break;
    }
    if((len_mm-i)>=__IVEC_SIZE)
    for(;i<len_mm;i+=__IVEC_SIZE)
    {
	__ivec ind[2],tmp;
	if(IVEC_ALIGNED(in_data)) {
	    ind[0]=_ivec_loada(&((const uint8_t *)in_data)[i*2]);
	    ind[1]=_ivec_loada(&((const uint8_t *)in_data)[i*2+__IVEC_SIZE]);
	} else {
	    ind[0]=_ivec_loadu(&((const uint8_t *)in_data)[i*2]);
	    ind[1]=_ivec_loadu(&((const uint8_t *)in_data)[i*2+__IVEC_SIZE]);
	}
	tmp   = _ivec_scale_s16_from_s32(ind[0],ind[1]);
	if(final)
	    _ivec_stream(&((uint8_t *)out_data)[i],tmp);
	else
	    _ivec_storea(&((uint8_t *)out_data)[i],tmp);
    }
    if(final) _ivec_sfence();
    _ivec_empty();
#endif
    for(;i<len;i++)
	((uint16_t*)out_data)[i]=(uint16_t)((((const uint32_t*)in_data)[i])>>16);
}

static void __FASTCALL__ PVECTOR_RENAME(change_bps)(const void* in_data, void* out_data, unsigned len, unsigned inbps, unsigned outbps,int final)
{
  unsigned i;
  // Change the number of bits
    switch(inbps){
    case 1:
      switch(outbps){
      case 2:
	PVECTOR_RENAME(int8_to_int16)(in_data,out_data,len,final);
	break;
      case 3:
	for(i=0;i<len;i++)
	  ((uint8_t*)out_data)[3*i]=
	  ((uint8_t*)out_data)[3*i+1]=0;
	  ((uint8_t*)out_data)[3*i+2]=(((uint8_t*)in_data)[i]);
	break;
      case 4:
	i=0;
	for(;i<len;i++)
	  ((uint32_t*)out_data)[i]=((uint32_t)((uint8_t*)in_data)[i])<<24;
	break;
      }
      break;
    case 2:
      switch(outbps){
      case 1:
	PVECTOR_RENAME(int16_to_int8)(in_data,out_data,len,final);
	break;
      case 3:
	for(i=0;i<len;i++)
	  ((uint8_t*)out_data)[3*i]=0;
	  ((uint8_t*)out_data)[3*i+1]=(((uint8_t*)in_data)[2*i]);
	  ((uint8_t*)out_data)[3*i+2]=(((uint8_t*)in_data)[2*i+1]);
	break;
      case 4:
	PVECTOR_RENAME(int16_to_int32)(in_data,out_data,len,final);
	break;
      }
      break;
    case 3:
      switch(outbps){
      case 1:
	for(i=0;i<len;i++)
	  ((uint8_t*)out_data)[i]=(((uint8_t*)in_data)[3*i]);
	break;
      case 2:
	for(i=0;i<len;i++)
	{
	  ((uint8_t*)out_data)[2*i]=(uint8_t)(((uint8_t*)in_data)[3*i+1]);
	  ((uint8_t*)out_data)[2*i+1]=(uint8_t)(((uint8_t*)in_data)[3*i+2]);
	}
	break;
      case 4:
	for(i=0;i<len;i++)
	{
	  ((uint8_t*)out_data)[4*i+1]=((uint8_t*)in_data)[3*i];
	  ((uint8_t*)out_data)[4*i+2]=((uint8_t*)in_data)[3*i+1];
	  ((uint8_t*)out_data)[4*i+3]=((uint8_t*)in_data)[3*i+2];
	  ((uint8_t*)out_data)[4*i]=0;
	}
	break;
      }
      break;
    case 4:
      switch(outbps){
      case 1:
	i=0;
	for(;i<len;i++)
	  ((uint8_t*)out_data)[i]=(uint8_t)((((uint32_t*)in_data)[i])>>24);
	break;
      case 2:
        PVECTOR_RENAME(int32_to_int16)(in_data,out_data,len,final);
	break;
      case 3:
	for(i=0;i<len;i++)
	  ((uint8_t*)out_data)[3*i]=(((uint8_t*)in_data)[4*i+1]);
	  ((uint8_t*)out_data)[3*i+1]=(((uint8_t*)in_data)[4*i+2]);
	  ((uint8_t*)out_data)[3*i+2]=(((uint8_t*)in_data)[4*i+3]);
	break;
      }
      break;
    }
}

#ifdef HAVE_INT_PVECTOR
static __inline __m64 __attribute__((__gnu_inline__, __always_inline__))
PVECTOR_RENAME(_m_load)(const void *__P)
{
    return *(const __m64 *)__P;
}
#undef _m_load
#define _m_load PVECTOR_RENAME(_m_load)

static __inline __m64 __attribute__((__gnu_inline__, __always_inline__))
PVECTOR_RENAME(_m_load_half)(const void *__P)
{
    return _mm_cvtsi32_si64 (*(const int *)__P);
}
#undef _m_load_half
#define _m_load_half PVECTOR_RENAME(_m_load_half)

static __inline void __attribute__((__gnu_inline__, __always_inline__))
PVECTOR_RENAME(_m_store)(void *__P, __m64 src)
{
    *(__m64 *)__P = src;
}
#undef _m_store
#define _m_store PVECTOR_RENAME(_m_store)

static __inline void __attribute__((__gnu_inline__, __always_inline__))
PVECTOR_RENAME(_m_store_half)(void *__P, __m64 src)
{
    *(int *)__P = _mm_cvtsi64_si32(src);
}
#undef _m_store_half
#define _m_store_half PVECTOR_RENAME(_m_store_half)

static __inline void __attribute__((__gnu_inline__, __always_inline__))
PVECTOR_RENAME(_m_movntq)(void *__P, __m64 src)
{
#ifdef HAVE_MMX2
    _mm_stream_pi(__P,src);
#else
    _m_store(__P,src);
#endif
}
#undef _m_movntq
#define _m_movntq PVECTOR_RENAME(_m_movntq)
#endif
static int32_t __FASTCALL__ PVECTOR_RENAME(FIR_i16)(int16_t *x,int16_t *w)
{
#ifdef OPTIMIZE_SSE
    __m64 mm[8];
    mm[0] = _m_load(&w[0]);
    mm[1] = _m_load(&w[4]);
    mm[2] = _m_load(&w[8]);
    mm[3] = _m_load(&w[12]);

    mm[4] = _m_pmaddwd(mm[0],_m_load(&x[0]));
    mm[5] = _m_pmaddwd(mm[1],_m_load(&x[4]));
    mm[6] = _m_pmaddwd(mm[2],_m_load(&x[8]));
    mm[7] = _m_pmaddwd(mm[3],_m_load(&x[12]));

    mm[0] = _m_paddd(mm[4],mm[5]);
    mm[1] = _m_paddd(mm[6],mm[7]);
    mm[2] = _m_paddd(mm[0],mm[1]);
    mm[0] = _m_pshufw(mm[2],0xFE);

    mm[0] = _m_paddd(mm[0],mm[2]);
    mm[0] = _m_psrldi(mm[0],16);
    return _mm_cvtsi64_si32(mm[0]);
#else
  return ( w[0] *x[0] +w[1] *x[1] +w[2] *x[2] +w[3] *x[3]
         + w[4] *x[4] +w[5] *x[5] +w[6] *x[6] +w[7] *x[7]
         + w[8] *x[8] +w[9] *x[9] +w[10]*x[10]+w[11]*x[11]
         + w[12]*x[12]+w[13]*x[13]+w[14]*x[14]+w[15]*x[15] ) >> 16;
#endif
}

static void __FASTCALL__ PVECTOR_RENAME(float_to_int32)(const float* in, int32_t* out, unsigned len, int final)
{
    register unsigned i;
    float ftmp;
#ifdef HAVE_F32_PVECTOR
    unsigned len_mm;
  __f32vec int_max,plus1,minus1;
#endif
    i=0;
#ifdef HAVE_F32_PVECTOR
    int_max = _f32vec_broadcast(INT32_MAX-1);
    /* SSE float2int engine doesn't have SATURATION functionality.
       So CLAMP volume on 0.0002% here. */
    plus1 = _f32vec_broadcast(+0.999998);
    minus1= _f32vec_broadcast(-0.999998);
    if(!F32VEC_ALIGNED(out))
    for(;i<len;i++) {
      ftmp=((const float*)in)[i];
      SATURATE(ftmp,-0.999998,+0.999998);
      ((int32_t*)out)[i]=(int32_t)lrintf((INT_MAX-1)*ftmp);
      if(F32VEC_ALIGNED(out)) break;
    }
    _ivec_empty();
    len_mm=len&(~(__F32VEC_SIZE-1));
    if((len_mm-i)>=__F32VEC_SIZE/sizeof(float))
    for(;i<len_mm;i+=__F32VEC_SIZE/sizeof(float)) {
	__f32vec tmp;
	if(F32VEC_ALIGNED(in))
	    tmp = _f32vec_loada(&((const float*)in)[i]);
	else
	    tmp = _f32vec_loadu(&((const float*)in)[i]);
	tmp = _f32vec_clamp(tmp,minus1,plus1);
	tmp = _f32vec_mul(int_max,tmp);
	if(final)
	    _f32vec_to_s32_stream(&((int32_t*)out)[i],tmp);
	else
	    _f32vec_to_s32a(&((int32_t*)out)[i],tmp);
    }
    if(final) _ivec_sfence();
    _ivec_empty();
#endif
    for(;i<len;i++) {
      ftmp=((const float*)in)[i];
      SATURATE(ftmp,-0.999998,+0.999998);
      ((int32_t*)out)[i]=(int32_t)lrintf((INT32_MAX-1)*ftmp);
    }
}

static void __FASTCALL__ PVECTOR_RENAME(int32_to_float)(int32_t* in, float* out, unsigned len, int final)
{
#ifdef HAVE_F32_PVECTOR
    __f32vec rev_imax = _f32vec_broadcast(1.0/INT_MAX);
#endif
  register unsigned i=0;
#ifdef HAVE_F32_PVECTOR
    if(!F32VEC_ALIGNED(out))
    for(;i<len;i++) {
      ((float*)out)[i]=(1.0/INT_MAX)*((float)((const int32_t*)in)[i]);
      if(F32VEC_ALIGNED(out)) break;
    }
    _ivec_empty();
    if((len-i)>=__F32VEC_SIZE)
    for(;i<len;i+=__F32VEC_SIZE/sizeof(float)) {
	__f32vec tmp;
	if(F32VEC_ALIGNED(in))
	    tmp = _f32vec_from_s32a(&((const int32_t*)in)[i]);
	else
	    tmp = _f32vec_from_s32u(&((const int32_t*)in)[i]);
	tmp = _f32vec_mul(rev_imax,tmp);
	if(final)
	    _f32vec_stream(&((float*)out)[i],tmp);
	else
	    _f32vec_storea(&((float*)out)[i],tmp);
    }
    if(final) _ivec_sfence();
    _ivec_empty();
#endif
    for(;i<len;i++)
      ((float*)out)[i]=(1.0/INT_MAX)*((float)((const int32_t*)in)[i]);
}

static void __FASTCALL__ PVECTOR_RENAME(float2int)(void* in, void* out, int len, int bps,int final)
{
  float ftmp;
  register int i;
  switch(bps){
  case(1):
    for(i=0;i<len;i++) {
      ftmp=((float*)in)[i];
      SATURATE(ftmp,-1.0,+1.0);
      ((int8_t*)out)[i]=(int8_t)lrintf(SCHAR_MAX*ftmp);
    }
    break;
  case(2):
#ifdef HAVE_INT_PVECTOR
    {
	int32_t __attribute__((aligned(64))) itmp[len];
	/* unfortunatelly there is now universal way for convertion on perfectly different chips */
	PVECTOR_RENAME(float_to_int32)(in, itmp, len, 0);
	PVECTOR_RENAME(int32_to_int16)(itmp, out, len, final);
    }
#else
    for(i=0;i<len;i++) {
      ftmp=((float*)in)[i];
      SATURATE(ftmp,-1.0,+1.0);
      ((int16_t*)out)[i]=(int16_t)lrintf(SHRT_MAX*ftmp);
    }
#endif
    break;
  case(3):
    for(i=0;i<len;i++) {
      ftmp=((float*)in)[i];
      SATURATE(ftmp,-1.0,+1.0);
      store24bit(out, i, (int32_t)lrintf((INT_MAX-1)*ftmp));
    }
    break;
  case(4):
    PVECTOR_RENAME(float_to_int32)(in, out, len, final);
    break;
  }
}

static void __FASTCALL__ PVECTOR_RENAME(int2float)(void* in, void* out, int len, int bps, int final)
{
  register int i;
  switch(bps){
  case(1):
    for(i=0;i<len;i++)
      ((float*)out)[i]=(1.0/SCHAR_MAX)*((float)((int8_t*)in)[i]);
    break;
  case(2):
#ifdef HAVE_INT_PVECTOR
    {
	int32_t __attribute__((aligned(64))) itmp[len];
	PVECTOR_RENAME(int16_to_int32)(in, itmp, len, 0);
	PVECTOR_RENAME(int32_to_float)(itmp, out, len, final);
    }
#else
    for(i=0;i<len;i++)
      ((float*)out)[i]=(1.0/SHRT_MAX)*((float)((int16_t*)in)[i]);
#endif
    break;
  case(3):
    for(i=0;i<len;i++)
      ((float*)out)[i]=(1.0/INT_MAX)*((float)((int32_t)load24bit(in, i)));
    break;
  case(4):
    PVECTOR_RENAME(int32_to_float)(in,out,len,final);
    break;
  }
}

static float __FASTCALL__ PVECTOR_RENAME(FIR_f32)(float *x,float *w)
{
#ifdef HAVE_3DNOW
    float rval;
    __asm __volatile(
	"movq		(%1), %%mm0\n\t"
	"movq		8(%1), %%mm1\n\t"
	"movq		16(%1), %%mm2\n\t"
	"movq		24(%1), %%mm3\n\t"
	"movq		32(%1), %%mm4\n\t"
	"movq		40(%1), %%mm5\n\t"
	"movq		48(%1), %%mm6\n\t"
	"movq		56(%1), %%mm7\n\t"
	"pfmul		(%2), %%mm0\n\t"
	"pfmul		8(%2), %%mm1\n\t"
	"pfmul		16(%2), %%mm2\n\t"
	"pfmul		24(%2), %%mm3\n\t"
	"pfmul		32(%2), %%mm4\n\t"
	"pfmul		40(%2), %%mm5\n\t"
	"pfmul		48(%2), %%mm6\n\t"
	"pfmul		56(%2), %%mm7\n\t"
	"pfadd		%%mm1, %%mm0\n\t"
	"pfadd		%%mm3, %%mm2\n\t"
	"pfadd		%%mm5, %%mm4\n\t"
	"pfadd		%%mm7, %%mm6\n\t"
	"pfadd		%%mm2, %%mm0\n\t"
	"pfadd		%%mm6, %%mm4\n\t"
	"pfadd		%%mm4, %%mm0\n\t"
	"pfacc		%%mm0, %%mm0\n\t"
	"movd		%%mm0, %0\n\t"
	"femms"
	:"=&r"(rval):"r"(w),"r"(x)
	:"memory"
#ifdef FPU_CLOBBERED
	,FPU_CLOBBERED
#endif
#ifdef MMX_CLOBBERED
	,MMX_CLOBBERED
#endif
	);
	return rval;
#else
  return ( w[0] *x[0] +w[1] *x[1] +w[2] *x[2] +w[3] *x[3]
         + w[4] *x[4] +w[5] *x[5] +w[6] *x[6] +w[7] *x[7]
         + w[8] *x[8] +w[9] *x[9] +w[10]*x[10]+w[11]*x[11]
         + w[12]*x[12]+w[13]*x[13]+w[14]*x[14]+w[15]*x[15] );
#endif
}
