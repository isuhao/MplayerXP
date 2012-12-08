#include "mp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;
/*  ASF file parser for DEMUXER v0.3  by A'rpi/ESP-team and Nickols_K */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "help_mp.h"
#include "osdep/bswap.h"

#include "libmpstream/stream.h"
#include "asf.h"
#include "demuxer.h"
#include "demuxer_internal.h"
#include "stheader.h"
#include "libmpcodecs/dec_audio.h"
#include "aviprint.h"
#include "demux_msg.h"

/*
 * Load 16/32-bit values in little endian byte order
 * from an unaligned address
 */
inline unsigned int LOAD_LE32(const unsigned int* p) { return le2me_32(*p); }
inline unsigned short LOAD_LE16(const unsigned short* p) { return le2me_16(*p);}

inline unsigned int ASF_LOAD_GUID_PREFIX(const uint8_t*guid) { return LOAD_LE32(reinterpret_cast<const unsigned int*>(guid)); }

enum {
    ASF_GUID_PREFIX_audio_stream	=0xF8699E40,
    ASF_GUID_PREFIX_video_stream	=0xBC19EFC0,
    ASF_GUID_PREFIX_video_conceal_none	=0x20FB5700,
    ASF_GUID_PREFIX_audio_conceal_none	=0x49f1a440,
    ASF_GUID_PREFIX_audio_conceal_interleave=0xbfc3cd50,
    ASF_GUID_PREFIX_header		=0x75B22630,
    ASF_GUID_PREFIX_data_chunk		=0x75b22636,
    ASF_GUID_PREFIX_index_chunk		=0x33000890,
    ASF_GUID_PREFIX_stream_header	=0xB7DC0791,
    ASF_GUID_PREFIX_header_2_0		=0xD6E229D1,
    ASF_GUID_PREFIX_file_header		=0x8CABDCA1,
    ASF_GUID_PREFIX_content_desc	=0x75b22633,
    ASF_GUID_PREFIX_stream_group	=0x7bf875ce
};

struct asf_priv_t : public Opaque
{
    public:
	asf_priv_t() {}
	virtual ~asf_priv_t() {}

	ASF_header_t asfh;
	ASF_obj_header_t objh;
	ASF_file_header_t fileh;
	ASF_stream_header_t streamh;
	ASF_content_description_t contenth;
	int asf_scrambling_h;
	int asf_scrambling_w;
	int asf_scrambling_b;
	int asf_packetsize;
	double asf_packetrate;
	int asf_movielength;
};

// the variable string is modify in this function
void pack_asf_string(char* string, int length) {
  int i,j;
  if( string==NULL ) return;
  for( i=0, j=0; i<length && string[i]!='\0'; i+=2, j++) {
    string[j]=string[i];
  }
  string[j]='\0';
}

// the variable string is modify in this function
void print_asf_string(const char* name, char* string, int length) {
  pack_asf_string(string, length);
  MSG_V("%s%s\n", name, string);
}

static const char* asf_chunk_type(const uint8_t* guid) {
  static char tmp[60];
  char *p;
  int i;

  switch(ASF_LOAD_GUID_PREFIX(guid)){
    case ASF_GUID_PREFIX_audio_stream:
      return "guid_audio_stream";
    case ASF_GUID_PREFIX_video_stream:
      return "guid_video_stream";
    case ASF_GUID_PREFIX_video_conceal_none:
      return "guid_video_conceal_none";
    case ASF_GUID_PREFIX_audio_conceal_none:
      return "guid_audio_conceal_none";
    case ASF_GUID_PREFIX_audio_conceal_interleave:
      return "guid_audio_conceal_interleave";
    case ASF_GUID_PREFIX_header:
      return "guid_header";
    case ASF_GUID_PREFIX_data_chunk:
      return "guid_data_chunk";
    case ASF_GUID_PREFIX_index_chunk:
      return "guid_index_chunk";
    case ASF_GUID_PREFIX_stream_header:
      return "guid_stream_header";
    case ASF_GUID_PREFIX_header_2_0:
      return "guid_header_2_0";
    case ASF_GUID_PREFIX_file_header:
      return "guid_file_header";
    case ASF_GUID_PREFIX_content_desc:
      return "guid_content_desc";
    default:
      strcpy(tmp, "unknown guid ");
      p = tmp + strlen(tmp);
      for (i = 0; i < 16; i++) {
	if ((1 << i) & ((1<<4) | (1<<6) | (1<<8))) *p++ = '-';
	sprintf(p, "%02x", guid[i]);
	p += 2;
      }
      return tmp;
  }
}

static MPXP_Rc asf_probe(Demuxer *demuxer){
  const unsigned char asfhdrguid[16]= {0x30,0x26,0xB2,0x75,0x8E,0x66,0xCF,0x11,0xA6,0xD9,0x00,0xAA,0x00,0x62,0xCE,0x6C};
  const unsigned char asf2hdrguid[16]={0xD1,0x29,0xE2,0xD6,0xDA,0x35,0xD1,0x11,0x90,0x34,0x00,0xA0,0xC9,0x03,0x49,0xBE};
  asf_priv_t* apriv;
  demuxer->priv=apriv=new(zeromem) asf_priv_t;
  apriv->asf_scrambling_h=1;
  apriv->asf_scrambling_w=1;
  apriv->asf_scrambling_b=1;
  apriv->asf_packetsize=0;
  apriv->asf_packetrate=0;
  apriv->asf_movielength=0;

  demuxer->stream->read((char*)&apriv->asfh,sizeof(ASF_header_t)); // header obj
  le2me_ASF_header_t(&apriv->asfh);	// swap to machine endian
  if(memcmp(asf2hdrguid,apriv->asfh.objh.guid,16)==0){
    MSG_ERR("ASF_check: found ASF v2 guid!\nCurrently is not supported - please report!\n");
    delete demuxer->priv;
    return MPXP_False; // not ASF guid
  }
  if(memcmp(asfhdrguid,apriv->asfh.objh.guid,16)){
    MSG_V("ASF_check: not ASF guid!\n");
    delete demuxer->priv;
    return MPXP_False; // not ASF guid
  }
  if(apriv->asfh.cno>256){
    MSG_V("ASF_check: invalid subchunks_no %d\n",(int) apriv->asfh.cno);
    delete demuxer->priv;
    return MPXP_False; // invalid header???
  }
  demuxer->file_format=Demuxer::Type_ASF;
  return MPXP_Ok;
}

static Opaque* asf_open(Demuxer *demuxer){
  static unsigned char buffer[2048];
  uint32_t* streams = NULL;
  int audio_streams=0;
  int video_streams=0;
  uint16_t stream_count=0;
  int best_video = -1;
  int best_audio = -1;
  asf_priv_t *apriv=static_cast<asf_priv_t*>(demuxer->priv);

while(!demuxer->stream->eof()){
  int pos,endpos;
  pos=demuxer->stream->tell();
  demuxer->stream->read((char*) &apriv->objh,sizeof(ASF_obj_header_t));
  le2me_ASF_obj_header_t(&apriv->objh);
  if(demuxer->stream->eof()) break; // EOF
  endpos=pos+apriv->objh.size;
  switch(ASF_LOAD_GUID_PREFIX(apriv->objh.guid)){
    case ASF_GUID_PREFIX_stream_header:
      demuxer->stream->read((char*) &apriv->streamh,sizeof(ASF_stream_header_t));
      le2me_ASF_stream_header_t(&apriv->streamh);
	MSG_V("stream type: %s\n"
	      "stream concealment: %s\n"
	      "type: %d bytes,  stream: %d bytes  ID: %d\n"
	      "unk1: %lX  unk2: %X\n"
	      "FILEPOS=0x%X\n"
	      ,asf_chunk_type(apriv->streamh.type)
	      ,asf_chunk_type(apriv->streamh.concealment)
	      ,(int)apriv->streamh.type_size,(int)apriv->streamh.stream_size,(int)apriv->streamh.stream_no
	      ,(unsigned long)apriv->streamh.unk1,(unsigned int)apriv->streamh.unk2
	      ,demuxer->stream->tell());
      // type-specific data:
      demuxer->stream->read((char*) buffer,apriv->streamh.type_size);
      switch(ASF_LOAD_GUID_PREFIX(apriv->streamh.type)){
      case ASF_GUID_PREFIX_audio_stream: {
	sh_audio_t* sh_audio=demuxer->new_sh_audio(apriv->streamh.stream_no & 0x7F);
	++audio_streams;
	sh_audio->wf=(WAVEFORMATEX*)mp_calloc((apriv->streamh.type_size<sizeof(WAVEFORMATEX))?sizeof(WAVEFORMATEX):apriv->streamh.type_size,1);
	memcpy(sh_audio->wf,buffer,apriv->streamh.type_size);
	le2me_WAVEFORMATEX(sh_audio->wf);
	if(mp_conf.verbose>=1) print_wave_header(sh_audio->wf,apriv->streamh.type_size);
	if(ASF_LOAD_GUID_PREFIX(apriv->streamh.concealment)==ASF_GUID_PREFIX_audio_conceal_interleave){
	  demuxer->stream->read((char*) buffer,apriv->streamh.stream_size);
	  apriv->asf_scrambling_h=buffer[0];
	  apriv->asf_scrambling_w=(buffer[2]<<8)|buffer[1];
	  apriv->asf_scrambling_b=(buffer[4]<<8)|buffer[3];
	  apriv->asf_scrambling_w/=apriv->asf_scrambling_b;
	} else {
	  apriv->asf_scrambling_b=apriv->asf_scrambling_h=apriv->asf_scrambling_w=1;
	}
	MSG_V("ASF: audio scrambling: %d x %d x %d\n",apriv->asf_scrambling_h,apriv->asf_scrambling_w,apriv->asf_scrambling_b);
	break;
	}
      case ASF_GUID_PREFIX_video_stream: {
	sh_video_t* sh_video=demuxer->new_sh_video(apriv->streamh.stream_no & 0x7F);
	unsigned int len=apriv->streamh.type_size-(4+4+1+2);
	++video_streams;
	sh_video->bih=(BITMAPINFOHEADER*)mp_mallocz(len<sizeof(BITMAPINFOHEADER)?sizeof(BITMAPINFOHEADER):len);
	memcpy(sh_video->bih,&buffer[4+4+1+2],len);
	le2me_BITMAPINFOHEADER(sh_video->bih);
	if(mp_conf.verbose>=1) print_video_header(sh_video->bih,len);
	break;
	}
      }
      // stream-specific data:
      break;
//  case ASF_GUID_PREFIX_header_2_0: return "guid_header_2_0";
    case ASF_GUID_PREFIX_file_header: // guid_file_header
      demuxer->stream->read((char*) &apriv->fileh,sizeof(ASF_file_header_t));
      le2me_ASF_file_header_t(&apriv->fileh);
      MSG_V("ASF: size: %llu play_duration: %llu send_duration: %llu packets: %d\nflags: %d  min_packet_size: %d  max_packet_size: %d  max_bitrate: %d  preroll: %d\n",
      (uint64_t)apriv->fileh.file_size,(uint64_t)apriv->fileh.play_duration,(uint64_t)apriv->fileh.send_duration,
      (int)apriv->fileh.num_packets,(int)apriv->fileh.flags,(int)apriv->fileh.min_packet_size,(int)apriv->fileh.max_packet_size,
      (int)apriv->fileh.max_bitrate,(int)apriv->fileh.preroll);
      apriv->asf_packetsize=apriv->fileh.max_packet_size;
      apriv->asf_packetrate=apriv->fileh.max_bitrate/8.0/(double)apriv->asf_packetsize;
      demuxer->movi_length=apriv->asf_movielength=apriv->fileh.send_duration/10000000LL;
      break;
    case ASF_GUID_PREFIX_data_chunk: // guid_data_chunk
      demuxer->movi_start=demuxer->stream->tell()+26;
      demuxer->movi_end=endpos;
      MSG_V("Found movie at 0x%X - 0x%X\n",(int)demuxer->movi_start,(int)demuxer->movi_end);
      break;

//  case ASF_GUID_PREFIX_index_chunk: return "guid_index_chunk";

    case ASF_GUID_PREFIX_content_desc: // Content description
    {
	char *string=NULL;
	demuxer->stream->read((char*) &apriv->contenth,sizeof(ASF_content_description_t));
	le2me_ASF_content_description_t(&apriv->contenth);
	MSG_V("\n");
	// extract the title
	if( apriv->contenth.title_size!=0 ) {
	    string=new char[apriv->contenth.title_size];
	    demuxer->stream->read( string, apriv->contenth.title_size);
	    pack_asf_string(string, apriv->contenth.title_size);
	    demuxer->info().add(INFOT_NAME, string);
	    delete string;
	}
	// extract the author
	if( apriv->contenth.author_size!=0 ) {
	    string=new char [apriv->contenth.author_size];
	    demuxer->stream->read( string, apriv->contenth.author_size);
	    pack_asf_string(string, apriv->contenth.author_size);
	    demuxer->info().add(INFOT_AUTHOR, string);
	    delete string;
	}
	// extract the copyright
	if( apriv->contenth.copyright_size!=0 ) {
	    string=new char [apriv->contenth.copyright_size];
	    demuxer->stream->read( string, apriv->contenth.copyright_size);
	    pack_asf_string(string, apriv->contenth.copyright_size);
	    demuxer->info().add(INFOT_COPYRIGHT, string);
	    delete string;
	}
	// extract the comment
	if( apriv->contenth.comment_size!=0 ) {
	    string=new char [apriv->contenth.comment_size];
	    demuxer->stream->read( string, apriv->contenth.comment_size);
	    pack_asf_string(string, apriv->contenth.comment_size);
	    demuxer->info().add(INFOT_COMMENTS, string);
	    delete string;
	}
	// extract the rating
	if( apriv->contenth.rating_size!=0 ) {
	    string=new char [apriv->contenth.rating_size];
	    demuxer->stream->read( string, apriv->contenth.rating_size);
	    pack_asf_string(string, apriv->contenth.comment_size);
	    demuxer->info().add(INFOT_RATING, string);
	    delete string;
	}
	MSG_V("\n");
      break;
    }
    case ASF_GUID_PREFIX_stream_group: {
	uint16_t stream_id, i;
	uint32_t max_bitrate;
	char *object=NULL, *ptr=NULL;
	MSG_V("============ ASF Stream group == START ===\n");
	MSG_V(" object size = %d\n", (int)apriv->objh.size);
	object = new char[apriv->objh.size];
	if( object==NULL ) {
	  MSG_ERR("Memory allocation failed\n");
	  delete demuxer->priv;
	  return NULL;
	}
	demuxer->stream->read(object, apriv->objh.size );
	// FIXME: We need some endian handling below...
	ptr = object;
	stream_count = le2me_16(*(uint16_t*)ptr);
	ptr += sizeof(uint16_t);
	if(stream_count > 0)
	      streams = (uint32_t*)mp_malloc(2*stream_count*sizeof(uint32_t));
	MSG_V(" stream count=[0x%x][%u]\n", stream_count, stream_count );
	for( i=0 ; i<stream_count && ptr<((char*)object+apriv->objh.size) ; i++ ) {
	  stream_id = le2me_16(*(uint16_t*)ptr);
	  ptr += sizeof(uint16_t);
	  memcpy(&max_bitrate, ptr, sizeof(uint32_t));// workaround unaligment bug on sparc
	  max_bitrate = le2me_32(max_bitrate);
	  ptr += sizeof(uint32_t);
	  MSG_V("   stream id=[0x%x][%u]\n", stream_id, stream_id );
	  MSG_V("   max bitrate=[0x%x][%u]\n", max_bitrate, max_bitrate );
	  streams[2*i] = stream_id;
	  streams[2*i+1] = max_bitrate;
	}
	MSG_V("============ ASF Stream group == END ===\n");
	delete object ;
      break;
    }
  } // switch GUID

  if(ASF_LOAD_GUID_PREFIX(apriv->objh.guid)==ASF_GUID_PREFIX_data_chunk) break; // movi chunk

  if(!demuxer->stream->seek(endpos)) break;
} // while EOF

if(streams) {
  uint32_t vr = 0, ar = 0,i;
  for(i = 0; i < stream_count; i++) {
    uint32_t id = streams[2*i];
    uint32_t rate = streams[2*i+1];
    if(demuxer->get_sh_video(id) && rate > vr) {
      vr = rate;
      best_video = id;
    } else if(demuxer->get_sh_audio(id) && rate > ar) {
      ar = rate;
      best_audio = id;
    }
  }
  delete streams;
}

MSG_V("ASF: %d audio and %d video streams found\n",audio_streams,video_streams);
if(!audio_streams) demuxer->audio->id=-2;  // nosound
else if(best_audio > 0 && demuxer->audio->id == -1) demuxer->audio->id=best_audio;
if(!video_streams){
    if(!audio_streams){
	MSG_ERR("ASF: no audio or video headers found - broken file?\n");
	delete demuxer->priv;
	return NULL;
    }
    demuxer->video->id=-2; // audio-only
} else if (best_video > 0 && demuxer->video->id == -1) demuxer->video->id = best_video;

{
  Demuxer_Stream *d_video=demuxer->video;
  Demuxer_Stream *d_audio=demuxer->audio;
  demuxer->stream->reset();
  demuxer->stream->seek(demuxer->movi_start);
  if(d_video->id != -2) {
    if(!d_video->fill_buffer()){
      MSG_WARN("ASF: " MSGTR_MissingVideoStream);
      demuxer->video->sh=NULL;
    } else {
	sh_video_t *sh_video=reinterpret_cast<sh_video_t*>(demuxer->video->sh);
	sh_video->ds=d_video;
	sh_video->fps=1000.0f; // 1ms
	MSG_V("VIDEO:  [%.4s]  %ldx%ld  %dbpp\n",
	(char *)&sh_video->bih->biCompression,
	sh_video->bih->biWidth,
	sh_video->bih->biHeight,
	sh_video->bih->biBitCount);
    }
  }
  if(d_audio->id!=-2){
    MSG_V("ASF: Searching for audio stream (id:%d)\n",d_audio->id);
    if(!d_audio->fill_buffer()){
	MSG_WARN("ASF: " MSGTR_MissingAudioStream);
	demuxer->audio->sh=NULL;
    } else {
	sh_audio_t *sh_audio = reinterpret_cast<sh_audio_t*>(demuxer->audio->sh);
	sh_audio->ds=d_audio;
	sh_audio->wtag=sh_audio->wf->wFormatTag;
    }
  }
}
    check_pin("demuxer",demuxer->pin,DEMUX_PIN);
    return apriv;
}

// based on asf file-wtag doc by Eugene [http://divx.euro.ru]

static void asf_descrambling(asf_priv_t *apriv, unsigned char *src,int len){
  unsigned char *dst=new unsigned char[len];
  unsigned char *s2=src;
  int i=0,x,y;
  while(len-i>=apriv->asf_scrambling_h*apriv->asf_scrambling_w*apriv->asf_scrambling_b){
	for(x=0;x<apriv->asf_scrambling_w;x++)
	  for(y=0;y<apriv->asf_scrambling_h;y++){
	    memcpy(dst+i,s2+(y*apriv->asf_scrambling_w+x)*apriv->asf_scrambling_b,apriv->asf_scrambling_b);
		i+=apriv->asf_scrambling_b;
	  }
	s2+=apriv->asf_scrambling_h*apriv->asf_scrambling_w*apriv->asf_scrambling_b;
  }
  memcpy(src,dst,i);
  delete dst;
}


static int demux_asf_read_packet(Demuxer *demux,off_t dataoff,int len,int id,int seq,unsigned long time,unsigned short dur,int offs,int keyframe){
  Demuxer_Stream *ds=NULL;
  asf_priv_t *apriv=static_cast<asf_priv_t*>(demux->priv);

  MSG_DBG3("demux_asf.read_packet: id=%d seq=%d len=%d time=%u dur=%u offs=%i key=%i\n",id,seq,len,time,dur,offs,keyframe);

  if(demux->video->id==-1)
    if(demux->get_sh_video(id))
	demux->video->id=id;

  if(demux->audio->id==-1)
    if(demux->get_sh_audio(id))
	demux->audio->id=id;

  if(id==demux->audio->id){
      // audio
      ds=demux->audio;
      if(!ds->sh){
	ds->sh=demux->get_sh_audio(id);
	MSG_V("Auto-selected ASF audio ID = %d\n",ds->id);
      }
  } else
  if(id==demux->video->id){
      // video
      ds=demux->video;
      if(!ds->sh){
	ds->sh=demux->get_sh_video(id);
	MSG_V("Auto-selected ASF video ID = %d\n",ds->id);
      }
  }

  if(ds){
    if(ds->asf_packet){
      if(ds->asf_seq!=seq){
	// closed segment, finalize packet:
		if(ds==demux->audio)
		  if(apriv->asf_scrambling_h>1 && apriv->asf_scrambling_w>1)
		    asf_descrambling(apriv,ds->asf_packet->buffer(),ds->asf_packet->length());
	ds->add_packet(ds->asf_packet);
	ds->asf_packet=NULL;
      } else {
	// append data to it!
	Demuxer_Packet* dp=ds->asf_packet;
	if(dp->length()!=unsigned(offs) && offs!=-1) MSG_V("warning! fragment.len=%d BUT next fragment offset=%d  \n",dp->length(),offs);
	dp->resize(dp->length()+len);
	demux->stream->seek(dataoff);
	demux->stream->read(dp->buffer()+dp->length(),len);
	MSG_DBG3("data appended! %d+%d\n",dp->length(),len);
	// we are ready now.
	return 1;
      }
    }
    // create new packet:
    {
      if(offs>0){
	MSG_V("warning!  broken fragment or incomplete seeking, %d bytes missing  \n",offs);
	return 0;
      }
      Demuxer_Packet* dp=new(zeromem) Demuxer_Packet(len);
      demux->stream->seek(dataoff);
      len=demux->stream->read(dp->buffer(),len);
      dp->resize(len);
      dp->pts=time*0.001f;
      dp->flags=keyframe?DP_KEYFRAME:DP_NONKEYFRAME;
      dp->pos=demux->filepos;
      ds->asf_packet=dp;
      ds->asf_seq=seq;
      MSG_DBG2("ASF: reading %s PTS %u %f %i\n",ds==demux->audio?"audio":"video",time,dp->pts,keyframe);
      // we are ready now.
      return 1;
    }
  }

  return 0;
}

// return value:
//     0 = EOF or no stream found
//     1 = successfully read a packet
static int asf_demux(Demuxer *demux,Demuxer_Stream *ds){
Stream *stream=demux->stream;
asf_priv_t *apriv=static_cast<asf_priv_t*>(demux->priv);
int done=0;
while(!done)
{
    demux->filepos=stream->tell();
    // Broadcast stream have movi_start==movi_end
    // Better test ?
    if((demux->movi_start != demux->movi_end) && (demux->filepos>=demux->movi_end)){
	stream->eof(1);
	return 0;
    }

    if(stream->eof()) return 0; // EOF
    {	    unsigned char ecc_flags;
	    off_t p_start,p_end;
	    unsigned char flags;
	    unsigned char segtype;
	    int padding;
	    int plen;
	    int sequence;
	    unsigned long time=0;
	    unsigned short duration=0;
	    int segs=1;
	    unsigned char segsizetype=0x80;
	    int seg=-1;

	    p_start=stream->tell();
	    p_end = p_start+apriv->asf_packetsize; /* FIXME: parser is not ready for variable packet length */
	    ecc_flags=stream->read_char(); /* read v82 header */
	    MSG_DBG2("ecc=%02X ecc_flags=%u\n",ecc_flags,ecc_flags&0x0F);
	    stream->skip(ecc_flags&15);
	    flags=stream->read_char();
	    segtype=stream->read_char();

	    /* Read packet size (plen): */
	    switch((flags>>5)&3){
	    case 3: plen=stream->read_dword_le();break;	// dword
	    case 2: plen=stream->read_word_le();break;	// word
	    case 1: plen=stream->read_char();break;	// byte
	    default: plen=0; /* not present */
//		MSG_V("Invalid plen type! assuming plen=0 (flags=%02X)\n",flags);
	    }

	    /* Read sequence: */
	    switch((flags>>1)&3){
	    case 3: sequence=stream->read_dword_le();break;// dword
	    case 2: sequence=stream->read_word_le();break;	// word
	    case 1: sequence=stream->read_char();break;	// byte
	    default: sequence=0;
	    }

	    /* Read padding size (padding): */
	    switch((flags>>3)&3){
	    case 3: padding=stream->read_dword_le();break;	// dword
	    case 2: padding=stream->read_word_le();break;	// word
	    case 1: padding=stream->read_char();break;	// byte
	    default: padding=0;
	    }

	    if(((flags>>5)&3)!=0){
	      // Explicit (absoulte) packet size
	      MSG_DBG2("Explicit packet size specified: %d\n",plen);
	      if(plen>apriv->asf_packetsize) MSG_V("Warning! plen>packetsize! (%d>%d)\n",plen,apriv->asf_packetsize);
	    } else {
	      // Padding (relative) size
	      plen=apriv->asf_packetsize-padding;
	    }

	    // Read time & duration:
	    time = stream->read_dword_le();
	    duration = stream->read_word_le();

	    // Read payload flags:
	    if(flags&1){
		unsigned char sf=stream->read_char();
		// multiple sub-packets
		segsizetype=sf>>6;
		segs=sf & 0x3F;
	    }
	    MSG_DBG2(":  flag=%02X  segs=%d  seq=%d  plen=%d  pad=%d  time=%ld  dur=%d %08X\n",
		flags,segs,sequence,plen,padding,time,duration,demux->filepos);

	    for(seg=0;seg<segs;seg++){
	      //ASF_segmhdr_t* sh;
	      unsigned char st;
	      unsigned char streamno;
	      unsigned int seq;
	      unsigned int x;	// offset or timestamp
	      unsigned int rlen;
	      //
	      int len;
	      unsigned int time2=0;
	      int keyframe=0;

	      if(stream->tell()>=p_end) MSG_V("Warning! invalid packet 1, sig11 coming soon...\n");

	      st=stream->read_char();
	      streamno=st&0x7F;
	      if(st&0x80) keyframe=1;

	      // Read media object number (seq):
	      switch((segtype>>4)&3){
	      case 3: seq=stream->read_dword_le();break;// dword
	      case 2: seq=stream->read_word_le();break;	// word
	      case 1: seq=stream->read_char();break;	// byte
	      default: seq=0;
	      }

	      // Read offset or timestamp:
	      switch((segtype>>2)&3){
	      case 3: x=stream->read_dword_le();break;	// dword
	      case 2: x=stream->read_word_le();break;	// word
	      case 1: x=stream->read_char();break;	// byte
	      default: x=0;
	      }

	      // Read replic.data len:
	      switch((segtype)&3){
	      case 3: rlen=stream->read_dword_le();break;	// dword
	      case 2: rlen=stream->read_word_le();break;	// word
	      case 1: rlen=stream->read_char();break;	// byte
	      default: rlen=0;
	      }

	      switch(rlen){
	      case 0x01: // 1 = special, means grouping
		stream->skip(1); // skip PTS delta
		break;
	      default:
		if(rlen>=8){
		    stream->skip(4);// skip object size
		    time2=stream->read_dword_le(); // read PTS
		    stream->skip(rlen-8);
		} else {
		    MSG_V("unknown segment type (rlen): 0x%02X  \n",rlen);
		    time2=0; // unknown
		    stream->skip(rlen);
		}
	      }

	      if(flags&1){
		// multiple segments
		switch(segsizetype){
		  case 3: len=stream->read_dword_le();break;	// dword
		  case 2: len=stream->read_word_le();break;	// word
		  case 1: len=stream->read_char();break;		// byte
		  default: len=plen-(stream->tell()-p_start); // ???
		}
	      } else {
		// single segment
		len=plen-(stream->tell()-p_start);
	      }
	      if(len<0 || (stream->tell()+len)>p_end){
		MSG_V("ASF_parser: warning! segment len=%d\n",len);
	      }
	      MSG_DBG2("  seg #%d: streamno=%d  seq=%d  type=%02X  len=%d\n",seg,streamno,seq,rlen,len);

	      switch(rlen){
	      case 0x01:
		// GROUPING:
		while(len>0){
		  int len2=stream->read_char();
		  if(len2<0) len2=0;
		  done=demux_asf_read_packet(demux,stream->tell(),len2,streamno,seq,x,duration,-1,keyframe);
		  len-=len2+1;
		  ++seq;
		}
		if(len!=0){
		  MSG_V("ASF_parser: warning! groups total != len\n");
		}
		break;
	      default:
		// NO GROUPING:
		if(len<0) len=0;
		done=demux_asf_read_packet(demux,stream->tell(),len,streamno,seq,time2,duration,x,keyframe);
		break;
	      }

	    } // for segs
	    stream->seek(p_end);
	    if(done) return 1; // success
    }
}
return 0;
}


static void asf_seek(Demuxer *demuxer,const seek_args_t* seeka){
    Demuxer_Stream *d_audio=demuxer->audio;
    Demuxer_Stream *d_video=demuxer->video;
    asf_priv_t *apriv=static_cast<asf_priv_t*>(demuxer->priv);
    sh_audio_t *sh_audio=reinterpret_cast<sh_audio_t*>(d_audio->sh);

  /*================= seek in ASF ==========================*/
    float p_rate=apriv->asf_packetrate; // packets / sec
    off_t rel_seek_packs=(seeka->flags&DEMUX_SEEK_PERCENTS)?	 // FIXME: int may be enough?
	(seeka->secs*(demuxer->movi_end-demuxer->movi_start)/apriv->asf_packetsize):
	(seeka->secs*p_rate);
    off_t rel_seek_bytes=rel_seek_packs*apriv->asf_packetsize;
    off_t newpos;
    newpos=((seeka->flags&DEMUX_SEEK_SET)?demuxer->movi_start:demuxer->filepos)+rel_seek_bytes;
    if(newpos<0 || newpos<demuxer->movi_start) newpos=demuxer->movi_start;
    demuxer->stream->seek(newpos);

    /*!!! FIXME: this loop is too long sometime !!!*/
    while(d_video->fill_buffer())
	if(d_video->flags&1) break; /* found a keyframe! */

    if(sh_audio && !d_audio->eof){
      d_audio->free_packs_until_pts(d_video->pts);
      d_audio->fill_buffer();
    }
}

static void asf_close(Demuxer *demuxer)
{
    delete demuxer->priv;
}

static MPXP_Rc asf_control(const Demuxer *demuxer,int cmd,any_t*args)
{
    UNUSED(demuxer);
    UNUSED(cmd);
    UNUSED(args);
    return MPXP_Unknown;
}

extern const demuxer_driver_t demux_asf =
{
    "asf",
    "ASF - Advanced stream format v1 parser",
    ".asf",
    NULL,
    asf_probe,
    asf_open,
    asf_demux,
    asf_seek,
    asf_close,
    asf_control
};
