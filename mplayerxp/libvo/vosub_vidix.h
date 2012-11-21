/*
 *  vosub_vidix.h
 *
 *	Copyright (C) Nickols_K <nickols_k@mail.ru> - 2002
 *
 *  You can redistribute this file under terms and conditions
 *  of GNU General Public licence v2.
 *
 * This file contains vosub_vidix interface to any mplayer's VO driver
 */

#ifndef __VOSUB_VIDIX_INCLUDED
#define __VOSUB_VIDIX_INCLUDED
#include "xmpcore/xmp_enums.h"

typedef struct vidix_server_s {
    vo_select_frame_t	select_frame;
    vo_control_t	control;
}vidix_server_t;

		    /* drvname can be NULL */
vidix_server_t*	 __FASTCALL__ vidix_preinit(vo_data_t* vo,const char *drvname,const any_t*server);
MPXP_Rc  __FASTCALL__ vidix_init(vo_data_t*,unsigned src_width,unsigned src_height,
		    unsigned dest_x,unsigned dest_y,unsigned dst_width,
		    unsigned dst_height,unsigned format,unsigned dest_bpp,
		    unsigned vid_w,unsigned vid_h);
int	 vidix_start(vo_data_t*);
int	 vidix_stop(vo_data_t*);
void     vidix_term(vo_data_t*);

#include <vidix/vidix.h>
/* graphic keys */
int __FASTCALL__ vidix_grkey_support(const vo_data_t*);
int __FASTCALL__ vidix_grkey_get(const vo_data_t*,vidix_grkey_t *gr_key);
int __FASTCALL__ vidix_grkey_set(const vo_data_t*,const vidix_grkey_t *gr_key);

#endif
