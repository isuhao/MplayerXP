#ifndef __MPLAYER_SUB_H
#define __MPLAYER_SUB_H
#include <inttypes.h>
#include "osd.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mp_osd_bbox_s {
    int x1,y1,x2,y2;
} mp_osd_bbox_t;

enum {
    OSDTYPE_OSD		=1,
    OSDTYPE_SUBTITLE	=2,
    OSDTYPE_PROGBAR	=3,
    OSDTYPE_SPU		=4,
    OSDTYPE_VOBSUB	=5,
    OSDTYPE_DVDNAV	=6,
    OSDTYPE_TELETEXT	=7
};

enum {
    OSDFLAG_VISIBLE	=1,
    OSDFLAG_CHANGED	=2,
    OSDFLAG_BBOX	=4,
    OSDFLAG_OLD_BBOX	=8,
    OSDFLAG_FORCE_UPDATE=16
};

enum {
    MAX_UCS		=1600,
    MAX_UCSLINES	=16
};
typedef struct mp_osd_obj_s {
    struct mp_osd_obj_s* next;
    unsigned char type;
    unsigned char alignment; // 2 bits: x;y percents, 2 bits: x;y relative to parent; 2 bits: alignment left/right/center
    unsigned short flags;
    int x,y;
    int dxs,dys;
    mp_osd_bbox_t bbox; // bounding box
    mp_osd_bbox_t old_bbox; // the renderer will save bbox here
    int cleared_frames; // The number of frames that has been cleared from old OSD, -1 = don't clear
    union {
	struct {
	    const any_t* sub;		// value of vo_sub at last update
	    int utbl[MAX_UCS+1];	// subtitle text
	    int xtbl[MAX_UCSLINES];	// x positions
	    int lines;			// no. of lines
	} subtitle;
	struct {
	    int elems;
	} progbar;
    } params;
    int stride;
    int allocated;
    unsigned char *alpha_buffer;
    unsigned char *bitmap_buffer;
} mp_osd_obj_t;

enum {
    OSD_PLAY		=0x01,
    OSD_PAUSE		=0x02,
    OSD_STOP		=0x03,
    OSD_REW		=0x04,
    OSD_FFW		=0x05,
    OSD_CLOCK		=0x06,
    OSD_CONTRAST	=0x07,
    OSD_SATURATION	=0x08,
    OSD_VOLUME		=0x09,
    OSD_BRIGHTNESS	=0x0A,
    OSD_HUE		=0x0B,
    OSD_DVDMENU		=0x0C,

    OSD_PB_START	=0x10,
    OSD_PB_0		=0x11,
    OSD_PB_END		=0x12,
    OSD_PB_1		=0x13
};

typedef struct sub_data_s {
    char *	cp;
    int		unicode;
    int		utf8;
    int		pos;
    int		bg_color; /* subtitles background color */
    int		bg_alpha;
}sub_data_t;
extern sub_data_t sub_data;

typedef void (* __FASTCALL__ draw_osd_f)(any_t*vo,unsigned idx,int x0,int y0, int w,int h,const unsigned char* src,const unsigned char *srca, int stride);
typedef void (* __FASTCALL__ clear_osd_f)(any_t*vo,unsigned idx,int x0,int y0, int w,int h);

/* for direct calll from XP-thread*/
extern void __FASTCALL__ vo_draw_spudec(any_t*vo,unsigned idx,int dxs,int dys,draw_osd_f draw_alpha);

extern void __FASTCALL__ vo_draw_text(any_t*vo,unsigned idx,int dxs,int dys, draw_osd_f draw_alpha);
extern void __FASTCALL__ vo_remove_text(any_t*vo,unsigned idx,int dxs,int dys,clear_osd_f remove);

void vo_init_osd(void);
int __FASTCALL__ vo_update_osd(any_t*vo,int dxs,int dys);
int __FASTCALL__ vo_osd_changed(int new_value);
int __FASTCALL__ get_osd_height(any_t* vo,int c,int h);
void __FASTCALL__ osd_set_nav_box (uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey);
#ifdef __cplusplus
}
#endif

#endif
