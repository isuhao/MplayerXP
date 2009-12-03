/* Stuff for correct aspect scaling. */
#include "aspect.h"

//#define ASPECT_DEBUG

#ifdef ASPECT_DEBUG
#include <stdio.h>
#endif
#include "vo_msg.h"

float monitor_aspect=0;
float monitor_pixel_aspect=1;

static struct {
  int orgw; // real width
  int orgh; // real height
  int prew; // prescaled width
  int preh; // prescaled height
  int scrw; // horizontal resolution
  int scrh; // vertical resolution
} aspdat;

void __FASTCALL__ aspect_save_orig(int orgw, int orgh){
  aspdat.orgw = orgw;
  aspdat.orgh = orgh;
}

void __FASTCALL__ aspect_save_prescale(int prew, int preh){
  aspdat.prew = prew;
  aspdat.preh = preh;
}

void __FASTCALL__ aspect_save_screenres(int scrw, int scrh){
  aspdat.scrw = scrw;
  aspdat.scrh = scrh;
  monitor_aspect = monitor_pixel_aspect * scrw / scrh;
}

/* aspect is called with the source resolution and the
 * resolution, that the scaled image should fit into
 */

void __FASTCALL__ aspect(int *srcw, int *srch, int zoom){
  int tmpw;

#ifdef ASPECT_DEBUG
  MSG_DBG2("aspect(0) fitin: %dx%d zoom: %d \n",aspdat.scrw,aspdat.scrh,zoom);
  MSG_DBG2("aspect(1) wh: %dx%d (org: %dx%d)\n",*srcw,*srch,aspdat.prew,aspdat.preh);
#endif
  if(zoom){
    if(aspdat.prew >= aspdat.preh)
    {
	/* Landscape mode */
	*srcw = aspdat.scrw;
	*srch = (int)(((float)aspdat.scrw / (float)aspdat.prew * (float)aspdat.preh)
		* ((float)aspdat.scrh / ((float)aspdat.scrw / monitor_aspect)));
    }
    else
    {
	/* Portrait mode */
	*srch = aspdat.scrh;
	*srcw = (int)(((float)aspdat.scrh / (float)aspdat.preh * (float)aspdat.prew)
		* ((float)aspdat.scrw / ((float)aspdat.scrh * monitor_aspect)));
    }
  }else{
    *srcw = aspdat.prew;
    *srch = (int)((float)aspdat.preh
               * ((float)aspdat.scrh / ((float)aspdat.scrw / monitor_aspect)));
  }
  (*srch)+= (*srch)%2; // round
#ifdef ASPECT_DEBUG
  MSG_DBG2("aspect(2) wh: %dx%d (org: %dx%d)\n",*srcw,*srch,aspdat.prew,aspdat.preh);
#endif
  if(*srch>aspdat.scrh || *srch<aspdat.orgh){
    if(zoom)
      tmpw = (int)(((float)aspdat.scrh / (float)aspdat.preh * (float)aspdat.prew)
                * ((float)aspdat.scrw / ((float)aspdat.scrh / (1.0/monitor_aspect))));
    else
      tmpw = (int)((float)aspdat.prew
                * ((float)aspdat.scrw / ((float)aspdat.scrh / (1.0/monitor_aspect))));
    if(tmpw<=aspdat.scrw && tmpw>=aspdat.orgw){
      *srch = zoom?aspdat.scrh:aspdat.preh;
      *srcw = tmpw;
      *srcw+= *srcw%2; // round
    }
  }
#ifdef ASPECT_DEBUG
  MSG_DBG2("aspect(3) wh: %dx%d (org: %dx%d)\n",*srcw,*srch,aspdat.prew,aspdat.preh);
#endif
}

