/*
   ad.c - audio decoder interface
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mp_config.h"

#include "stream.h"
#include "demuxer.h"
#include "stheader.h"
#include "ad.h"

/* Missed vorbis, mad, dshow */

extern const ad_functions_t mpcodecs_ad_null;
extern const ad_functions_t mpcodecs_ad_mp3;
extern const ad_functions_t mpcodecs_ad_ffmp3;
extern const ad_functions_t mpcodecs_ad_a52;
extern const ad_functions_t mpcodecs_ad_dca;
extern const ad_functions_t mpcodecs_ad_hwac3;
extern const ad_functions_t mpcodecs_ad_pcm;
extern const ad_functions_t mpcodecs_ad_libdv;
extern const ad_functions_t mpcodecs_ad_dvdpcm;
extern const ad_functions_t mpcodecs_ad_dshow;
extern const ad_functions_t mpcodecs_ad_msacm;
extern const ad_functions_t mpcodecs_ad_faad;
extern const ad_functions_t mpcodecs_ad_vorbis;
extern const ad_functions_t mpcodecs_ad_real;
extern const ad_functions_t mpcodecs_ad_twin;
extern const ad_functions_t mpcodecs_ad_dmo;
extern const ad_functions_t mpcodecs_ad_qtaudio;

const ad_functions_t* mpcodecs_ad_drivers[] =
{
  &mpcodecs_ad_null,
  &mpcodecs_ad_mp3,
  &mpcodecs_ad_a52,
  &mpcodecs_ad_dca,
  &mpcodecs_ad_hwac3,
  &mpcodecs_ad_ffmp3,
  &mpcodecs_ad_pcm,
  &mpcodecs_ad_dvdpcm,
  &mpcodecs_ad_faad,
#ifdef HAVE_LIBVORBIS
  &mpcodecs_ad_vorbis,
#endif
#ifdef HAVE_LIBDV
  &mpcodecs_ad_libdv,
#endif
  &mpcodecs_ad_real,
#ifdef HAVE_WIN32LOADER
  &mpcodecs_ad_dshow,
  &mpcodecs_ad_twin,
  &mpcodecs_ad_msacm,
  &mpcodecs_ad_dmo,
  &mpcodecs_ad_qtaudio,
#endif
  NULL
};

static unsigned int nddrivers=sizeof(mpcodecs_ad_drivers)/sizeof(ad_functions_t*);

void libmpcodecs_ad_register_options(m_config_t* cfg)
{
  unsigned i;
  for(i=0;i<nddrivers;i++)
  {
    if(mpcodecs_ad_drivers[i])
	if(mpcodecs_ad_drivers[i]->options)
	    m_config_register_options(cfg,mpcodecs_ad_drivers[i]->options);
  }
}

void afm_help(void) {
  unsigned i;
  MSG_INFO("Available audio codec families/drivers:\n");
  for(i=0;i<nddrivers;i++) {
    if(mpcodecs_ad_drivers[i])
	if(mpcodecs_ad_drivers[i]->options)
	    MSG_INFO("\t%-10s %s\n",mpcodecs_ad_drivers[i]->info->driver_name,mpcodecs_ad_drivers[i]->info->descr);
  }
  MSG_INFO("\n");
}
