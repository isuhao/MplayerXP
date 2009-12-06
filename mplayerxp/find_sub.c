//**************************************************************************//
//             .SUB 
//**************************************************************************//

#include "mp_config.h"

#ifdef USE_OSD

#include <stdio.h>
#include <limits.h>
#define MSGT_CLASS MSGT_FINDSUB
#include "__mp_msg.h"
#include "libvo/video_out.h"
#include "libvo/sub.h"
#include "subreader.h"

static int current_sub=0;

//static subtitle* subtitles=NULL;
static unsigned long nosub_range_start=ULONG_MAX;
static unsigned long nosub_range_end=ULONG_MAX;

void find_sub(subtitle* subtitles,unsigned long key){
    int i,j;
    
    if ( !subtitles ) return;
    
    if(vo_sub){
      if(key>=vo_sub->start && key<=vo_sub->end) return; // OK!
    } else {
      if(key>nosub_range_start && key<nosub_range_end) return; // OK!
    }
    // sub changed!

    /* Tell the OSD subsystem that the OSD contents will change soon */
    vo_osd_changed(OSDTYPE_SUBTITLE);

    if(key<=0){
      vo_sub=NULL; // no sub here
      return;
    }
    
    
    // check next sub.
    if(current_sub>=0 && current_sub+1<sub_num){
      if(key>subtitles[current_sub].end && key<subtitles[current_sub+1].start){
          // no sub
          nosub_range_start=subtitles[current_sub].end;
          nosub_range_end=subtitles[current_sub+1].start;
          vo_sub=NULL;
          return;
      }
      // next sub?
      ++current_sub;
      vo_sub=&subtitles[current_sub];
      if(key>=vo_sub->start && key<=vo_sub->end) return; // OK!
    }

    
    // use logarithmic search:
    i=0;j=sub_num-1;
    while(j>=i){
        current_sub=(i+j+1)/2;
        vo_sub=&subtitles[current_sub];
        if(key<vo_sub->start) j=current_sub-1;
        else if(key>vo_sub->end) i=current_sub+1;
        else return; // found!
    }
//    if(key>=vo_sub->start && key<=vo_sub->end) return; // OK!
    
    // check where are we...
    if(key<vo_sub->start){
      if(current_sub<=0){
          // before the first sub
          nosub_range_start=key-1; // tricky
          nosub_range_end=vo_sub->start;
          vo_sub=NULL;
          return;
      }
      --current_sub;
      if(key>subtitles[current_sub].end && key<subtitles[current_sub+1].start){
          // no sub
          nosub_range_start=subtitles[current_sub].end;
          nosub_range_end=subtitles[current_sub+1].start;
          vo_sub=NULL;
          return;
      }
      MSG_V("HEH????  ");
    } else {
      if(key<=vo_sub->end) MSG_V("JAJJ!  "); else
      if(current_sub+1>=sub_num){
          // at the end?
          nosub_range_start=vo_sub->end;
          nosub_range_end=0x7FFFFFFF; // MAXINT
          vo_sub=NULL;
          return;
      } else
      if(key>subtitles[current_sub].end && key<subtitles[current_sub+1].start){
          // no sub
          nosub_range_start=subtitles[current_sub].end;
          nosub_range_end=subtitles[current_sub+1].start;
          vo_sub=NULL;
          return;
      }
    }
    
    MSG_ERR("SUB ERROR:  %d  ?  %d --- %d  [%d]  \n",key,(int)vo_sub->start,(int)vo_sub->end,current_sub);

    vo_sub=NULL; // no sub here
}

#endif
