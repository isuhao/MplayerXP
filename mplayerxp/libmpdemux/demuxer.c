//=================== DEMUXER v2.5 =========================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "stream.h"
#include "../mp_config.h"
#include "demux_msg.h"
#include "help_mp.h"
#include "../mplayer.h"
#include "../subreader.h"
#include "../cfgparser.h"
#include "../nls/nls.h"

#include "demuxer.h"
#include "stheader.h"

#include "../libvo/fastmemcpy.h"

extern demuxer_driver_t demux_aiff;
extern demuxer_driver_t demux_rawaudio;
extern demuxer_driver_t demux_rawvideo;
extern demuxer_driver_t demux_avi;
extern demuxer_driver_t demux_y4m;
extern demuxer_driver_t demux_asf;
extern demuxer_driver_t demux_nuv;
extern demuxer_driver_t demux_nsv;
extern demuxer_driver_t demux_mov;
extern demuxer_driver_t demux_mkv;
extern demuxer_driver_t demux_vivo;
extern demuxer_driver_t demux_ra;
extern demuxer_driver_t demux_real;
extern demuxer_driver_t demux_fli;
extern demuxer_driver_t demux_film;
extern demuxer_driver_t demux_roq;
extern demuxer_driver_t demux_bmp;
extern demuxer_driver_t demux_ogg;
extern demuxer_driver_t demux_pva;
extern demuxer_driver_t demux_smjpeg;
extern demuxer_driver_t demux_vqf;
extern demuxer_driver_t demux_mpxpav64;
extern demuxer_driver_t demux_mpgps;
extern demuxer_driver_t demux_mpgts;
extern demuxer_driver_t demux_ty;
extern demuxer_driver_t demux_audio;
extern demuxer_driver_t demux_lavf;

static demuxer_driver_t *ddrivers[] =
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
    &demux_ra,
    &demux_real,
    &demux_fli,
    &demux_film,
    &demux_roq,
    &demux_bmp,
    &demux_ogg,
    &demux_pva,
    &demux_smjpeg,
    &demux_vqf,
    &demux_mpxpav64,
    &demux_mpgps,
    &demux_aiff,
    &demux_audio,
    &demux_mpgts,
    &demux_ty,
    &demux_lavf
};
static unsigned int nddrivers=sizeof(ddrivers)/sizeof(demuxer_driver_t*);

typedef struct demuxer_info_st {
  char *id[INFOT_MAX];
} demuxer_info_t;

void libmpdemux_register_options(m_config_t* cfg)
{
  unsigned i;
  for(i=0;i<nddrivers;i++)
  {
    if(ddrivers[i]->options)
	m_config_register_options(cfg,ddrivers[i]->options);
  }
}

void free_demuxer_stream(demux_stream_t *ds){
    if(ds)
    {
	ds_free_packs(ds);
	free(ds);
    }
}

int demux_aid_vid_mismatch = 0;

demux_stream_t* new_demuxer_stream(struct demuxer_st *demuxer,int id){
  demux_stream_t* ds=malloc(sizeof(demux_stream_t));
  ds->buffer_pos=ds->buffer_size=0;
  ds->buffer=NULL;
  ds->pts=0;
  ds->pts_bytes=0;
  ds->eof=0;
  ds->pos=0;
  ds->dpos=0;
  ds->pack_no=0;
//---------------
  ds->packs=0;
  ds->bytes=0;
  ds->first=ds->last=ds->current=NULL;
  ds->id=id;
  ds->demuxer=demuxer;
//----------------
  ds->asf_seq=-1;
  ds->asf_packet=NULL;
//----------------
  ds->ss_mul=ds->ss_div=0;
//----------------
  ds->sh=NULL;
  ds->pts_flags=0;
  ds->prev_pts=ds->pts_corr=0;
  return ds;
}

demuxer_t* new_demuxer(stream_t *stream,int type,int a_id,int v_id,int s_id){
  demuxer_t *d=malloc(sizeof(demuxer_t));
  memset(d,0,sizeof(demuxer_t));
  d->stream=stream;
  d->movi_start=stream->start_pos;
  d->movi_end=stream->end_pos;
  d->movi_length=UINT_MAX;
  d->flags|=DEMUXF_SEEKABLE;
  d->synced=0;
  d->filepos=0;
  d->audio=new_demuxer_stream(d,a_id);
  d->video=new_demuxer_stream(d,v_id);
  d->sub=new_demuxer_stream(d,s_id);
  d->file_format=type;
  d->info=malloc(sizeof(demuxer_info_t));
  memset(d->info,0,sizeof(demuxer_info_t));
  stream_reset(stream);
  stream_seek(stream,stream->start_pos);
  return d;
}

sh_audio_t *get_sh_audio(demuxer_t *demuxer, int id)
{
    if(id > MAX_A_STREAMS-1 || id < 0)
    {
	MSG_WARN("Requested audio stream id overflow (%d > %d)\n",
	    id, MAX_A_STREAMS);
	return NULL;
    }
    return demuxer->a_streams[id];
}

sh_audio_t* new_sh_audio_aid(demuxer_t *demuxer,int id,int aid){
    if(id > MAX_A_STREAMS-1 || id < 0)
    {
	MSG_WARN("Requested audio stream id overflow (%d > %d)\n",
	    id, MAX_A_STREAMS);
	return NULL;
    }
    if(demuxer->a_streams[id]){
        MSG_WARN(MSGTR_AudioStreamRedefined,id);
    } else {
        sh_audio_t *sh;
        MSG_V("==> Found audio stream: %d\n",id);
        demuxer->a_streams[id]=calloc(1, sizeof(sh_audio_t));
        sh = demuxer->a_streams[id];
        // set some defaults
        sh->samplesize=2;
        sh->sample_format=0x01; /* PCM */
        sh->audio_out_minsize=8192;/* default size, maybe not enough for Win32/ACM*/
          MSG_V("ID_AUDIO_ID=%d\n", aid);
    }
    ((sh_audio_t *)demuxer->a_streams[id])->aid = aid;
    return demuxer->a_streams[id];
}

void free_sh_audio(sh_audio_t* sh){
    MSG_V("DEMUXER: freeing sh_audio at %p  \n",sh);
    if(sh->wf) free(sh->wf);
    free(sh);
}

sh_video_t *get_sh_video(demuxer_t *demuxer, int id)
{
    if(id > MAX_V_STREAMS-1 || id < 0)
    {
	MSG_WARN("Requested video stream id overflow (%d > %d)\n",
	    id, MAX_V_STREAMS);
	return NULL;
    }
    return demuxer->v_streams[id];
}

sh_video_t* new_sh_video_vid(demuxer_t *demuxer,int id,int vid){
    if(id > MAX_V_STREAMS-1 || id < 0)
    {
	MSG_WARN("Requested video stream id overflow (%d > %d)\n",
	    id, MAX_V_STREAMS);
	return NULL;
    }
    if(demuxer->v_streams[id]){
        MSG_WARN(MSGTR_VideoStreamRedefined,id);
    } else {
        MSG_V("==> Found video stream: %d\n",id);
        demuxer->v_streams[id]=calloc(1, sizeof(sh_video_t));
          MSG_V("ID_VIDEO_ID=%d\n", vid);
    }
    ((sh_video_t *)demuxer->v_streams[id])->vid = vid;
    return demuxer->v_streams[id];
}

void free_sh_video(sh_video_t* sh){
    MSG_V("DEMUXER: freeing sh_video at %p  \n",sh);
    if(sh->bih) free(sh->bih);
    free(sh);
}

void free_demuxer(demuxer_t *demuxer){
    unsigned i;
    if(demuxer)
    {
	MSG_V("DEMUXER: freeing demuxer at %p  \n",demuxer);

	if(demuxer->driver) demuxer->driver->close(demuxer);

	// free streams:
	for(i=0;i<256;i++){
	    if(demuxer->a_streams[i]) free_sh_audio(demuxer->a_streams[i]);
	    if(demuxer->v_streams[i]) free_sh_video(demuxer->v_streams[i]);
	}
	//if(sh_audio) free_sh_audio(sh_audio);
	//if(sh_video) free_sh_video(sh_video);
	// free demuxers:
	FREE_DEMUXER_STREAM(demuxer->audio);
	FREE_DEMUXER_STREAM(demuxer->video);
	FREE_DEMUXER_STREAM(demuxer->sub);
	demux_info_free(demuxer);
	free(demuxer);
    }
}


void ds_add_packet(demux_stream_t *ds,demux_packet_t* dp){
//    demux_packet_t* dp=new_demux_packet(len);
//    stream_read(stream,dp->buffer,len);
//    dp->pts=pts; //(float)pts/90000.0f;
//    dp->pos=pos;
    // append packet to DS stream:
    if(dp->len>0)
    {
	++ds->packs;
	ds->bytes+=dp->len;
	if(ds->last){
	    // next packet in stream
	    ds->last->next=dp;
	    ds->last=dp;
	} else {
	    // first packet in stream
	    ds->first=ds->last=dp;
	}
	MSG_DBG2("DEMUX: Append packet to %s, len=%d  pts=%5.3f  pos=%u  [packs: A=%d V=%d]\n",
	    (ds==ds->demuxer->audio)?"d_audio":"d_video",
	    dp->len,dp->pts,(unsigned int)dp->pos,ds->demuxer->audio->packs,ds->demuxer->video->packs);
    }
    else
	MSG_DBG2("DEMUX: Skip packet for %s, len=%d  pts=%5.3f  pos=%u  [packs: A=%d V=%d]\n",
	    (ds==ds->demuxer->audio)?"d_audio":"d_video",
	    dp->len,dp->pts,(unsigned int)dp->pos,ds->demuxer->audio->packs,ds->demuxer->video->packs);
}

void ds_read_packet(demux_stream_t *ds,stream_t *stream,int len,float pts,off_t pos,int flags){
    demux_packet_t* dp=new_demux_packet(len);
    len=stream_read(stream,dp->buffer,len);
    resize_demux_packet(dp,len);
    dp->pts=pts; //(float)pts/90000.0f;
    dp->pos=pos;
    dp->flags=flags;
    // append packet to DS stream:
    ds_add_packet(ds,dp);
    MSG_DBG2("ds_read_packet(%s,%u,%f,%llu,%i)\n",ds==ds->demuxer->video?"video":"audio",len,pts,pos,flags);
}

int demux_fill_buffer(demuxer_t *demux,demux_stream_t *ds){
    /* Note: parameter 'ds' can be NULL! */
    return demux->driver->demux(demux,ds);
}

// return value:
//     0 = EOF
//     1 = succesfull
int ds_fill_buffer(demux_stream_t *ds){
  demuxer_t *demux=ds->demuxer;
  if(ds->buffer) free(ds->buffer);
  if(verbose>2){
    if(ds==demux->audio)
	MSG_DBG3("ds_fill_buffer(d_audio) called\n");
    else
    if(ds==demux->video) 
	MSG_DBG3("ds_fill_buffer(d_video) called\n");
    else
    if(ds==demux->sub)
	MSG_DBG3("ds_fill_buffer(d_sub) called\n");
    else
        MSG_DBG3("ds_fill_buffer(unknown %p) called\n",ds);
  }
  while(1){
    if(ds->packs){
      demux_packet_t *p=ds->first;
      // copy useful data:
      ds->buffer=p->buffer;
      ds->buffer_pos=0;
      ds->buffer_size=p->len;
      ds->pos=p->pos;
      ds->dpos+=p->len; // !!!
      ++ds->pack_no;
      if(p->pts){
        ds->pts=p->pts;
        ds->pts_bytes=0;
      }
      ds->pts_bytes+=p->len; // !!!
      ds->flags=p->flags;
      // free packet:
      ds->bytes-=p->len;
      ds->current=p;
      ds->first=p->next;
      if(!ds->first) ds->last=NULL;
      --ds->packs;
      return 1; //ds->buffer_size;
    }
    if(demux->audio->bytes>=MAX_PACK_BYTES){
      MSG_ERR(MSGTR_TooManyAudioInBuffer,demux->audio->packs,demux->audio->bytes);
      MSG_HINT(MSGTR_MaybeNI);
      break;
    }
    if(demux->video->bytes>=MAX_PACK_BYTES){
      MSG_ERR(MSGTR_TooManyVideoInBuffer,demux->video->packs,demux->video->bytes);
      MSG_HINT(MSGTR_MaybeNI);
      break;
    }
    if(!demux->driver){
       MSG_DBG2("ds_fill_buffer: demux->driver==NULL failed\n");
       break; // EOF
    }
    if(!demux->driver->demux(demux,ds)){
       MSG_DBG2("ds_fill_buffer: demux->driver->demux() failed\n");
       break; // EOF
    }
  }
  ds->buffer_pos=ds->buffer_size=0;
  ds->buffer=NULL;
  ds->current=NULL;
  MSG_V("ds_fill_buffer: EOF reached (stream: %s)  \n",ds==demux->audio?"audio":"video");
  ds->eof=1;
  return 0;
}

int demux_read_data(demux_stream_t *ds,unsigned char* mem,int len){
int x;
int bytes=0;
while(len>0){
  x=ds->buffer_size-ds->buffer_pos;
  if(x==0){
    if(!ds_fill_buffer(ds)) return bytes;
  } else {
    if(x>len) x=len;
    if(x<0) return bytes; /* BAD!!! sometime happens. Broken stream, driver, gcc ??? */
    if(mem) memcpy(mem+bytes,&ds->buffer[ds->buffer_pos],x);
    bytes+=x;len-=x;ds->buffer_pos+=x;
  }
}
return bytes;
}

void ds_free_packs(demux_stream_t *ds){
  demux_packet_t *dp=ds->first;
  while(dp){
    demux_packet_t *dn=dp->next;
    free_demux_packet(dp);
    dp=dn;
  }
  if(ds->asf_packet){
    // free unfinished .asf fragments:
    free(ds->asf_packet->buffer);
    free(ds->asf_packet);
    ds->asf_packet=NULL;
  }
  ds->first=ds->last=NULL;
  ds->packs=0; // !!!!!
  ds->bytes=0;
  if(ds->current) free(ds->current);
  ds->current=NULL;
  ds->buffer=NULL;
  ds->buffer_pos=ds->buffer_size;
  ds->pts=0; ds->pts_bytes=0;
}

void ds_free_packs_until_pts(demux_stream_t *ds,float pts){
  demux_packet_t *dp=ds->first;
  unsigned packs,bytes;
  packs=bytes=0;
  while(dp){
    demux_packet_t *dn=dp->next;
    if(dp->pts >= pts) break;
    packs++;
    bytes+=dp->len;
    free_demux_packet(dp);
    dp=dn;
  }
  if(!dp)
  {
    if(ds->asf_packet){
	// free unfinished .asf fragments:
	free(ds->asf_packet->buffer);
	free(ds->asf_packet);
	ds->asf_packet=NULL;
    }
    ds->first=ds->last=NULL;
    ds->packs=0; // !!!!!
    ds->bytes=0;
    ds->pts=0;
  }
  else
  {
    ds->first=dp;
    ds->packs-=packs;
    ds->bytes-=bytes;
    ds->pts=dp->pts;
  }
  if(ds->current) free(ds->current);
  ds->current=NULL;
  ds->buffer=NULL;
  ds->buffer_pos=ds->buffer_size;
  ds->pts_bytes=0;
}

int ds_get_packet(demux_stream_t *ds,unsigned char **start){
    while(1){
        int len;
        if(ds->buffer_pos>=ds->buffer_size){
          if(!ds_fill_buffer(ds)){
            // EOF
            *start = NULL;
            return -1;
          }
        }
        len=ds->buffer_size-ds->buffer_pos;
        *start = &ds->buffer[ds->buffer_pos];
        ds->buffer_pos+=len;
        return len;
    }
}

int ds_get_packet_sub(demux_stream_t *ds,unsigned char **start){
    while(1){
        int len;
        if(ds->buffer_pos>=ds->buffer_size){
          *start = NULL;
          if(!ds->packs) return -1; // no sub
          if(!ds_fill_buffer(ds)) return -1; // EOF
        }
        len=ds->buffer_size-ds->buffer_pos;
        *start = &ds->buffer[ds->buffer_pos];
        ds->buffer_pos+=len;
        return len;
    }
}

float ds_get_next_pts(demux_stream_t *ds) {
  demuxer_t* demux = ds->demuxer;
  while(!ds->first) {
    if(demux->audio->bytes>=MAX_PACK_BYTES){
      MSG_ERR(MSGTR_TooManyAudioInBuffer,demux->audio->packs,demux->audio->bytes);
      MSG_HINT(MSGTR_MaybeNI);
      return -1;
    }
    if(demux->video->bytes>=MAX_PACK_BYTES){
      MSG_ERR(MSGTR_TooManyVideoInBuffer,demux->video->packs,demux->video->bytes);
      MSG_HINT(MSGTR_MaybeNI);
      return -1;
    }
    if(!demux_fill_buffer(demux,ds))
      return -1;
  }
  return ds->first->pts;
}

// ====================================================================
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
static demuxer_t* demux_open_stream(stream_t *stream,int file_format,int audio_id,int video_id,int dvdsub_id)
{
    unsigned i;
    demuxer_t *demuxer=NULL,*new_demux=NULL;

    pts_from_bps=0;
    demux_aid_vid_mismatch = 0;
    i=0;
    again:
    for(;i<nddrivers;i++)
    {
	/* don't remove it from loop!!! (for initializing) */
	demuxer = new_demuxer(stream,DEMUXER_TYPE_UNKNOWN,audio_id,video_id,dvdsub_id);
	MSG_V("Probing %s ... ",ddrivers[i]->name);
	stream_reset(demuxer->stream);
	stream_seek(demuxer->stream,demuxer->stream->start_pos);
	if(ddrivers[i]->probe(demuxer))
	{
	    MSG_V("OK\n");
	    demuxer->driver = ddrivers[i];
	    break;
	}
	MSG_V("False\n");
	FREE_DEMUXER(demuxer);
    }
    if(!demuxer || !demuxer->driver)
    {
	MSG_ERR(MSGTR_FormatNotRecognized);
        FREE_DEMUXER(demuxer);
	return NULL;
    }

    if(!(new_demux=demuxer->driver->open(demuxer)))
    {
	MSG_ERR("Can't open stream with '%s'\n", demuxer->driver->name);
	demuxer->driver=NULL;
	i++;
	goto again;
    }
    demuxer=new_demux;
    MSG_OK("Using: %s\n",demuxer->driver->name);
    for(i=0;i<sizeof(stream_txt_ids)/sizeof(struct s_stream_txt_ids);i++)
    if(!demux_info_get(demuxer,stream_txt_ids[i].demuxer_id))
    {
	char stream_name[256];
	if(demuxer->stream->driver->control)
	{
	    if(demuxer->stream->driver->control(demuxer->stream,stream_txt_ids[i].stream_id,stream_name) == SCTRL_OK)
	    {
		demux_info_add(demuxer,stream_txt_ids[i].demuxer_id,stream_name);
	    }
	}
    }
    return demuxer;
}

static char* audio_stream = NULL;
static char* sub_stream = NULL;
static int demuxer_type = 0, audio_demuxer_type = 0, sub_demuxer_type = 0;

demuxer_t* demux_open(stream_t *vs,int file_format,int audio_id,int video_id,int dvdsub_id){
  stream_t *as = NULL,*ss = NULL;
  demuxer_t *vd,*ad = NULL,*sd = NULL;
  int afmt = 0,sfmt = 0;

  if(audio_stream) {
    as = open_stream(audio_stream,&afmt,NULL);
    if(!as) {
      MSG_ERR("Can't open audio stream: %s\n",audio_stream);
      return NULL;
    }
  }
  if(sub_stream) {
    ss = open_stream(sub_stream,&sfmt,NULL);
    if(!ss) {
      MSG_ERR("Can't open subtitles stream: %s\n",sub_stream);
      return NULL;
    }
  }

  vd = demux_open_stream(vs,demuxer_type ? demuxer_type : file_format,audio_stream ? -2 : audio_id,video_id, sub_stream ? -2 : dvdsub_id);
  if(!vd)
    return NULL;
  if(as) {
    ad = demux_open_stream(as,audio_demuxer_type ? audio_demuxer_type : afmt,audio_id,-2,-2);
    if(!ad)
      MSG_WARN("Failed to open audio demuxer: %s\n",audio_stream);
    else if(ad->audio->sh && ((sh_audio_t*)ad->audio->sh)->format == 0x55) // MP3
      m_config_set_flag(mconfig,"hr-mp3-seek",1); // Enable high res seeking
  }
  if(ss) {
    sd = demux_open_stream(ss,sub_demuxer_type ? sub_demuxer_type : sfmt,-2,-2,dvdsub_id);
    if(!sd)
      MSG_WARN("Failed to open subtitles demuxer: %s\n",sub_stream);
  }

  if(ad && sd)
    return new_demuxers_demuxer(vd,ad,sd);
  else if(ad)
    return new_demuxers_demuxer(vd,ad,vd);
  else if(sd)
    return new_demuxers_demuxer(vd,vd,sd);
  return vd;
}

int demux_seek(demuxer_t *demuxer,float rel_seek_secs,int flags){
    demux_stream_t *d_audio=demuxer->audio;
    demux_stream_t *d_video=demuxer->video;
    sh_audio_t *sh_audio=d_audio->sh;
    sh_video_t *sh_video=d_video->sh;

    if(!(demuxer->stream->type&STREAMTYPE_SEEKABLE))
    {
	MSG_WARN("Stream is not seekable\n");
	return 0;
    }
    if(!(demuxer->flags&DEMUXF_SEEKABLE))
    {
	MSG_WARN("Demuxer is not seekable\n");
	return 0;
    }

    // clear demux buffers:
    if(sh_audio){ ds_free_packs(d_audio);sh_audio->a_buffer_len=0;}
    ds_free_packs(d_video);
    
    stream_set_eof(demuxer->stream,0); // clear eof flag
    demuxer->video->eof=0;
    demuxer->audio->eof=0;
    demuxer->video->prev_pts=0;
    demuxer->audio->prev_pts=0;

#if 0
    if(sh_audio) sh_audio->timer=sh_video->timer;
#else
    if(sh_audio) sh_audio->timer=0;
    if(sh_video) sh_video->timer=0; // !!!!!!
#endif
    if(demuxer->driver->seek) demuxer->driver->seek(demuxer,rel_seek_secs,flags);
    else MSG_WARN("Demuxer seek error\n");
    return 1;
}

static const char *info_names[INFOT_MAX] =
{
    "Author",
    "Name",
    "Subject",
    "Copyright",
    "Description",
    "Album",
    "Date",
    "Track",
    "Genre",
    "Encoder",
    "SrcMedia",
    "WWW",
    "Mail",
    "Rating",
    "Comments",
    "Mime"
};

int demux_info_add(demuxer_t *demuxer, unsigned opt, const char *param)
{
    if(!opt || opt > INFOT_MAX)
    {
	MSG_WARN("Unknown info type %u\n",opt);
	return 0;
    }
    opt--;
    if(((demuxer_info_t *)demuxer->info)->id[opt])
    {
	MSG_V( "Demuxer info '%s' already present as '%s'!\n",info_names[opt],((demuxer_info_t *)demuxer->info)->id[opt]);
	free(((demuxer_info_t *)demuxer->info)->id[opt]);
    }
    ((demuxer_info_t *)demuxer->info)->id[opt]=nls_recode2screen_cp(sub_cp,param,strlen(param));
    return 1;
}

int demux_info_print(demuxer_t *demuxer,const char *filename)
{
    unsigned i;
    MSG_HINT(" CLIP INFO (%s):\n",filename);
    for(i=0;i<INFOT_MAX;i++)
	if(((demuxer_info_t *)demuxer->info)->id[i])
	    MSG_HINT("   %s: %s\n",info_names[i],((demuxer_info_t *)demuxer->info)->id[i]);
    return 0;
}

void demux_info_free(demuxer_t* demuxer)
{
    unsigned i;
    if(demuxer->info) 
    {
	for(i=0;i<INFOT_MAX;i++)
	    if(((demuxer_info_t *)demuxer->info)->id[i])
		free(((demuxer_info_t *)demuxer->info)->id[i]);
	free(demuxer->info);
    }
}

const char* demux_info_get(demuxer_t *demuxer, unsigned opt) {
    if(!opt || opt > INFOT_MAX) return NULL;
    return ((demuxer_info_t *)demuxer->info)->id[opt-1];
}

/******************* Options stuff **********************/

static const config_t demuxer_opts[] = {
  { "audiofile", &audio_stream, CONF_TYPE_STRING, 0, 0, 0, NULL },
  { "subfile", &sub_stream, CONF_TYPE_STRING, 0, 0, 0, NULL },
  { "demuxer", &demuxer_type, CONF_TYPE_INT, CONF_RANGE, 1, DEMUXER_TYPE_MAX, NULL },
  { "audio-demuxer", &audio_demuxer_type, CONF_TYPE_INT, CONF_RANGE, 1, DEMUXER_TYPE_MAX, NULL },
  { "sub-demuxer", &sub_demuxer_type, CONF_TYPE_INT, CONF_RANGE, 1, DEMUXER_TYPE_MAX, NULL },
  { NULL, NULL, 0, 0, 0, 0, NULL}
};

void demuxer_register_options(m_config_t* cfg) {
  m_config_register_options(cfg,demuxer_opts);
}

static int demux_control(demuxer_t *demuxer, int cmd, void *arg) {

    if(demuxer->driver)
	return demuxer->driver->control(demuxer,cmd,arg);
    return DEMUX_UNKNOWN;
}

int demuxer_switch_audio(demuxer_t *demuxer, int id)
{
    if(id>MAX_A_STREAMS) id=0;
    if (demux_control(demuxer, DEMUX_CMD_SWITCH_AUDIO, &id) == DEMUX_UNKNOWN)
	id = demuxer->audio->id;
    return id;
}

int demuxer_switch_video(demuxer_t *demuxer, int id)
{
    if(id>MAX_V_STREAMS) id=0;
    if (demux_control(demuxer, DEMUX_CMD_SWITCH_VIDEO, &id) == DEMUX_UNKNOWN)
	id = demuxer->audio->id;
    return id;
}

int demuxer_switch_subtitle(demuxer_t *demuxer, int id)
{
    if(id>MAX_S_STREAMS) id=0;
    if (demux_control(demuxer, DEMUX_CMD_SWITCH_SUBS, &id) == DEMUX_UNKNOWN)
	id = demuxer->audio->id;
    return id;
}
