/*=============================================================================
//
//  This software has been released under the terms of the GNU General Public
//  license. See http://www.gnu.org/copyleft/gpl.html for details.
//
//  Copyright 2002 Anders Johansson ajh@atri.curtin.edu.au
//
//=============================================================================
*/

#ifndef	_AFLIB_H
#define	_AFLIB_H	1

#include "mp_config.h"
#include "xmpcore/mp_aframe.h"
/* Implementation of routines used for DSP */

/* Size of floating point type used in routines */
typedef float _ftype_t;

extern void __FASTCALL__ boxcar(int n, _ftype_t* w);
extern void __FASTCALL__ triang(int n, _ftype_t* w);
extern void __FASTCALL__ hanning(int n, _ftype_t* w);
extern void __FASTCALL__ hamming(int n,_ftype_t* w);
extern void __FASTCALL__ blackman(int n,_ftype_t* w);
extern void __FASTCALL__ flattop(int n,_ftype_t* w);
extern void __FASTCALL__ kaiser(int n, _ftype_t* w,_ftype_t b);

// Design and implementation of different types of digital filters


// Flags used for filter design

// Filter characteristics
enum {
    LP		=0x00010000, // Low pass
    HP		=0x00020000, // High pass
    BP		=0x00040000, // Band pass
    BS		=0x00080000, // Band stop
    TYPE_MASK	=0x000F0000
};
// Window types
enum {
    BOXCAR	=0x00000001,
    TRIANG	=0x00000002,
    HAMMING	=0x00000004,
    HANNING	=0x00000008,
    BLACKMAN	=0x00000010,
    FLATTOP	=0x00000011,
    KAISER	=0x00000012,
    WINDOW_MASK	=0x0000001F
};
// Parallel filter design
enum {
    FWD		=0x00000001, // Forward indexing of polyphase filter
    REW		=0x00000002, // Reverse indexing of polyphase filter
    ODD		=0x00000010 // Make filter HP
};
// Exported functions
extern _ftype_t __FASTCALL__ fir(unsigned int n, _ftype_t* w, _ftype_t* x);

extern _ftype_t* __FASTCALL__ pfir(unsigned int n, unsigned int k, unsigned int xi, _ftype_t** w, _ftype_t** x, _ftype_t* y, unsigned int s);

extern int __FASTCALL__ updateq(unsigned int n, unsigned int xi, _ftype_t* xq, _ftype_t* in);
extern int __FASTCALL__ updatepq(unsigned int n, unsigned int k, unsigned int xi, _ftype_t** xq, _ftype_t* in, unsigned int s);

extern int __FASTCALL__ design_fir(unsigned int n, _ftype_t* w, _ftype_t* fc, unsigned int flags, _ftype_t opt);

extern int __FASTCALL__ design_pfir(unsigned int n, unsigned int k, _ftype_t* w, _ftype_t** pw, _ftype_t g, unsigned int flags);

extern int __FASTCALL__ szxform(_ftype_t* a, _ftype_t* b, _ftype_t Q, _ftype_t fc, _ftype_t fs, _ftype_t *k, _ftype_t *coef);

/* Add new data to circular queue designed to be used with a FIR
   filter. xq is the circular queue, in pointing at the new sample, xi
   current index for xq and n the length of the filter. xq must be n*2
   long.
*/
#define updateq(n,xi,xq,in)\
  xq[xi]=(xq)[(xi)+(n)]=*(in);\
  xi=(++(xi))&((n)-1);

#include <math.h>
/* Private data for Highpass effect
	      --------------
	      !
	      !
---------------
*/
struct highp_t {
	double	A, B;
	float	previ, prevo;
};

/* Private data for Lowpass effect
--------------
	     !
	     !
	     --------------
*/
struct lowp_t {
	double	A, B, C;
	float	prev, pprev;
};

/* Private data for Bandpass effect
     ---------
     !       !
     !       !
------       -------
*/
struct bandp_t {
	double	A, B, C;
	double	prev, pprev;
	int	noise;
	/* 50 bytes of data, 52 bytes long for allocation purposes. */
};

extern void __FASTCALL__ lowp_init(lowp_t *lp, unsigned center, unsigned rate);
extern void __FASTCALL__ highp_init(highp_t *hp, unsigned center, unsigned rate);
extern void __FASTCALL__ bandp_init(bandp_t *bp, unsigned center, unsigned width, unsigned rate, int noise);

static inline float lowpass(lowp_t *_this,float sample)
{
    float fret;
    fret = (_this->A * sample - _this->B * _this->prev) - _this->C*_this->pprev;
    _this->pprev = _this->prev;
    _this->prev = fret;
    return fret;
}

static inline float highpass(highp_t *_this,float sample)
{
    float fret;
    fret = _this->B * ((_this->prevo - _this->previ) + sample)*0.8;
    _this->previ = sample;
    _this->prevo = fret;
    return fret;
}

static inline float bandpass(bandp_t *_this,float sample)
{
    float fret;
    fret = (_this->A * sample - _this->B * _this->prev) - _this->C * _this->pprev;
    _this->pprev = _this->prev;
    _this->prev = fret;
    return fret;
}

/*4th order IIR Butterworth filter */
static inline float IIR(float in,const float *w,float *q) {
    float h0 = q[0];
    float h1 = q[1];
    float hn = in-h0*w[0]-h1*w[1];
    q[1]=h0;
    q[0]=hn;
    return hn+h0*w[2]+h1*w[3];
}

/* some mmx_optimized stuff */
extern void (* __FASTCALL__ change_bps)(const mp_aframe_t* in, mp_aframe_t* out);
extern void (* __FASTCALL__ float2int)(const mp_aframe_t* in, mp_aframe_t* out);
extern void (* __FASTCALL__ int2float)(const mp_aframe_t* in, mp_aframe_t* out);
extern int32_t (* __FASTCALL__ FIR_i16)(const int16_t *x,const int16_t *w);
extern float (* __FASTCALL__ FIR_f32)(const float *x,const float *w);

template <class T> const T& saturate ( T& x, const T& _min, const T& _max ) {
    if(x<_min) x=_min;
    else if(x>_max) x=_max;
    return x;
}
#endif
