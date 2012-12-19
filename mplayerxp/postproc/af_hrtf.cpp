#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;
/* Experimental audio filter that mixes 5.1 and 5.1 with matrix
   encoded rear channels into headphone signal using FIR filtering
   with HRTF.
*/
//#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>

#include <math.h>

#include "af.h"
#include "af_internal.h"
#include "aflib.h"
#include "pp_msg.h"

/* HRTF filter coefficients and adjustable parameters */
#include "af_hrtf.h"

typedef float real_t;

struct af_hrtf_t {
    /* Lengths */
    int dlbuflen, hrflen, basslen;
    /* L, C, R, Ls, Rs channels */
    float *lf, *rf, *lr, *rr, *cf, *cr;
    float *cf_ir, *af_ir, *of_ir, *ar_ir, *or_ir, *cr_ir;
    int cf_o, af_o, of_o, ar_o, or_o, cr_o;
    /* Bass */
    float *ba_l, *ba_r;
    float *ba_ir;
    /* Whether to matrix decode the rear center channel */
    int matrix_mode;
    /* How to decode the input:
       0 = 5/5+1 channels
       1 = 2 channels
       2 = matrix encoded 2 channels */
    int decode_mode;
    /* Full wave rectified (FWR) amplitudes and gain used to steer the
       active matrix decoding of front channels (variable names
       lpr/lmr means Lt + Rt, Lt - Rt) */
    float l_fwr, r_fwr, lpr_fwr, lmr_fwr;
    float adapt_l_gain, adapt_r_gain, adapt_lpr_gain, adapt_lmr_gain;
    /* Matrix input decoding require special FWR buffer, since the
       decoding is done in place. */
    float *fwrbuf_l, *fwrbuf_r, *fwrbuf_lr, *fwrbuf_rr;
    /* Rear channel delay buffer for matrix decoding */
    float *rear_dlbuf;
    /* Full wave rectified amplitude and gain used to steer the active
       matrix decoding of center rear channel */
    float lr_fwr, rr_fwr, lrprr_fwr, lrmrr_fwr;
    float adapt_lr_gain, adapt_rr_gain;
    float adapt_lrprr_gain, adapt_lrmrr_gain;
    /* Cyclic position on the ring buffer */
    int cyc_pos;
};

/* Convolution on a ring buffer
 *    nx:	length of the ring buffer
 *    nk:	length of the convolution kernel
 *    sx:	ring buffer
 *    sk:	convolution kernel
 *    offset:	offset on the ring buffer, can be
 */
static float __FASTCALL__ conv(const int nx, const int nk, float *sx, float *sk,
		  const int offset)
{
    /* k = reminder of offset / nx */
    int k = offset >= 0 ? offset % nx : nx + (offset % nx);

    if(nk + k <= nx)
	return fir(nk, sx + k, sk);
    else
	return	fir(nk + k - nx, sx, sk + nx - k) +
		fir(nx - k, sx + k, sk);
}

/* Detect when the impulse response starts (significantly) */
static int __FASTCALL__ pulse_detect(float *sx)
{
    /* nmax must be the reference impulse response length (128) minus
       s->hrflen */
    const int nmax = 128 - HRTFFILTLEN;
    const float thresh = IRTHRESH;
    int i;

    for(i = 0; i < nmax; i++)
	if(fabs(sx[i]) > thresh)
	    return i;
    return 0;
}

/* Fuzzy matrix coefficient transfer function to "lock" the matrix on
   a effectively passive mode if the gain is approximately 1 */
inline float passive_lock(float x)
{
   const float x1 = x - 1;
   const float ax1s = fabs(x - 1) * (1.0 / MATAGCLOCK);

   return x1 - x1 / (1 + ax1s * ax1s) + 1;
}

/* Unified active matrix decoder for 2 channel matrix encoded surround
   sources */
inline void matrix_decode(real_t *in, const int k, const int il,
			  const int ir, const int decode_rear,
			  const int dlbuflen,
			  float l_fwr, float r_fwr,
			  float lpr_fwr, float lmr_fwr,
			  float *adapt_l_gain, float *adapt_r_gain,
			  float *adapt_lpr_gain, float *adapt_lmr_gain,
			  float *lf, float *rf, float *lr,
			  float *rr, float *cf)
{
   const int kr = (k + MATREARDELAY) % dlbuflen;
   float l_gain = (l_fwr + r_fwr) / (1 + l_fwr + l_fwr);
   float r_gain = (l_fwr + r_fwr) / (1 + r_fwr + r_fwr);
   /* The 2nd axis has strong gain fluctuations, and therefore require
      limits.  The factor corresponds to the 1 / amplification of (Lt
      - Rt) when (Lt, Rt) is strongly correlated. (e.g. during
      dialogues).  It should be bigger than -12 dB to prevent
      distortion. */
   float lmr_lim_fwr = lmr_fwr > M9_03DB * lpr_fwr ?
      lmr_fwr : M9_03DB * lpr_fwr;
   float lpr_gain = (lpr_fwr + lmr_lim_fwr) /
      (1 + lpr_fwr + lpr_fwr);
   float lmr_gain = (lpr_fwr + lmr_lim_fwr) /
      (1 + lmr_lim_fwr + lmr_lim_fwr);
   float lmr_unlim_gain = (lpr_fwr + lmr_fwr) /
      (1 + lmr_fwr + lmr_fwr);
   float lpr, lmr;
   float l_agc, r_agc, lpr_agc, lmr_agc;
   float f, d_gain, c_gain, c_agc_cfk;

#if 0
   static int counter = 0;
   static FILE *fp_out;

   if(counter == 0)
      fp_out = fopen("af_hrtf.log", "w");
   if(counter % 240 == 0)
      fprintf(fp_out, "%g %g %g %g %g ", counter * (1.0 / 48000),
	      l_gain, r_gain, lpr_gain, lmr_gain);
#endif

   /*** AXIS NO. 1: (Lt, Rt) -> (C, Ls, Rs) ***/
   /* AGC adaption */
   d_gain = (fabs(l_gain - *adapt_l_gain) +
	     fabs(r_gain - *adapt_r_gain)) * 0.5;
   f = d_gain * (1.0 / MATAGCTRIG);
   f = MATAGCDECAY - MATAGCDECAY / (1 + f * f);
   *adapt_l_gain = (1 - f) * *adapt_l_gain + f * l_gain;
   *adapt_r_gain = (1 - f) * *adapt_r_gain + f * r_gain;
   /* Matrix */
   l_agc = in[il] * passive_lock(*adapt_l_gain);
   r_agc = in[ir] * passive_lock(*adapt_r_gain);
   cf[k] = (l_agc + r_agc) * M_SQRT1_2;
   if(decode_rear) {
      lr[kr] = rr[kr] = (l_agc - r_agc) * M_SQRT1_2;
      /* Stereo rear channel is steered with the same AGC steering as
	 the decoding matrix. Note this requires a fast updating AGC
	 at the order of 20 ms (which is the case here). */
      lr[kr] *= (l_fwr + l_fwr) /
	 (1 + l_fwr + r_fwr);
      rr[kr] *= (r_fwr + r_fwr) /
	 (1 + l_fwr + r_fwr);
   }

   /*** AXIS NO. 2: (Lt + Rt, Lt - Rt) -> (L, R) ***/
   lpr = (in[il] + in[ir]) * M_SQRT1_2;
   lmr = (in[il] - in[ir]) * M_SQRT1_2;
   /* AGC adaption */
   d_gain = fabs(lmr_unlim_gain - *adapt_lmr_gain);
   f = d_gain * (1.0 / MATAGCTRIG);
   f = MATAGCDECAY - MATAGCDECAY / (1 + f * f);
   *adapt_lpr_gain = (1 - f) * *adapt_lpr_gain + f * lpr_gain;
   *adapt_lmr_gain = (1 - f) * *adapt_lmr_gain + f * lmr_gain;
   /* Matrix */
   lpr_agc = lpr * passive_lock(*adapt_lpr_gain);
   lmr_agc = lmr * passive_lock(*adapt_lmr_gain);
   lf[k] = (lpr_agc + lmr_agc) * M_SQRT1_2;
   rf[k] = (lpr_agc - lmr_agc) * M_SQRT1_2;

   /*** CENTER FRONT CANCELLATION ***/
   /* A heuristic approach exploits that Lt + Rt gain contains the
      information about Lt, Rt correlation.  This effectively reshapes
      the front and rear "cones" to concentrate Lt + Rt to C and
      introduce Lt - Rt in L, R. */
   /* 0.67677 is the emprical lower bound for lpr_gain. */
   c_gain = 8 * (*adapt_lpr_gain - 0.67677);
   c_gain = c_gain > 0 ? c_gain : 0;
   /* c_gain should not be too high, not even reaching full
      cancellation (~ 0.50 - 0.55 at current AGC implementation), or
      the center will s0und too narrow. */
   c_gain = MATCOMPGAIN / (1 + c_gain * c_gain);
   c_agc_cfk = c_gain * cf[k];
   lf[k] -= c_agc_cfk;
   rf[k] -= c_agc_cfk;
   cf[k] += c_agc_cfk + c_agc_cfk;
#if 0
   if(counter % 240 == 0)
      fprintf(fp_out, "%g %g %g %g %g\n",
	      *adapt_l_gain, *adapt_r_gain,
	      *adapt_lpr_gain, *adapt_lmr_gain,
	      c_gain);
   counter++;
#endif
}

inline void update_ch(af_hrtf_t *s, real_t *in, const int k)
{
    const int fwr_pos = (k + FWRDURATION) % s->dlbuflen;
    /* Update the full wave rectified total amplitude */
    /* Input matrix decoder */
    if(s->decode_mode == HRTF_MIX_MATRIX2CH) {
       s->l_fwr += abs(in[0]) - fabs(s->fwrbuf_l[fwr_pos]);
       s->r_fwr += abs(in[1]) - fabs(s->fwrbuf_r[fwr_pos]);
       s->lpr_fwr += abs(in[0] + in[1]) -
	  fabs(s->fwrbuf_l[fwr_pos] + s->fwrbuf_r[fwr_pos]);
       s->lmr_fwr += abs(in[0] - in[1]) -
	  fabs(s->fwrbuf_l[fwr_pos] - s->fwrbuf_r[fwr_pos]);
    }
    /* Rear matrix decoder */
    if(s->matrix_mode) {
       s->lr_fwr += abs(in[2]) - fabs(s->fwrbuf_lr[fwr_pos]);
       s->rr_fwr += abs(in[3]) - fabs(s->fwrbuf_rr[fwr_pos]);
       s->lrprr_fwr += abs(in[2] + in[3]) -
	  fabs(s->fwrbuf_lr[fwr_pos] + s->fwrbuf_rr[fwr_pos]);
       s->lrmrr_fwr += abs(in[2] - in[3]) -
	  fabs(s->fwrbuf_lr[fwr_pos] - s->fwrbuf_rr[fwr_pos]);
    }

    switch (s->decode_mode) {
    case HRTF_MIX_51:
       /* 5/5+1 channel sources */
       s->lf[k] = in[0];
       s->cf[k] = in[4];
       s->rf[k] = in[1];
       s->fwrbuf_lr[k] = s->lr[k] = in[2];
       s->fwrbuf_rr[k] = s->rr[k] = in[3];
       break;
    case HRTF_MIX_MATRIX2CH:
       /* Matrix encoded 2 channel sources */
       s->fwrbuf_l[k] = in[0];
       s->fwrbuf_r[k] = in[1];
       matrix_decode(in, k, 0, 1, 1, s->dlbuflen,
		     s->l_fwr, s->r_fwr,
		     s->lpr_fwr, s->lmr_fwr,
		     &(s->adapt_l_gain), &(s->adapt_r_gain),
		     &(s->adapt_lpr_gain), &(s->adapt_lmr_gain),
		     s->lf, s->rf, s->lr, s->rr, s->cf);
       break;
    case HRTF_MIX_STEREO:
       /* Stereo sources */
       s->lf[k] = in[0];
       s->rf[k] = in[1];
       s->cf[k] = s->lr[k] = s->rr[k] = 0;
       break;
    }

    /* We need to update the bass compensation delay line, too. */
    s->ba_l[k] = in[0] + in[4] + in[2];
    s->ba_r[k] = in[4] + in[1] + in[3];
}

static MPXP_Rc __FASTCALL__ config_af(af_instance_t* af,const af_conf_t* arg)
{
    af_hrtf_t *s = reinterpret_cast<af_hrtf_t*>(af->setup);
    MPXP_Rc test_output_res;
    af->conf.rate   = arg->rate;
    if(af->conf.rate != 48000) {
	// automatic samplerate adjustment in the filter chain
	// is not yet supported.
	//MSG_ERR("[hrtf] ERROR: Sampling rate is not 48000 Hz (%d)!\n",
	//	   af->data->rate);
	//return MPXP_Error;

	/* NK: Let use af_resample here */
	af->conf.rate = 48000;
    }
    af->conf.nch    = arg->nch;
    if(af->conf.nch == 2) {
	/* 2 channel input */
	if(s->decode_mode != HRTF_MIX_MATRIX2CH) {
	    /* Default behavior is stereo mixing. */
	    s->decode_mode = HRTF_MIX_STEREO;
	}
    }
    else if (af->conf.nch < 5) af->conf.nch = 5;
    af->conf.format = MPAF_F|MPAF_NE|MPAF_BPS_4;
    af->mul.n = 2;
    af->mul.d = af->conf.nch;
    test_output_res = af_test_output(af,arg);
    // after testing input set the real output format
    af->conf.nch = 2;
    return test_output_res;
}
/* Initialization and runtime control_af */
static MPXP_Rc __FASTCALL__ control_af(af_instance_t *af, int cmd, any_t* arg)
{
    af_hrtf_t *s = reinterpret_cast<af_hrtf_t*>(af->setup);
    char mode;

    switch(cmd) {
    case AF_CONTROL_COMMAND_LINE:
	sscanf((char*)arg, "%c", &mode);
	switch(mode) {
	case 'm':
	    /* Use matrix rear decoding. */
	    s->matrix_mode = 1;
	    break;
	case 's':
	    /* Input needs matrix decoding. */
	    s->decode_mode = HRTF_MIX_MATRIX2CH;
	    break;
	case '0':
	    s->matrix_mode = 0;
	    break;
	default:
	    MSG_ERR("[hrtf] Mode is neither 'm', 's', nor '0' (%c).\n",
		   mode);
	    return MPXP_Error;
	}
	return MPXP_Ok;
  case AF_CONTROL_SHOWCONF:
  {
	switch (s->decode_mode) {
	case HRTF_MIX_51:
	  MSG_INFO("[af_hrtf] Using HRTF to mix %s discrete surround into L, R channels\n", s->matrix_mode ? "5+1" : "5");
	  break;
	case HRTF_MIX_STEREO:
	  MSG_INFO("[af_hrtf] Using HRTF to mix stereo into L, R channels\n");
	  break;
	case HRTF_MIX_MATRIX2CH:
	  MSG_INFO("[af_hrtf] Using active matrix to decode 2 channel "
		   "input, HRTF to mix %s matrix surround into "
		   "L, R channels\n", "3/2");
	  break;
	default:
	  MSG_WARN("[af_hrtf] bogus decode_mode: %d\n", s->decode_mode);
	  break;
	}

	if(s->matrix_mode)
	  MSG_INFO("[af_hrtf] Using active matrix to decode rear center channel\n");
	return MPXP_Ok;
  }
  default: break;
  }
  return MPXP_Unknown;
}

/* Deallocate memory */
static void __FASTCALL__ uninit(af_instance_t *af)
{
    if(af->setup) {
	af_hrtf_t *s = reinterpret_cast<af_hrtf_t*>(af->setup);

	if(s->lf) delete s->lf;
	if(s->rf) delete s->rf;
	if(s->lr) delete s->lr;
	if(s->rr) delete s->rr;
	if(s->cf) delete s->cf;
	if(s->cr) delete s->cr;
	if(s->ba_l) delete s->ba_l;
	if(s->ba_r) delete s->ba_r;
	if(s->ba_ir) delete s->ba_ir;
	if(s->fwrbuf_l) delete s->fwrbuf_l;
	if(s->fwrbuf_r) delete s->fwrbuf_r;
	if(s->fwrbuf_lr) delete s->fwrbuf_lr;
	if(s->fwrbuf_rr) delete s->fwrbuf_rr;
	delete reinterpret_cast<af_hrtf_t*>(af->setup);
    }
}

/* Filter data through filter

Two "tricks" are used to compensate the "color" of the KEMAR data:

1. The KEMAR data is refiltered to ensure that the front L, R channels
on the same side of the ear are equalized (especially in the high
frequencies).

2. A bass compensation is introduced to ensure that 0-200 Hz are not
damped (without any real 3D acoustical image, however).
*/
static mp_aframe_t* __FASTCALL__ play(af_instance_t *af,const mp_aframe_t *ind)
{
    af_hrtf_t*	s = reinterpret_cast<af_hrtf_t*>(af->setup);
    real_t*	in = reinterpret_cast<real_t*>(ind->audio); // Input audio data
    real_t*	out = NULL; // Output audio data
    real_t*	end = in + ind->len / sizeof(real_t); // Loop end
    float	common, left, right, diff, left_b, right_b;
    const int	dblen = s->dlbuflen, hlen = s->hrflen, blen = s->basslen;
    mp_aframe_t*outd=new_mp_aframe_genome(ind);
    outd->len=af_lencalc(af->mul,ind);
    mp_alloc_aframe(outd);

    out = reinterpret_cast<real_t*>(outd->audio);

    /* MPlayer's 5 channel layout (notation for the variable):
     *
     * 0: L (LF), 1: R (RF), 2: Ls (LR), 3: Rs (RR), 4: C (CF), matrix
     * encoded: Cs (CR)
     *
     * or: L = left, C = center, R = right, F = front, R = rear
     *
     * Filter notation:
     *
     *      CF
     * OF        AF
     *      Ear->
     * OR        AR
     *      CR
     *
     * or: C = center, A = same side, O = opposite, F = front, R = rear
     */

    while(in < end) {
	const int k = s->cyc_pos;

	update_ch(s, in, k);

	/* Simulate a 7.5 ms -20 dB echo of the center channel in the
	   front channels (like reflection from a room wall) - a kind of
	   psycho-acoustically "cheating" to focus the center front
	   channel, which is normally hard to be perceived as front */
	s->lf[k] += CFECHOAMPL * s->cf[(k + CFECHODELAY) % s->dlbuflen];
	s->rf[k] += CFECHOAMPL * s->cf[(k + CFECHODELAY) % s->dlbuflen];

	switch (s->decode_mode) {
	case HRTF_MIX_51:
	case HRTF_MIX_MATRIX2CH:
	   /* Mixer filter matrix */
	   common = conv(dblen, hlen, s->cf, s->cf_ir, k + s->cf_o);
	   if(s->matrix_mode) {
	      /* In matrix decoding mode, the rear channel gain must be
		 renormalized, as there is an additional channel. */
	      matrix_decode(in, k, 2, 3, 0, s->dlbuflen,
			    s->lr_fwr, s->rr_fwr,
			    s->lrprr_fwr, s->lrmrr_fwr,
			    &(s->adapt_lr_gain), &(s->adapt_rr_gain),
			    &(s->adapt_lrprr_gain), &(s->adapt_lrmrr_gain),
			    s->lr, s->rr, NULL, NULL, s->cr);
	      common +=
		 conv(dblen, hlen, s->cr, s->cr_ir, k + s->cr_o) *
		 M1_76DB;
	      left    =
		 ( conv(dblen, hlen, s->lf, s->af_ir, k + s->af_o) +
		   conv(dblen, hlen, s->rf, s->of_ir, k + s->of_o) +
		   (conv(dblen, hlen, s->lr, s->ar_ir, k + s->ar_o) +
		    conv(dblen, hlen, s->rr, s->or_ir, k + s->or_o)) *
		   M1_76DB + common);
	      right   =
		 ( conv(dblen, hlen, s->rf, s->af_ir, k + s->af_o) +
		   conv(dblen, hlen, s->lf, s->of_ir, k + s->of_o) +
		   (conv(dblen, hlen, s->rr, s->ar_ir, k + s->ar_o) +
		    conv(dblen, hlen, s->lr, s->or_ir, k + s->or_o)) *
		   M1_76DB + common);
	   } else {
	      left    =
		 ( conv(dblen, hlen, s->lf, s->af_ir, k + s->af_o) +
		   conv(dblen, hlen, s->rf, s->of_ir, k + s->of_o) +
		   conv(dblen, hlen, s->lr, s->ar_ir, k + s->ar_o) +
		   conv(dblen, hlen, s->rr, s->or_ir, k + s->or_o) +
		   common);
	      right   =
		 ( conv(dblen, hlen, s->rf, s->af_ir, k + s->af_o) +
		   conv(dblen, hlen, s->lf, s->of_ir, k + s->of_o) +
		   conv(dblen, hlen, s->rr, s->ar_ir, k + s->ar_o) +
		   conv(dblen, hlen, s->lr, s->or_ir, k + s->or_o) +
		   common);
	   }
	   break;
	case HRTF_MIX_STEREO:
	   left    =
	      ( conv(dblen, hlen, s->lf, s->af_ir, k + s->af_o) +
		conv(dblen, hlen, s->rf, s->of_ir, k + s->of_o));
	   right   =
	      ( conv(dblen, hlen, s->rf, s->af_ir, k + s->af_o) +
		conv(dblen, hlen, s->lf, s->of_ir, k + s->of_o));
	   break;
	default:
	    /* make gcc happy */
	    left = 0.0;
	    right = 0.0;
	    break;
	}

	/* Bass compensation for the lower frequency cut of the HRTF.  A
	   cross talk of the left and right channel is introduced to
	   match the directional characteristics of higher frequencies.
	   The bass will not have any real 3D perception, but that is
	   OK (note at 180 Hz, the wavelength is about 2 m, and any
	   spatial perception is impossible). */
	left_b  = conv(dblen, blen, s->ba_l, s->ba_ir, k);
	right_b = conv(dblen, blen, s->ba_r, s->ba_ir, k);
	left  += (1 - BASSCROSS) * left_b  + BASSCROSS * right_b;
	right += (1 - BASSCROSS) * right_b + BASSCROSS * left_b;
	/* Also mix the LFE channel (if available) */
	if(ind->nch >= 6) {
	    left  += in[5] * M3_01DB;
	    right += in[5] * M3_01DB;
	}

	/* Amplitude renormalization. */
	left  *= AMPLNORM;
	right *= AMPLNORM;

	switch (s->decode_mode) {
	case HRTF_MIX_51:
	case HRTF_MIX_STEREO:
	   /* "Cheating": linear stereo expansion to amplify the 3D
	      perception.  Note: Too much will destroy the acoustic space
	      and may even result in headaches. */
	   diff = STEXPAND2 * (left - right);
	   out[0] = (real_t)(left  + diff);
	   out[1] = (real_t)(right - diff);
	   break;
	case HRTF_MIX_MATRIX2CH:
	   /* Do attempt any stereo expansion with matrix encoded
	      sources.  The L, R channels are already stereo expanded
	      by the steering, any further stereo expansion will sound
	      very unnatural. */
	   out[0] = (real_t)left;
	   out[1] = (real_t)right;
	   break;
	}

	/* Next sample... */
	in = &in[ind->nch];
	out = &out[af->conf.nch];
	(s->cyc_pos)--;
	if(s->cyc_pos < 0)
	    s->cyc_pos += dblen;
    }
    return outd;
}

static int __FASTCALL__ allocate(af_hrtf_t *s)
{
    if ((s->lf = new float [s->dlbuflen]) == NULL) return -1;
    if ((s->rf = new float [s->dlbuflen]) == NULL) return -1;
    if ((s->lr = new float [s->dlbuflen]) == NULL) return -1;
    if ((s->rr = new float [s->dlbuflen]) == NULL) return -1;
    if ((s->cf = new float [s->dlbuflen]) == NULL) return -1;
    if ((s->cr = new float [s->dlbuflen]) == NULL) return -1;
    if ((s->ba_l = new float [s->dlbuflen]) == NULL) return -1;
    if ((s->ba_r = new float [s->dlbuflen]) == NULL) return -1;
    if ((s->fwrbuf_l = new float [s->dlbuflen]) == NULL) return -1;
    if ((s->fwrbuf_r = new float [s->dlbuflen]) == NULL) return -1;
    if ((s->fwrbuf_lr = new float [s->dlbuflen]) == NULL) return -1;
    if ((s->fwrbuf_rr = new float [s->dlbuflen]) == NULL) return -1;
    return 0;
}

/* Allocate memory and set function pointers */
static MPXP_Rc __FASTCALL__ af_open(af_instance_t* af)
{
    int i;
    af_hrtf_t *s;
    float fc;

    af->config_af = config_af;
    af->control_af = control_af;
    af->uninit = uninit;
    af->play = play;
    af->mul.n = 1;
    af->mul.d = 1;
    af->setup = mp_calloc(1, sizeof(af_hrtf_t));
    if(af->setup == NULL) return MPXP_Error;

    s = reinterpret_cast<af_hrtf_t*>(af->setup);
    s->dlbuflen = DELAYBUFLEN;
    s->hrflen = HRTFFILTLEN;
    s->basslen = BASSFILTLEN;

    s->cyc_pos = s->dlbuflen - 1;
    /* With a full (two axis) steering matrix decoder, s->matrix_mode
       should not be enabled lightly (it will also steer the Ls, Rs
       channels). */
    s->matrix_mode = 0;
    s->decode_mode = HRTF_MIX_51;

    if (allocate(s) != 0) {
	MSG_ERR("[hrtf] Memory allocation error.\n");
	return MPXP_Error;
    }

    for(i = 0; i < s->dlbuflen; i++)
	s->lf[i] = s->rf[i] = s->lr[i] = s->rr[i] = s->cf[i] =
	    s->cr[i] = 0;

    s->lr_fwr =
	s->rr_fwr = 0;

    s->cf_ir = cf_filt + (s->cf_o = pulse_detect(cf_filt));
    s->af_ir = af_filt + (s->af_o = pulse_detect(af_filt));
    s->of_ir = of_filt + (s->of_o = pulse_detect(of_filt));
    s->ar_ir = ar_filt + (s->ar_o = pulse_detect(ar_filt));
    s->or_ir = or_filt + (s->or_o = pulse_detect(or_filt));
    s->cr_ir = cr_filt + (s->cr_o = pulse_detect(cr_filt));

    if((s->ba_ir = new float [s->basslen]) == NULL) {
	MSG_ERR("[hrtf] Memory allocation error.\n");
	return MPXP_Error;
    }
    fc = 2.0 * BASSFILTFREQ / (float)af->conf.rate;
    if(design_fir(s->basslen, s->ba_ir, &fc, LP | KAISER, 4 * M_PI) == -1) {
	MSG_ERR("[hrtf] Unable to design low-pass "
	       "filter.\n");
	return MPXP_Error;
    }
    for(i = 0; i < s->basslen; i++)
	s->ba_ir[i] *= BASSGAIN;

    check_pin("afilter",af->pin,AF_PIN);
    return MPXP_Ok;
}

/* Description of this filter */
extern const af_info_t af_info_hrtf = {
    "HRTF Headphone",
    "hrtf",
    "ylai",
    "",
    AF_FLAGS_REENTRANT,
    af_open
};
