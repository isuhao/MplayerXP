#ifdef __AD_ACM /* simply ugly hack */
#include "loader/wine/msacm.h"
#endif
#include "libmpconf/codec-cfg.h"

#include "libmpstream/stream.h"
#include "libmpdemux/demuxer_r.h"
#include "libmpdemux/stheader.h"

#include "ad.h"

#include "ad_msg.h"

static const audio_probe_t* __FASTCALL__ probe(ad_private_t* ctx,uint32_t wtag);
static MPXP_Rc __FASTCALL__ init(ad_private_t *ctx);
static ad_private_t* __FASTCALL__  preinit(sh_audio_t *ctx);
static void __FASTCALL__  uninit(ad_private_t *ctx);
static MPXP_Rc control_ad(ad_private_t *ctx,int cmd,any_t* arg, ...);
static unsigned __FASTCALL__  decode(ad_private_t *ctx,unsigned char *buf,unsigned minlen,unsigned maxlen,float *pts);

#define LIBAD_EXTERN(x) extern const ad_functions_t mpcodecs_ad_##x = {\
	&info,\
	options,\
	probe, \
	preinit,\
	init,\
	uninit,\
	control_ad,\
	decode\
};

