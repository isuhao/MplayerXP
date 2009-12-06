#define DISP
/*
 * video_out_dga.c, X11 interface
 *
 *
 * Copyright ( C ) 2001, Andreas Ackermann. All Rights Reserved.
 *
 * <acki@acki-netz.de>
 *
 * Sourceforge username: acki2
 *
 * DRI_VO + XP mode support and lots of DGA1 - DGA2 related improvements
 * by Nickols_K <nickols_k@mail.ru>
 * Sourceforge username: nickols_k
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include "../mp_config.h"
#include "../mplayer.h"
#include "video_out.h"
#include "video_out_internal.h"
#include "aspect.h"

LIBVO_EXTERN( dga )

#include <X11/Xlib.h>
#include <X11/extensions/xf86dga.h>

#ifdef HAVE_XF86VM
#include <X11/extensions/xf86vmode.h>
#endif


#include "x11_common.h"
#include "fastmemcpy.h"
#include "dri_vo.h"
//#define VO_DGA_DBG 1
//#undef HAVE_DGA2
//#undef HAVE_XF86VM


static vo_info_t vo_info =
{
#ifdef HAVE_DGA2
        "X11/DGA ( Direct Graphic Access V2.0 )",
#else
        "X11/DGA ( Direct Graphic Access V1.0+XF86VidModeExtension )",
#endif
        "dga",
        "Andreas Ackermann <acki@acki-netz.de>, Nickols_K <nickols_k@mail.ru>",
        ""
};

#ifdef HAVE_XF86VM
static XF86VidModeModeInfo **vo_dga_vidmodes=NULL;
#endif

//------------------------------------------------------------------


//#define BITSPP (vo_dga_modes[vo_dga_active_mode].vdm_bitspp)
//#define BYTESPP (vo_dga_modes[vo_dga_active_mode].vdm_bytespp)

#define VO_DGA_INVALID_RES 100000

#define HW_MODE (vo_dga_modes[vo_dga_hw_mode])
#define SRC_MODE (vo_dga_modes[vo_dga_src_mode]) 

struct vd_modes {
  unsigned vdm_mplayer_depth;
  int    vdm_supported;
  unsigned vdm_depth;
  unsigned vdm_bitspp;
  unsigned vdm_bytespp;
  int    vdm_rmask;
  int    vdm_gmask;
  int    vdm_bmask;
  int    vdm_hw_mode;
  int    vdm_conversion_func;
};

//------------------------------------------------------------------

#define VDM_CONV_NATIVE 0
#define VDM_CONV_15TO16 1
#define VDM_CONV_24TO32 2

static struct vd_modes vo_dga_modes[] = {
  // these entries describe HW modes
  // however, we use the same entries to tell mplayer what we support
  // so the last two values describe, which HW mode to use and which conversion 
  // function to use for a mode that is not supported by HW

  {  0,  0,  0,  0, 0,          0,          0, 0,      0, 0},
  { 15,  0, 15, 16, 2,     0x7c00,     0x03e0, 0x001f, 2, VDM_CONV_15TO16 },
  { 16,  0, 16, 16, 2,     0xf800,     0x07e0, 0x001f, 2, VDM_CONV_NATIVE },
  { 24,  0, 24, 24, 3,   0xff0000,   0x00ff00, 0x0000ff, 4, VDM_CONV_24TO32},
  { 32,  0, 24, 32, 4, 0x00ff0000, 0x0000ff00, 0x000000ff, 4, VDM_CONV_NATIVE}
};

static unsigned vo_dga_mode_num = sizeof(vo_dga_modes)/sizeof(struct vd_modes);

// enable a HW mode (by description)
static int __FASTCALL__ vd_EnableMode( unsigned depth, unsigned bitspp, 
                    int rmask, int gmask, int bmask){
  unsigned i;
  for(i=1; i<vo_dga_mode_num; i++){
    if(vo_dga_modes[i].vdm_depth == depth &&
       vo_dga_modes[i].vdm_bitspp == bitspp &&
       vo_dga_modes[i].vdm_rmask == rmask &&
       vo_dga_modes[i].vdm_gmask == gmask &&
       vo_dga_modes[i].vdm_bmask == bmask){
       vo_dga_modes[i].vdm_supported = 1;
       vo_dga_modes[i].vdm_hw_mode = i;
       vo_dga_modes[i].vdm_conversion_func = VDM_CONV_NATIVE;
       return i;
    }
  }
  return 0;
}

static int __FASTCALL__ vd_ModeEqual(unsigned depth, unsigned bitspp, 
		 int rmask, int gmask, int bmask, int index){
  return (
     (vo_dga_modes[index].vdm_depth == depth &&
     vo_dga_modes[index].vdm_bitspp == bitspp &&
     vo_dga_modes[index].vdm_rmask == rmask &&
     vo_dga_modes[index].vdm_gmask == gmask &&
     vo_dga_modes[index].vdm_bmask == bmask)
     ? 1 : 0); 
}


// enable a HW mode (mplayer_depth decides which)
static int __FASTCALL__ vd_ValidateMode( unsigned mplayer_depth){
  unsigned i;
  if(mplayer_depth == 0)return 0;
  for(i=1; i<vo_dga_mode_num; i++){
    if(vo_dga_modes[i].vdm_mplayer_depth == mplayer_depth ){ 
      vo_dga_modes[i].vdm_supported = 1;
      vo_dga_modes[i].vdm_hw_mode = i;
      vo_dga_modes[i].vdm_conversion_func = VDM_CONV_NATIVE;
      return i;
    }
  }
  return 0;
}

// do we support this mode? (not important whether native or conversion)
static int __FASTCALL__ vd_ModeValid( unsigned mplayer_depth){
  unsigned i;
  if(mplayer_depth == 0)return 0;
  for(i=1; i<vo_dga_mode_num; i++){
    if(vo_dga_modes[i].vdm_mplayer_depth == mplayer_depth && 
       vo_dga_modes[i].vdm_supported != 0){
      return i;
    }
  }
  return 0;
}

static char * __FASTCALL__ vd_GetModeString(int index){

#define VO_DGA_MAX_STRING_LEN 100
  static char stringbuf[VO_DGA_MAX_STRING_LEN]; 
  stringbuf[VO_DGA_MAX_STRING_LEN-1]=0;
  snprintf(stringbuf, VO_DGA_MAX_STRING_LEN-2, 
    "depth=%d, bpp=%d, r=%06x, g=%06x, b=%06x, %s (-bpp %d)",
    vo_dga_modes[index].vdm_depth,
    vo_dga_modes[index].vdm_bitspp,
    vo_dga_modes[index].vdm_rmask,
    vo_dga_modes[index].vdm_gmask,
    vo_dga_modes[index].vdm_bmask,
    vo_dga_modes[index].vdm_supported ? 
    (vo_dga_modes[index].vdm_conversion_func == VDM_CONV_NATIVE ? 
        "native (fast),    " : "conversion (slow),") :
        "not supported :-( ",
    vo_dga_modes[index].vdm_mplayer_depth);
  return stringbuf;
}

//-----------------------------------------------------------------

static int	vo_dga_src_format;
static unsigned	vo_dga_width;           // bytes per line in framebuffer
static unsigned	vo_dga_vp_width;        // visible pixels per line in 
                                        // framebuffer
static unsigned	vo_dga_vp_height;       // visible lines in framebuffer
static int	vo_dga_is_running = 0; 
static unsigned	vo_dga_src_width;       // width of video in pixels
static unsigned	vo_dga_src_height;      // height of video in pixels
static unsigned	vo_dga_dest_width;      // width of scaled image in pixels
static unsigned	vo_dga_dest_height;     // height of scaled image in pixels
static unsigned	vo_dga_vp_offset=0;     // offset in dest
static unsigned	vo_dga_hw_mode = 0;     // index in mode list that is actually
                                         // used by framebuffer
static unsigned	vo_dga_src_mode = 0;    // index in mode list that is used by 
                                         // codec
static unsigned	vo_dga_XServer_mode = 0;// index in mode list for resolution
                                         // XServer is running

static unsigned	vo_dga_dbf_mem_offset[MAX_DRI_BUFFERS];  // offset in bytes for alternative 
                                         // framebuffer (0 if dbf is not 
					 // possible)
static unsigned	vo_dga_dbf_y_offset[MAX_DRI_BUFFERS]; /* viewport y offset (in scanlines) */
static unsigned	vo_dga_y_offset;
static unsigned udf_screenw,udf_screenh; /* X11 overwrites them so we should preserve them */

static unsigned char     *vo_dga_base;

/* XP-specific data */
static unsigned	num_buffers=1; // default

//---------------------------------------------------------

// quick & dirty - for debugging only 

#if 0
static void __FASTCALL__ fillblock(char *strt, int yoff, int lines, int val){
  char *i;
  for(i = strt + yoff * vo_dga_width *HW_MODE.vdm_bytespp; 
      i< strt + (lines+yoff) * vo_dga_width *HW_MODE.vdm_bytespp;  ){
    *i++ = val;
  }
}
#endif
//---------------------------------------------------------

static uint32_t check_events(void)
{
  return vo_x11_check_events(mDisplay,NULL);
}

//---------------------------------------------------------

#include "sub.h"

static void __FASTCALL__ flip_page( unsigned idx )
{
#ifdef HAVE_DGA2
    XDGASetViewport (mDisplay, XDefaultScreen(mDisplay), 
		    0, vo_dga_dbf_y_offset[idx],
		    XDGAFlipRetrace);
#else
    XF86DGASetViewPort (mDisplay, XDefaultScreen(mDisplay),
		        0, vo_dga_dbf_y_offset[idx]);
#endif
}

//---------------------------------------------------------

static const vo_info_t* get_info( void )
{ return &vo_info; }

//---------------------------------------------------------

static uint32_t __FASTCALL__ query_format( vo_query_fourcc_t *fourcc )
{

#ifdef HAVE_DGA2	
 XDGAMode *modelines;
 unsigned  modecount;
#endif
 Display  *qdisp;

 unsigned i;
 static int dga_depths_init = 0;
 if(dga_depths_init == 0){

   if((qdisp = XOpenDisplay(0))==NULL){
     MSG_ERR( "vo_dga: Can't open display!\n");
     return 0;
   }
   udf_screenw = vo_screenwidth;
   udf_screenh = vo_screenheight;
   if( !vo_x11_init() ){
    MSG_ERR( "vo_dga: vo_x11_init() failed!\n");
    return 1; 
   }
   vo_dga_XServer_mode = vd_ValidateMode(vo_depthonscreen);
 
   if(vo_dga_XServer_mode ==0){
#ifndef HAVE_DGA2
     MSG_ERR( "vo_dga: Your X-Server is not running in a ");
     MSG_ERR( "resolution supported by DGA driver!\n");
#endif     
   }
 
#ifdef HAVE_DGA2
   modelines=XDGAQueryModes(qdisp, XDefaultScreen(qdisp),&modecount);
   if(modelines){
     for(i=0; i< modecount; i++){
        MSG_DBG2( "vo_dga: (%03d) depth=%d, bpp=%d, r=%08x, g=%08x, b=%08x, %d x %d\n",
	  	i,
		modelines[i].depth,
		modelines[i].bitsPerPixel,
		modelines[i].redMask,
		modelines[i].greenMask,
	        modelines[i].blueMask,
	 	modelines[i].viewportWidth,
		modelines[i].viewportHeight);			  
        vd_EnableMode(
		modelines[i].depth,
		modelines[i].bitsPerPixel,
		modelines[i].redMask,
		modelines[i].greenMask,
	        modelines[i].blueMask);
     }
     XFree(modelines);

   }
#endif
   dga_depths_init = 1;
   XCloseDisplay(qdisp);

  if( !vo_dga_modes[1].vdm_supported && vo_dga_modes[2].vdm_supported ){
    vo_dga_modes[1].vdm_supported = 1;
  }

  if( !vo_dga_modes[3].vdm_supported && vo_dga_modes[4].vdm_supported ){
    vo_dga_modes[3].vdm_supported = 1;
  }

   for(i=1; i<vo_dga_mode_num; i++){
     MSG_V( "vo_dga: Mode: %s", vd_GetModeString(i));
     if(vo_dbpp && vo_dbpp != vo_dga_modes[i].vdm_mplayer_depth){
       vo_dga_modes[i].vdm_supported = 0;
       MSG_V( " ...disabled by -bpp %d", vo_dbpp );
     }
     MSG_V( "\n");
   }
 }
 switch(fourcc->fourcc)
 {
    case IMGFMT_BGR15: return vo_dga_modes[1].vdm_supported != 0;
    case IMGFMT_BGR16: return vo_dga_modes[2].vdm_supported != 0;
    case IMGFMT_BGR24: return vo_dga_modes[3].vdm_supported != 0;
    case IMGFMT_BGR32: return vo_dga_modes[4].vdm_supported != 0;
    default: break;
 }
 return 0;
}

//---------------------------------------------------------

static void uninit(void)
{

#ifdef HAVE_DGA2
  XDGADevice *dgadevice;
#endif

  if(vo_dga_is_running){	
    vo_dga_is_running = 0;
    MSG_DBG2( "vo_dga: in uninit\n");
    XUngrabPointer (mDisplay, CurrentTime);
    XUngrabKeyboard (mDisplay, CurrentTime);
#ifdef HAVE_DGA2
    XDGACloseFramebuffer(mDisplay, XDefaultScreen(mDisplay));
    dgadevice = XDGASetMode(mDisplay, XDefaultScreen(mDisplay), 0);
    if(dgadevice != NULL){
      XFree(dgadevice);	
    }
#else
    XF86DGADirectVideo (mDisplay, XDefaultScreen(mDisplay), 0);
    // first disable DirectVideo and then switch mode back!	
#ifdef HAVE_XF86VM
    if (vo_dga_vidmodes != NULL ){
      int screen; screen=XDefaultScreen( mDisplay );
      MSG_DBG2( "vo_dga: VidModeExt: Switching back..\n");
      // seems some graphics adaptors need this more than once ...
      XF86VidModeSwitchToMode(mDisplay,screen,vo_dga_vidmodes[0]);
      XF86VidModeSwitchToMode(mDisplay,screen,vo_dga_vidmodes[0]);
      XF86VidModeSwitchToMode(mDisplay,screen,vo_dga_vidmodes[0]);
      XF86VidModeSwitchToMode(mDisplay,screen,vo_dga_vidmodes[0]);
      XFree(vo_dga_vidmodes);
    }
#endif
#endif
    XCloseDisplay(mDisplay);
  }
}


/*----------------------------------------------------------*/
/* NK: since multibuffering maxY is meaningless */
static int __FASTCALL__ check_res( int num, int x, int y, int bpp,  
	       int new_x, int new_y, int new_vbi, int new_maxy,
                int *old_x, int *old_y, int *old_vbi, int *old_maxy){

//  MSG_V( "vo_dga: (%3d) Trying %4d x %4d @ %3d Hz @ depth %2d ..",
//          num, new_x, new_y, new_vbi, bpp );
//  MSG_V( "%dx%d (old: %dx%d@%d).", x, y, *old_x, *old_y, *old_vbi);
  if(new_x >= x && new_y >= y)
  {
    /* NK: TODO: check for BPP of video mode and select the best */
    if(
	(new_x < *old_x && new_y < *old_y)
	|| ((new_x == *old_x || new_y == *old_y) && new_vbi >= *old_vbi)
      )
    {
      *old_x = new_x;
      *old_y = new_y;
      *old_maxy = new_maxy;
      *old_vbi = new_vbi;
      MSG_V( ".ok!!\n");
      return 1;
    }
  }
  MSG_V( ".no\n");
  return 0;
}

/*---------------------------------------------------------*/

#ifdef HAVE_DGA2
static int __FASTCALL__ dga2_find_best_mode(unsigned wanted_width,unsigned wanted_height)
{
  XDGAMode *modelines=NULL;
  unsigned i,modecount,mVBI=100000, mMaxY=0,mX=VO_DGA_INVALID_RES, mY=VO_DGA_INVALID_RES,j=0;
  int dga_modenum;

  modelines=XDGAQueryModes(mDisplay, XDefaultScreen(mDisplay),&modecount);

  MSG_DBG2(
	    "vo_dga: modelines=%p, modecount=%d\n", modelines, modecount);

  if (modelines == NULL)
  {
    MSG_ERR( "vo_dga: can't get modelines\n");
    /* no mode is available */
    return -1;
  }
  
  MSG_V(
            "vo_dga: DGA 2.0 available! Can switch resolution AND depth!\n");	
  for (i=0; i<modecount; i++)
  {
  MSG_V( 
     "vo_dga: Trying hardware mode (%s) %ux%u@(depth %2d, bitspp %2d) *%3.2fHz [stride=%u]\n"
     ,modelines[i].name
     ,modelines[i].viewportWidth
     ,modelines[i].viewportHeight
     ,modelines[i].depth
     ,modelines[i].bitsPerPixel
     ,modelines[i].verticalRefresh
     ,modelines[i].bytesPerScanline/((modelines[i].bitsPerPixel+7)/8));
    if(vd_ModeEqual( modelines[i].depth, 
                     modelines[i].bitsPerPixel,
                     modelines[i].redMask,
		     modelines[i].greenMask,
	             modelines[i].blueMask,
                     vo_dga_hw_mode)){

       MSG_DBG2( "maxy: %4d, depth: %2d, %4dx%4d, ", 
                       modelines[i].maxViewportY, modelines[i].depth,
		       modelines[i].imageWidth, modelines[i].imageHeight );
       if ( check_res(i, wanted_width, wanted_height, modelines[i].depth,  
                  modelines[i].viewportWidth, 
                  modelines[i].viewportHeight, 
                  (unsigned) modelines[i].verticalRefresh, 
		  modelines[i].maxViewportY,
                   &mX, &mY, &mVBI, &mMaxY )) j = i;
     }
  }
  MSG_V( 
     "vo_dga: Found hardware mode (%s) %ux%u@(depth %2d, bitspp %2d) *%3.2fHz [stride=%u]\n"
     ,modelines[j].name
     ,modelines[j].viewportWidth
     ,modelines[j].viewportHeight
     ,modelines[j].depth
     ,modelines[j].bitsPerPixel
     ,modelines[j].verticalRefresh
     ,modelines[j].bytesPerScanline/((modelines[j].bitsPerPixel+7)/8));

  vo_dga_vp_width = mX;
  vo_dga_vp_height = mY;

  vo_dga_width = modelines[j].bytesPerScanline / HW_MODE.vdm_bytespp ;
  dga_modenum =  modelines[j].num;
  
  XFree(modelines);
  return dga_modenum;
}

static int __FASTCALL__ dga2_select_mode( int dga_modenum )
{
  XDGADevice *dgadevice;
  unsigned bank, ram_size;
  if (!XDGAOpenFramebuffer(mDisplay, XDefaultScreen(mDisplay))){
    MSG_ERR( "vo_dga: Framebuffer mapping failed!!!\n");
    XCloseDisplay(mDisplay);
    return 1;
  }
  if(dga_modenum != -1) 
  {
    dgadevice=XDGASetMode(mDisplay, XDefaultScreen(mDisplay), dga_modenum);
    XDGASync(mDisplay, XDefaultScreen(mDisplay));
    vo_dga_base = dgadevice->data;
    XFree(dgadevice);
  }
  else XF86DGAGetVideo (mDisplay, XDefaultScreen(mDisplay), 
		   (char **)&vo_dga_base, &vo_dga_width, &bank, &ram_size);
  XDGASetViewport (mDisplay, XDefaultScreen(mDisplay), 0, 0, XDGAFlipRetrace);
  return 0;
}
#else

#ifdef HAVE_XF86VM
static int __FASTCALL__ xf86vm_find_best_mode(unsigned wanted_width,unsigned wanted_height)
{

  unsigned vm_event, vm_error;
  unsigned vm_ver, vm_rev;
  int i, j=0, have_vm=0;
  int modecount, mX=VO_DGA_INVALID_RES, mY=VO_DGA_INVALID_RES, mVBI=100000, mMaxY=0, dga_modenum=-1;
  
  MSG_V(
     "vo_dga: DGA 1.0 compatibility code: Using XF86VidMode for mode switching!\n");

  if (XF86VidModeQueryExtension(mDisplay, &vm_event, &vm_error)) {
    XF86VidModeQueryVersion(mDisplay, &vm_ver, &vm_rev);
    MSG_V( "vo_dga: XF86VidMode Extension v%i.%i\n", vm_ver, vm_rev);
    have_vm=1;
  } else {
    MSG_ERR( "vo_dga: XF86VidMode Extension not available.\n");
    return -1;
  }

#define GET_VREFRESH(dotclk, x, y)( (((dotclk)/(x))*1000)/(y) )
  
  if (have_vm) {
    int screen;
    screen=XDefaultScreen(mDisplay);
    XF86VidModeGetAllModeLines(mDisplay,screen,&modecount,&vo_dga_vidmodes);

    if(vo_dga_vidmodes != NULL ){
      for (i=0; i<modecount; i++){
	if ( check_res(i, wanted_width, wanted_height, 
                        vo_dga_modes[vo_dga_hw_mode].vdm_depth,  
			vo_dga_vidmodes[i]->hdisplay, 
			vo_dga_vidmodes[i]->vdisplay,
			GET_VREFRESH(vo_dga_vidmodes[i]->dotclock, 
				     vo_dga_vidmodes[i]->htotal,
				     vo_dga_vidmodes[i]->vtotal),
		        0,
			&mX, &mY, &mVBI, &mMaxY )) j = i;
      }
    
      MSG_V(
 "vo_dga: Selected video mode %4d x %4d @ %3d Hz @ depth %2d, bitspp %2d\n", 
	mX, mY, mVBI, 
	vo_dga_modes[vo_dga_hw_mode].vdm_depth,
	vo_dga_modes[vo_dga_hw_mode].vdm_bitspp);
    }else{
      MSG_V( "vo_dga: XF86VidMode returned no screens - using current resolution.\n");
    }
    dga_modenum = j;
    vo_dga_vp_width = mX;
    vo_dga_vp_height = mY;
  }
  return dga_modenum;
}

static void __FASTCALL__ xf86vm_select_mode( int dga_modenum )
{
  if (dga_modenum != -1)
  {
    XF86VidModeLockModeSwitch(mDisplay,XDefaultScreen(mDisplay),0);
     /* Two calls are needed to switch modes on my ATI Rage 128. Why?
	for riva128 one call is enough! */
    XF86VidModeSwitchToMode(mDisplay,XDefaultScreen(mDisplay),vo_dga_vidmodes[dga_modenum]);
    XF86VidModeSwitchToMode(mDisplay,XDefaultScreen(mDisplay),vo_dga_vidmodes[dga_modenum]);
  }
}
#endif
#endif

static int x_off, y_off, dstFourcc;
static uint32_t __FASTCALL__ config( uint32_t width,  uint32_t height,
                      uint32_t d_width,uint32_t d_height,
                      uint32_t flags,char *title,uint32_t format,const vo_tune_info_t *info )
{

  unsigned wanted_width, wanted_height;
  int dga_modenum;
  int bank;
  unsigned dest_frame_size,ram_size,freq;
  unsigned mX,mY;
  UNUSED(title);
  UNUSED(info);
  if( vo_dga_is_running )return -1;

  vo_dga_src_format = format;
  vo_dga_src_width = vo_dga_dest_width = width;
  vo_dga_src_height = vo_dga_dest_height = height;

  wanted_width = d_width;
  wanted_height = d_height;

  if(!wanted_height) wanted_height = height;
  if(!wanted_width)  wanted_width = width;
  
  if(udf_screenw) wanted_width = udf_screenw;
  if(udf_screenh) wanted_height = udf_screenh;

  if( !vo_x11_init() ){
    MSG_ERR( "vo_dga: vo_x11_init() failed!\n");
    return 1; 
  }

  if( !vo_dbpp ) vo_dga_src_mode = vo_dga_XServer_mode;
  else		 vo_dga_src_mode = vd_ModeValid(vo_dbpp);
  vo_dga_hw_mode = SRC_MODE.vdm_hw_mode;

  if(vo_dga_src_mode != vo_dga_hw_mode ){
    MSG_ERR(
    "vo_dga: swScaler supports native modes only. Using %d instead of selected %d.\n",
       HW_MODE.vdm_mplayer_depth,
       SRC_MODE.vdm_mplayer_depth );
    vo_dga_src_mode = vo_dga_hw_mode;
  }

  if(!vo_dga_src_mode){ 
    MSG_ERR( "vo_dga: unsupported video format!\n");
    return 1;
  }

  if((mDisplay = XOpenDisplay(0))==NULL){
    MSG_ERR( "vo_dga: Can't open display\n");
    return 1;
  } 

// choose a suitable mode ...
  
#ifdef HAVE_DGA2
  dga_modenum = dga2_find_best_mode(wanted_width,wanted_height);
#else
#ifdef HAVE_XF86VM
  dga_modenum = xf86vm_find_best_mode(wanted_width,wanted_height);
#else
  UNUSED(dga_modenum);
  MSG_V( "vo_dga: Only have DGA 1.0 extension and no XF86VidMode :-(\n");
  MSG_V( "        Thus, resolution switching is NOT possible.\n");
#endif
#endif
  
  MSG_V(
     "vo_dga: Video parameters by codec: %3d x %3d, depth %2d, bitspp %2d.\n", 
     width, height,
     SRC_MODE.vdm_depth,
     SRC_MODE.vdm_bitspp);

/* now lets start the DGA thing */

#ifdef HAVE_DGA2
  if(dga2_select_mode(dga_modenum)) return 1;
#else
  
#ifdef HAVE_XF86VM
    xf86vm_select_mode(dga_modenum);
#endif
  XF86DGADirectVideo (mDisplay, XDefaultScreen(mDisplay),
                      XF86DGADirectGraphics | XF86DGADirectMouse |
                      XF86DGADirectKeyb);
  
  XF86DGASetViewPort (mDisplay, XDefaultScreen(mDisplay), 0, 0);

#endif
  /* for both DGA1 and DGA2 we need to know ram_size */
  XF86DGAGetVideo (mDisplay, XDefaultScreen(mDisplay), 
		   (char **)&vo_dga_base, &vo_dga_vp_width, &bank, &ram_size);

  XF86DGAGetViewPortSize(mDisplay,DefaultScreen(mDisplay),&mX,&mY);
  vo_dga_vp_width = mX;
  vo_dga_vp_height = mY;

  MSG_DBG2( "vo_dga: XServer res: %dx%d\n", 
                     vo_dga_vp_width, vo_dga_vp_height);

  if(vo_dga_dest_width > vo_dga_vp_width ||
     vo_dga_dest_height > vo_dga_vp_height)
  {
     MSG_ERR( "vo_dga: Sorry, video larger than viewport is not yet supported!\n");
     // ugly, do something nicer in the future ...
#ifndef HAVE_DGA2
#ifdef HAVE_XF86VM
     if(vo_dga_vidmodes){
	XFree(vo_dga_vidmodes);
	vo_dga_vidmodes = NULL;
     }
#endif
#endif
     return 1;
  }

  if((flags&0x04)||(flags&0x01))
  { /* -zoom or -fs */
    vo_dga_dest_width = d_width;
    vo_dga_dest_height = d_height;
    
    aspect_save_orig(vo_dga_src_width,vo_dga_src_height);
    aspect_save_screenres(mX,mY);
    aspect_save_prescale(vo_dga_dest_width,vo_dga_dest_height);
    if((flags&0x05)==0x05) /* -fs -zoom */
      aspect(&vo_dga_dest_width,&vo_dga_dest_height,A_ZOOM);
    else if(flags&0x04)/* -zoom */
      aspect(&vo_dga_dest_width,&vo_dga_dest_height,A_NOZOOM);
    else /* -fs */
    {
       vo_dga_dest_width = mX;
       vo_dga_dest_height = mY;
    }
    MSG_V(
       "vo_dga: Aspect corrected size for SwScaler: %4d x %4d.\n",
       vo_dga_dest_width, vo_dga_dest_height);
  }
  switch(HW_MODE.vdm_bitspp)
  {
    case 32: dstFourcc = IMGFMT_BGR32; break;
    case 24: dstFourcc = IMGFMT_BGR24; break;
    case 15: dstFourcc = IMGFMT_BGR15; break;
    default: dstFourcc = IMGFMT_BGR16; break;
  }

  MSG_DBG2( "vo_dga: bytes/line: %d, screen res: %dx%d, depth: %d, base: %08x, bpp: %d\n", 
          vo_dga_width, vo_dga_vp_width, 
          vo_dga_vp_height, HW_MODE.vdm_bytespp, vo_dga_base,
          HW_MODE.vdm_bitspp);

  x_off = (vo_dga_vp_width - vo_dga_dest_width)>>1; 
  vo_dga_y_offset = y_off = (vo_dga_vp_height - vo_dga_dest_height)>>1;

  vo_dga_vp_offset = (y_off * vo_dga_width + x_off ) * HW_MODE.vdm_bytespp;

  MSG_DBG2( "vo_dga: vp_off=%d\n", vo_dga_vp_offset);

  
  XGrabKeyboard (mDisplay, DefaultRootWindow(mDisplay), True, 
                 GrabModeAsync,GrabModeAsync, CurrentTime);
  XGrabPointer (mDisplay, DefaultRootWindow(mDisplay), True, 
                ButtonPressMask,GrabModeAsync, GrabModeAsync, 
                None, None, CurrentTime);
// TODO: check if mem of graphics adaptor is large enough for dbf

  // set up variables for double buffering ...
  vo_dga_dbf_y_offset[0] = 0;
  vo_dga_dbf_mem_offset[0] = 0;
  dest_frame_size = vo_dga_width*HW_MODE.vdm_bytespp*vo_dga_vp_height;

  if(vo_doublebuffering)
  {
   num_buffers = (ram_size*1024)/dest_frame_size;
   if(num_buffers > vo_da_buffs) num_buffers = vo_da_buffs;
   if(num_buffers > MAX_DRI_BUFFERS) num_buffers = MAX_DRI_BUFFERS;
   
   for(freq=1;freq<num_buffers;freq++)
   {
	vo_dga_dbf_y_offset[freq] = vo_dga_dbf_y_offset[freq-1] + vo_dga_vp_height;
	vo_dga_dbf_mem_offset[freq] = vo_dga_dbf_mem_offset[freq-1] + dest_frame_size;
   }
  }
  /* clear screen */
  for(freq=0;freq<num_buffers;freq++)
  {
	void *d;
	d=(&((char*)vo_dga_base)[vo_dga_dbf_mem_offset[freq]]);
	memset(d,0,dest_frame_size);
  }
  MSG_V("vo_dga: Doublebuffering is %s.\n",num_buffers>1?"enabled":"disabled");
  vo_dga_is_running = 1;
  return 0;
}

static uint32_t __FASTCALL__ preinit(const char *arg)
{
    if(arg) 
    {
	MSG_V( "vo_dga: Unknown subdevice: %s\n",arg);
	return ENOSYS;
    }
    return 0;
}

static void __FASTCALL__ dga_dri_get_surface_caps(dri_surface_cap_t *caps)
{
    caps->caps = DRI_CAP_VIDEO_MMAPED;
    caps->fourcc=dstFourcc;
    caps->width=vo_dga_vp_width;
    caps->height=vo_dga_vp_height;
    caps->x=x_off;
    caps->y=y_off;
    caps->w=vo_dga_dest_width;
    caps->h=vo_dga_dest_height;
    caps->strides[0] = vo_dga_width*((HW_MODE.vdm_bitspp+7)/8);
    caps->strides[1] = 0;
    caps->strides[2] = 0;
    caps->strides[3] = 0;
}

static void __FASTCALL__ dga_dri_get_surface(dri_surface_t *surf)
{
    surf->planes[0] = vo_dga_base + vo_dga_dbf_mem_offset[surf->idx];
    surf->planes[1] = 0;
    surf->planes[2] = 0;
    surf->planes[3] = 0;
}

static uint32_t __FASTCALL__ control(uint32_t request, void *data)
{
  switch (request) {
  case VOCTRL_QUERY_FORMAT:
    return query_format((vo_query_fourcc_t*)data);
  case VOCTRL_CHECK_EVENTS:
    check_events();
    (*(vo_resize_t *)data).event_type = 0; /* VO_EVENT_RESIZE is meaningless here */
    return VO_TRUE;
  case VOCTRL_GET_NUM_FRAMES:
	*(uint32_t *)data = num_buffers;
	return VO_TRUE;
  case DRI_GET_SURFACE_CAPS:
	dga_dri_get_surface_caps(data);
	return VO_TRUE;
  case DRI_GET_SURFACE: 
	dga_dri_get_surface(data);
	return VO_TRUE;
  }
  return VO_NOTIMPL;
}









