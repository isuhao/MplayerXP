#ifndef _MPLAYER_SPUDEC_H
#define _MPLAYER_SPUDEC_H

#include "libvo/video_out.h"
#include "libvo/sub.h"

extern int spu_alignment;
extern int spu_aamode;
extern float spu_gaussvar;

void __FASTCALL__ spudec_heartbeat(any_t*_this, unsigned int pts100);
void __FASTCALL__ spudec_now_pts(any_t*_this, unsigned int pts100);
void __FASTCALL__ spudec_assemble(any_t*_this, unsigned char *packet, unsigned int len, unsigned int pts100);
void __FASTCALL__ spudec_draw(any_t*this, draw_osd_f draw_alpha);
void __FASTCALL__ spudec_draw_scaled(any_t*_this, unsigned int dxs, unsigned int dys,draw_osd_f draw_alpha);
void __FASTCALL__ spudec_update_palette(any_t*_this,const unsigned int *palette);
any_t* __FASTCALL__ spudec_new_scaled(unsigned int *palette, unsigned int frame_width, unsigned int frame_height);
any_t* __FASTCALL__ spudec_new_scaled_vobsub(unsigned int *palette, unsigned int *cuspal, unsigned int custom, unsigned int frame_width, unsigned int frame_height);
any_t* __FASTCALL__ spudec_new(unsigned int *palette);
void __FASTCALL__ spudec_free(any_t*_this);
void __FASTCALL__ spudec_reset(any_t*_this);	// called after seek
int __FASTCALL__ spudec_visible(any_t*_this); // check if spu is visible
void __FASTCALL__ spudec_set_font_factor(any_t*_this, double factor); // sets the equivalent to ffactor
void __FASTCALL__ spudec_set_hw_spu(any_t*_this, vo_functions_t *hw_spu);
int __FASTCALL__ spudec_changed(any_t*_this);
void __FASTCALL__ spudec_calc_bbox(any_t*me, unsigned int dxs, unsigned int dys, unsigned int* bbox);
void __FASTCALL__ spudec_set_forced_subs_only(any_t* const _this, const unsigned int flag);

#endif

