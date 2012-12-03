#ifndef DEC_VIDEO_H_INCLUDED
#define DEC_VIDEO_H_INCLUDED 1
#include "xmpcore/xmp_enums.h"
#include "libmpdemux/demuxer_r.h"
#include "libmpstream/stream.h"
#include "libmpdemux/stheader.h"

struct video_decoder_t {
    any_t*	vd_private;
};

// dec_video.c:
extern video_decoder_t*	__FASTCALL__ mpcv_init(sh_video_t *sh_video, const char *codec_name,const char *family,int status,any_t*libinput);
extern void		__FASTCALL__ mpcv_uninit(video_decoder_t *handle);
extern video_decoder_t*	__FASTCALL__ mpcv_lavc_init(sh_video_t*,any_t* libinput);
extern int		__FASTCALL__ mpcv_decode(video_decoder_t *handle,const enc_frame_t* frame);

extern MPXP_Rc		__FASTCALL__ mpcv_get_quality_max(video_decoder_t *handle,unsigned *qual);
extern MPXP_Rc		__FASTCALL__ mpcv_set_quality(video_decoder_t *handle,int quality);
extern MPXP_Rc		__FASTCALL__ mpcv_set_colors(video_decoder_t *handle,const char *item,int value);
extern void		__FASTCALL__ mpcv_resync_stream(video_decoder_t *handle);

extern void		vfm_help(void);
#endif
