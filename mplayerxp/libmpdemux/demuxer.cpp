#include "mp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;
//=================== DEMUXER v2.5 =========================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "help_mp.h"
#include "libmpsub/subreader.h"
#include "libmpconf/cfgparser.h"

#include "osdep/fastmemcpy.h"
#include "libvo/sub.h"
#include "libao2/afmt.h"

#include "demux_msg.h"
#include "demuxer.h"
#include "demuxer_internal.h"
#include "libmpstream/stream.h"
#include "stheader.h"
#include "mplayerxp.h"

extern const demuxer_driver_t demux_aiff;
extern const demuxer_driver_t demux_rawaudio;
extern const demuxer_driver_t demux_rawvideo;
extern const demuxer_driver_t demux_avi;
extern const demuxer_driver_t demux_y4m;
extern const demuxer_driver_t demux_asf;
extern const demuxer_driver_t demux_nuv;
extern const demuxer_driver_t demux_nsv;
extern const demuxer_driver_t demux_mov;
extern const demuxer_driver_t demux_mkv;
extern const demuxer_driver_t demux_vivo;
extern const demuxer_driver_t demux_realaud;
extern const demuxer_driver_t demux_real;
extern const demuxer_driver_t demux_fli;
extern const demuxer_driver_t demux_film;
extern const demuxer_driver_t demux_roq;
extern const demuxer_driver_t demux_bmp;
extern const demuxer_driver_t demux_ogg;
extern const demuxer_driver_t demux_pva;
extern const demuxer_driver_t demux_smjpeg;
extern const demuxer_driver_t demux_vqf;
extern const demuxer_driver_t demux_mpxpav64;
extern const demuxer_driver_t demux_mpgps;
extern const demuxer_driver_t demux_mpgts;
extern const demuxer_driver_t demux_ty;
extern const demuxer_driver_t demux_audio;
extern const demuxer_driver_t demux_lavf;
extern const demuxer_driver_t demux_null;

namespace mpxp {
static const demuxer_driver_t *ddrivers[] =
{
    &demux_rawaudio,
    &demux_rawvideo,
    &demux_avi,
    &demux_y4m,
    &demux_asf,
    &demux_nsv,
    &demux_nuv,
    &demux_mov,
    &demux_mkv,
    &demux_vivo,
    &demux_realaud,
    &demux_real,
    &demux_fli,
    &demux_film,
    &demux_roq,
    &demux_bmp,
#ifdef HAVE_LIBVORBIS
    &demux_ogg,
#endif
    &demux_pva,
    &demux_smjpeg,
    &demux_vqf,
    &demux_mpxpav64,
    &demux_mpgps,
    &demux_aiff,
    &demux_audio,
    &demux_mpgts,
    &demux_ty,
    &demux_lavf,
    &demux_null,
    NULL
};

void libmpdemux_register_options(m_config_t* cfg)
{
    unsigned i;
    for(i=0;ddrivers[i];i++) {
	if(ddrivers[i]->options)
	    m_config_register_options(cfg,ddrivers[i]->options);
	if(ddrivers[i]==&demux_null) break;
    }
}

Demuxer::Demuxer()
	:_info(new(zeromem) Demuxer_Info)
{
}

Demuxer::Demuxer(stream_t *_stream,int a_id,int v_id,int s_id)
	:_info(new(zeromem) Demuxer_Info)
{
  fill_false_pointers(antiviral_hole,reinterpret_cast<long>(&pin)-reinterpret_cast<long>(&antiviral_hole));
  pin=DEMUX_PIN;
  stream=_stream;
  movi_start=_stream->start_pos;
  movi_end=_stream->end_pos;
  movi_length=UINT_MAX;
  flags|=Seekable;
  synced=0;
  filepos=0;
  audio=new(zeromem) Demuxer_Stream(this,a_id);
  video=new(zeromem) Demuxer_Stream(this,v_id);
  sub=new(zeromem) Demuxer_Stream(this,s_id);
  stream_reset(_stream);
  stream_seek(_stream,stream->start_pos);
}

Demuxer::~Demuxer() {
    unsigned i;
    MSG_V("DEMUXER: freeing demuxer at %p  \n",this);

    if(driver) driver->close(this);

    // free streams:
    for(i=0;i<MAX_A_STREAMS;i++) if(a_streams[i]) delete a_streams[i];
    for(i=0;i<MAX_V_STREAMS;i++) if(v_streams[i]) delete v_streams[i];
    // free demuxers:
    delete audio; audio=NULL;
    delete video; video=NULL;
    delete sub; sub=NULL;
}

sh_audio_t* Demuxer::get_sh_audio(int id) const
{
    if(id > MAX_A_STREAMS-1 || id < 0) {
	MSG_WARN("Requested audio stream id overflow (%d > %d)\n",
	    id, MAX_A_STREAMS);
	return NULL;
    }
    check_pin("demuxer",pin,DEMUX_PIN);
    return a_streams[id];
}

sh_audio_t* Demuxer::new_sh_audio_aid(int id,int aid) {
    if(id > MAX_A_STREAMS-1 || id < 0) {
	MSG_WARN("Requested audio stream id overflow (%d > %d)\n",
	    id, MAX_A_STREAMS);
	return NULL;
    }
    if(a_streams[id]) {
	MSG_WARN(MSGTR_AudioStreamRedefined,id);
    } else {
	sh_audio_t *sh;
	MSG_V("==> Found audio stream: %d\n",id);
	a_streams[id]=new(zeromem) sh_audio_t;
	sh = a_streams[id];
	// set some defaults
	sh->afmt=bps2afmt(2); /* PCM */
	sh->audio_out_minsize=8192;/* default size, maybe not enough for Win32/ACM*/
	  MSG_V("ID_AUDIO_ID=%d\n", aid);
    }
    a_streams[id]->id = aid;
    check_pin("demuxer",pin,DEMUX_PIN);
    return a_streams[id];
}

sh_video_t* Demuxer::get_sh_video(int id) const
{
    if(id > MAX_V_STREAMS-1 || id < 0) {
	MSG_WARN("Requested video stream id overflow (%d > %d)\n",
	    id, MAX_V_STREAMS);
	return NULL;
    }
    check_pin("demuxer",pin,DEMUX_PIN);
    return v_streams[id];
}

sh_video_t* Demuxer::new_sh_video_vid(int id,int vid) {
    if(id > MAX_V_STREAMS-1 || id < 0) {
	MSG_WARN("Requested video stream id overflow (%d > %d)\n",
	    id, MAX_V_STREAMS);
	return NULL;
    }
    if(v_streams[id]) {
	MSG_WARN(MSGTR_VideoStreamRedefined,id);
    } else {
	MSG_V("==> Found video stream: %d\n",id);
	v_streams[id]=new(zeromem) sh_video_t;
	  MSG_V("ID_VIDEO_ID=%d\n", vid);
    }
    v_streams[id]->id = vid;
    check_pin("demuxer",pin,DEMUX_PIN);
    return v_streams[id];
}

int Demuxer::fill_buffer(Demuxer_Stream *ds){
    /* Note: parameter 'ds' can be NULL! */
    return driver->demux(this,ds);
}

// ====================================================================
struct demux_conf {
    const char* audio_stream;
    const char* sub_stream;
    const char* type;
};
static demux_conf demux_conf;

const struct s_stream_txt_ids
{
    unsigned demuxer_id;
    unsigned stream_id;
}stream_txt_ids[]=
{
    { INFOT_AUTHOR, 	SCTRL_TXT_GET_STREAM_AUTHOR },
    { INFOT_NAME, 	SCTRL_TXT_GET_STREAM_NAME },
    { INFOT_SUBJECT, 	SCTRL_TXT_GET_STREAM_SUBJECT },
    { INFOT_COPYRIGHT, 	SCTRL_TXT_GET_STREAM_COPYRIGHT },
    { INFOT_DESCRIPTION,SCTRL_TXT_GET_STREAM_DESCRIPTION },
    { INFOT_ALBUM, 	SCTRL_TXT_GET_STREAM_ALBUM },
    { INFOT_DATE, 	SCTRL_TXT_GET_STREAM_DATE },
    { INFOT_TRACK, 	SCTRL_TXT_GET_STREAM_TRACK },
    { INFOT_GENRE, 	SCTRL_TXT_GET_STREAM_GENRE },
    { INFOT_ENCODER, 	SCTRL_TXT_GET_STREAM_ENCODER },
    { INFOT_SOURCE_MEDIA,SCTRL_TXT_GET_STREAM_SOURCE_MEDIA },
    { INFOT_RATING, 	SCTRL_TXT_GET_STREAM_RATING },
    { INFOT_COMMENTS, 	SCTRL_TXT_GET_STREAM_COMMENT },
    { INFOT_MIME, 	SCTRL_TXT_GET_STREAM_MIME }
};

static const demuxer_driver_t* demux_find_driver(const char *name) {
    unsigned i=0;
    for(;ddrivers[i]!=&demux_null;i++)
	if(strcmp(name,ddrivers[i]->short_name)==0) return ddrivers[i];
    return NULL;
}

Demuxer* demux_open_stream(stream_t *stream,int audio_id,int video_id,int dvdsub_id)
{
    unsigned i;
    Demuxer *demuxer=NULL,*new_demux=NULL;

    i=0;
    if(demux_conf.type) {
	const demuxer_driver_t* drv;
	drv=demux_find_driver(demux_conf.type);
	if(!drv) {
	    MSG_ERR("Can't find demuxer driver: '%s'\n",demux_conf.type);
	    goto err_exit;
	}
	MSG_V("Forcing %s ... ",drv->name);
	/* don't remove it from loop!!! (for initializing) */
	demuxer = new(zeromem) Demuxer(stream,audio_id,video_id,dvdsub_id);
	stream_reset(demuxer->stream);
	stream_seek(demuxer->stream,demuxer->stream->start_pos);
	if(drv->probe(demuxer)!=MPXP_Ok) {
	    MSG_ERR("Can't probe stream with driver: '%s'\n",demux_conf.type);
	    goto err_exit;
	}
	demuxer->driver = drv;
	goto force_driver;
    }
again:
    for(;ddrivers[i]!=&demux_null;i++) {
	MSG_V("Probing %s ... ",ddrivers[i]->name);
	/* don't remove it from loop!!! (for initializing) */
	demuxer = new(zeromem) Demuxer(stream,audio_id,video_id,dvdsub_id);
	stream_reset(demuxer->stream);
	stream_seek(demuxer->stream,demuxer->stream->start_pos);
	if(ddrivers[i]->probe(demuxer)==MPXP_Ok) {
	    MSG_V("OK\n");
	    demuxer->driver = ddrivers[i];
	    break;
	}
	MSG_V("False\n");
	delete demuxer; demuxer=NULL;
    }
    if(!demuxer || !demuxer->driver) {
err_exit:
	MSG_ERR(MSGTR_FormatNotRecognized);
	if(demuxer) { delete demuxer; demuxer=NULL; }
	return NULL;
    }
force_driver:
    if(!(new_demux=demuxer->driver->open(demuxer))) {
	MSG_ERR("Can't open stream with '%s'\n", demuxer->driver->name);
	demuxer->driver=NULL;
	i++;
	if(demux_conf.type)	goto err_exit;
	else			goto again;
    }
    demuxer=new_demux;
    MSG_OK("Using: %s\n",demuxer->driver->name);
    for(i=0;i<sizeof(stream_txt_ids)/sizeof(struct s_stream_txt_ids);i++)
    if(!demuxer->info().get(stream_txt_ids[i].demuxer_id)) {
	char stream_name[256];
	if(stream_control(demuxer->stream,stream_txt_ids[i].stream_id,stream_name) == MPXP_Ok) {
		demuxer->info().add(stream_txt_ids[i].demuxer_id,stream_name);
	}
    }
    stream->demuxer=demuxer;
    return demuxer;
}

Demuxer* demux_open(stream_t *vs,int audio_id,int video_id,int dvdsub_id){
  stream_t *as = NULL,*ss = NULL;
  Demuxer *vd,*ad = NULL,*sd = NULL;
  int afmt = 0,sfmt = 0;
  any_t* libinput=NULL;
#ifdef HAVE_STREAMIN
    libinput=vs->streaming_strl->libinput;
#endif

  if(demux_conf.audio_stream) {
    as = open_stream(libinput,demux_conf.audio_stream,&afmt,NULL);
    if(!as) {
      MSG_ERR("Can't open audio stream: %s\n",demux_conf.audio_stream);
      return NULL;
    }
  }
  if(demux_conf.sub_stream) {
    ss = open_stream(libinput,demux_conf.sub_stream,&sfmt,NULL);
    if(!ss) {
      MSG_ERR("Can't open subtitles stream: %s\n",demux_conf.sub_stream);
      return NULL;
    }
  }

  vd = demux_open_stream(vs,audio_id,video_id,dvdsub_id);
  if(!vd)
    return NULL;
  if(as) {
    ad = demux_open_stream(as,audio_id,-2,-2);
    if(!ad)
      MSG_WARN("Failed to open audio demuxer: %s\n",demux_conf.audio_stream);
    else if(ad->audio->sh && ((sh_audio_t*)ad->audio->sh)->wtag == 0x55) // MP3
      m_config_set_flag(mpxp_context().mconfig,"mp3.hr-seek",1); // Enable high res seeking
  }
  if(ss) {
    sd = demux_open_stream(ss,-2,-2,dvdsub_id);
    if(!sd)
      MSG_WARN("Failed to open subtitles demuxer: %s\n",demux_conf.sub_stream);
  }

  if(ad && sd)
    return new_demuxers_demuxer(vd,ad,sd);
  else if(ad)
    return new_demuxers_demuxer(vd,ad,vd);
  else if(sd)
    return new_demuxers_demuxer(vd,vd,sd);
  return vd;
}

int Demuxer::seek(const seek_args_t* seeka){
    sh_audio_t *sh_audio=reinterpret_cast<sh_audio_t*>(audio->sh);

    if(!(stream->type&STREAMTYPE_SEEKABLE))
    {
	MSG_WARN("Stream is not seekable\n");
	return 0;
    }
    if(!(flags&Seekable))
    {
	MSG_WARN("Demuxer is not seekable\n");
	return 0;
    }

    // clear demux buffers:
    if(sh_audio) { audio->free_packs(); sh_audio->a_buffer_len=0;}
    video->free_packs();

    stream_set_eof(stream,0); // clear eof flag
    video->eof=0;
    audio->eof=0;
    video->prev_pts=0;
    audio->prev_pts=0;

    if(sh_audio) sh_audio->timer=0;
    if(driver->seek) driver->seek(this,seeka);
    else MSG_WARN("Demuxer seek error\n");
    check_pin("demuxer",pin,DEMUX_PIN);
    return 1;
}

/******************* Options stuff **********************/

static const config_t demux_opts[] = {
  { "audiofile", &demux_conf.audio_stream, CONF_TYPE_STRING, 0, 0, 0, "forces reading of audio-stream from other file" },
  { "subfile", &demux_conf.sub_stream, CONF_TYPE_STRING, 0, 0, 0, "forces reading of subtitles from other file" },
  { "type", &demux_conf.type, CONF_TYPE_STRING, 0, 0, 0, "forces demuxer by given name" },
  { NULL, NULL, 0, 0, 0, 0, NULL}
};

static const config_t demuxer_opts[] = {
  { "demuxer", (any_t*)&demux_opts, CONF_TYPE_SUBCONFIG, 0, 0, 0, "Demuxer related options" },
  { NULL, NULL, 0, 0, 0, 0, NULL}
};

void demuxer_register_options(m_config_t* cfg) {
  m_config_register_options(cfg,demuxer_opts);
}

MPXP_Rc Demuxer::ctrl(int cmd, any_t*arg) const {

    if(driver)
	return driver->control(this,cmd,arg);
    check_pin("demuxer",pin,DEMUX_PIN);
    return MPXP_Unknown;
}

int Demuxer::switch_audio(int id) const
{
    if(id>MAX_A_STREAMS) id=0;
    if (ctrl(Demuxer::Switch_Audio, &id) == MPXP_Unknown)
	id = audio->id;
    check_pin("demuxer",pin,DEMUX_PIN);
    return id;
}

int Demuxer::switch_video(int id) const
{
    if(id>MAX_V_STREAMS) id=0;
    if (ctrl(Demuxer::Switch_Video, &id) == MPXP_Unknown)
	id = audio->id;
    check_pin("demuxer",pin,DEMUX_PIN);
    return id;
}

int Demuxer::switch_subtitle(int id) const
{
    if(id>MAX_S_STREAMS) id=0;
    if (ctrl(Demuxer::Switch_Subs, &id) == MPXP_Unknown)
	id = audio->id;
    check_pin("demuxer",pin,DEMUX_PIN);
    return id;
}

} // namespace mpxp
