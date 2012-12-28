#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;
/*  AVI file parser for DEMUXER v2.9  by A'rpi/ESP-team */

#include <algorithm>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "mpxp_help.h"

#include "libmpstream2/stream.h"
#include "demuxer.h"
#include "demuxer_internal.h"
#include "stheader.h"

#include "osdep/bswap.h"
#include "aviheader.h"
#include "libmpcodecs/dec_audio.h"
#include "libmpconf/cfgparser.h"
#include "aviprint.h"
#include "demux_msg.h"

struct avi_priv_t : public Opaque {
    public:
	avi_priv_t() {}
	virtual ~avi_priv_t();

	// index stuff:
	AVIINDEXENTRY* idx;
	int idx_size;
	off_t idx_pos;
	off_t idx_pos_a;
	off_t idx_pos_v;
	off_t idx_offset;  // ennyit kell hozzaadni az index offset ertekekhez
	// bps-based PTS stuff:
	int video_pack_no;
	int audio_block_size;
	off_t audio_block_no;
	// interleaved PTS stuff:
	int skip_video_frames;
	int audio_streams;
	float avi_audio_pts;
	float avi_video_pts;
	float pts_correction;
	unsigned int pts_corr_bytes;
	unsigned char pts_corrected;
	unsigned char pts_has_video;
	unsigned int numberofframes;
	avisuperindex_chunk *suidx;
	unsigned int suidx_size;
	int nini;
	int is_odml;
};

avi_priv_t::~avi_priv_t() {
    if(idx_size > 0) delete idx;
}

#define MAX_PACKS 4096
inline uint64_t avi_idx_offset(AVIINDEXENTRY* x) { return  ((x->dwFlags&0xffff0000)<<16)+x->dwChunkOffset; }

static int odml_get_vstream_id(int id, unsigned char res[])
{
    unsigned char *p = (unsigned char *)&id;
    id = le2me_32(id);

    if (p[2] == 'd') {
	if (res) {
	    res[0] = p[0];
	    res[1] = p[1];
	}
	return 1;
    }
    return 0;
}

static int avi_idx_cmp(const any_t*elem1,const any_t*elem2) {
  register off_t a = avi_idx_offset((AVIINDEXENTRY *)elem1);
  register off_t b = avi_idx_offset((AVIINDEXENTRY *)elem2);
  return (a > b) - (b > a);
}

int avi_stream_id(unsigned int id){
  unsigned char *p=(unsigned char *)&id;
  unsigned char a,b;
#if WORDS_BIGENDIAN
  a=p[3]-'0'; b=p[2]-'0';
#else
  a=p[0]-'0'; b=p[1]-'0';
#endif
  if(a>9 || b>9) return 100; // invalid ID
  return a*10+b;
}

/*
According to :
"MPEG Extension to AVI File Format "Editable MPEG FileFormat"
Draft Version 1.1 of 5/13/94"
*/
static float avi_aspects[]=
{
  1.0000, 0.6735, 0.7031, 0.7615, 0.8055, 0.8437, 0.8935,
  0.9375, 0.9815, 1.0255, 1.0695, 1.1250, 1.1575, 1.2015
};

static float get_avi_aspect(unsigned char id)
{
    if(id>0&&id<15)
    {
	return avi_aspects[id-1];
    }
    else return 1.0;
}

extern const demuxer_driver_t demux_avi;

static void read_avi_header(Demuxer *demuxer,int index_mode){
MainAVIHeader avih;
sh_audio_t *sh_audio=NULL;
sh_video_t *sh_video=NULL;
int stream_id=-1;
int idxfix_videostream=0;
int idxfix_divx=0;
avi_priv_t* priv=static_cast<avi_priv_t*>(demuxer->priv);
off_t list_end=0;

//---- AVI header:
priv->idx_size=0;
priv->audio_streams=0;
while(1){
  int id=demuxer->stream->read_dword_le();
  unsigned chunksize,size2;
  static int last_fccType=0;
  const char* hdr=NULL;
  int infot;
  //
  if(demuxer->stream->eof()) break;
  //
  if(id==mmioFOURCC('L','I','S','T')){
    unsigned len=demuxer->stream->read_dword_le();   // list size
    id=demuxer->stream->read_dword_le();             // list type
    MSG_DBG2("LIST '%.4s'  len=%u\n",(char *) &id,len);
    if(len >= 4) {
	len -= 4;
	list_end=demuxer->stream->tell()+((len+1)&(~1));
    } else {
	MSG_WARN("** empty list?!\n");
	list_end = 0;
    }
    MSG_DBG2("list_end=0x%X\n",(int)list_end);
    if(id==listtypeAVIMOVIE){
      // found MOVI header
      if(!demuxer->movi_start) demuxer->movi_start=demuxer->stream->tell();
      demuxer->movi_end=demuxer->stream->tell()+len;
      MSG_V("Found movie at 0x%X - 0x%X\n",(int)demuxer->movi_start,(int)demuxer->movi_end);
      if(demuxer->stream->end_pos()>demuxer->movi_end) demuxer->movi_end=demuxer->stream->end_pos();
      if(index_mode==-2 || index_mode==2 || index_mode==0)
	break; // reading from non-seekable source (stdin) or forced index or no index forced
      if(list_end>0) demuxer->stream->seek(list_end); // skip movi
      list_end=0;
    }
    continue;
  }
  size2=demuxer->stream->read_dword_le();
  MSG_V("CHUNK '%.4s' len=%u\n",(char *) &id,size2);
  chunksize=(size2+1)&(~1);
  infot=-1;
  switch(id){

    // Indicates where the subject of the file is archived
    case mmioFOURCC('I','A','R','L'): infot=INFOT_WWW; hdr="archival location";break;
    // Lists the artist of the original subject of the file;
    // for example, "Michaelangelo."
    case mmioFOURCC('I','A','R','T'): infot=INFOT_AUTHOR; hdr="artist";break;
    // Lists the name of the person or organization that commissioned
    // the subject of the file; for example "Pope Julian II."
    case mmioFOURCC('I','C','M','S'): hdr="Commissioned";break;
    // Provides general comments about the file or the subject
    // of the file. If the comment is several sentences long, end each
    // sentence with a period. Do not include new-line characters.
    case mmioFOURCC('I','C','M','T'): infot=INFOT_COMMENTS; hdr="Comments";break;
    // Records the copyright information for the file; for example,
    // "Copyright Encyclopedia International 1991." If there are multiple
    // copyrights, separate them by semicolon followed by a space.
    case mmioFOURCC('I','C','O','P'): infot=INFOT_COPYRIGHT; hdr="Copyright";break;
    // Describes whether an image has been cropped and, if so, how it
    // was cropped; for example, "lower-right corner."
    case mmioFOURCC('I','C','R','D'): infot=INFOT_DATE; hdr="Creation Date";break;
    // Describes whether an image has been cropped and, if so, how it
    // was cropped; for example, "lower-right corner."
    case mmioFOURCC('I','C','R','P'): hdr="Cropped";break;
    // Specifies the size of the original subject of the file; for
    // example, "8.5 in h, 11 in w."
    case mmioFOURCC('I','D','I','M'): hdr="Dimensions";break;
    // Stores dots per inch setting of the digitizer used to
    // produce the file, such as "300."
    case mmioFOURCC('I','D','P','I'): hdr="Dots Per Inch";break;
    // Stores the of the engineer who worked on the file. If there are
    // multiple engineers, separate the names by a semicolon and a blank;
    // for example, "Smith, John; Adams, Joe."
    case mmioFOURCC('I','E','N','G'): hdr="Engineer";break;
    // Describes the original work, such as "landscape,", "portrait,"
    // "still liefe," etc.
    case mmioFOURCC('I','G','N','R'): infot=INFOT_GENRE; hdr="Genre";break;
    // Provides a list of keywords that refer to the file or subject of the
    // file. Separate multiple keywords with a semicolon and a blank;
    // for example, "Seattle, aerial view; scenery."
    case mmioFOURCC('I','K','E','Y'): hdr="Keywords";break;
    // ILGT - Describes the changes in the lightness settings on the digitizer
    // required to produce the file. Note that the format of this information
    // depends on the hardware used.
    case mmioFOURCC('I','L','G','T'): hdr="Lightness";break;
    // IMED - Decribes the original subject of the file, such as
    // "computer image," "drawing," "lithograph," and so on.
    case mmioFOURCC('I','M','E','D'): hdr="Medium";break;
    // INAM - Stores the title of the subject of the file, such as
    // "Seattle from Above."
    case mmioFOURCC('I','N','A','M'): infot=INFOT_NAME; hdr="Name";break;
    // IPLT - Specifies the number of colors requested when digitizing
    // an image, such as "256."
    case mmioFOURCC('I','P','L','T'): hdr="Palette Setting";break;
    // IPRD - Specifies the name of title the file was originally intended
    // for, such as "Encyclopedia of Pacific Northwest Geography."
    case mmioFOURCC('I','P','R','D'): infot=INFOT_ALBUM; hdr="Product";break;
    // ISBJ - Decsribes the contents of the file, such as
    // "Aerial view of Seattle."
    case mmioFOURCC('I','S','B','J'): infot=INFOT_SUBJECT; hdr="Subject";break;
    // ISFT - Identifies the name of the software packages used to create the
    // file, such as "Microsoft WaveEdit"
    case mmioFOURCC('I','S','F','T'): infot=INFOT_ENCODER; hdr="Software";break;
    // ISHP - Identifies the change in sharpness for the digitizer
    // required to produce the file (the format depends on the hardware used).
    case mmioFOURCC('I','S','H','P'): hdr="Sharpness";break;
    // ISRC - Identifies the name of the person or organization who
    // suplied the original subject of the file; for example, "Try Research."
    case mmioFOURCC('I','S','R','C'): hdr="Source";break;
    // ISRF - Identifies the original form of the material that was digitized,
    // such as "slide," "paper," "map," and so on. This is not necessarily
    // the same as IMED
    case mmioFOURCC('I','S','R','F'): infot=INFOT_SOURCE_MEDIA; hdr="Source Form";break;
    // ITCH - Identifies the technician who digitized the subject file;
    // for example, "Smith, John."
    case mmioFOURCC('I','T','C','H'): hdr="Technician";break;
    case mmioFOURCC('I','S','M','P'): hdr="Time Code";break;
    case mmioFOURCC('I','D','I','T'): hdr="Digitization Time";break;

    case ckidAVIMAINHDR:          // read 'avih'
      demuxer->stream->read((char*) &avih,std::min(size2,unsigned(sizeof(avih))));
      le2me_MainAVIHeader(&avih); // swap to machine endian
      chunksize-=std::min(size2,unsigned(sizeof(avih)));
      demuxer->movi_length=avih.dwTotalFrames;
      if(mp_conf.verbose) print_avih(&avih); else print_avih_flags(&avih);
      break;
    case ckidSTREAMHEADER: {      // read 'strh'
      AVIStreamHeader h;
      demuxer->stream->read((char*) &h,std::min(size2,unsigned(sizeof(h))));
      le2me_AVIStreamHeader(&h);  // swap to machine endian
      chunksize-=std::min(size2,unsigned(sizeof(h)));
      ++stream_id;
      if(h.fccType==mmioFOURCC('i','a','v','s')){
	MSG_FATAL("DVDS chunk found!!! Still is not suupported\n");
      }
      if(h.fccType==streamtypeVIDEO){
	sh_video=demuxer->new_sh_video(stream_id);
	memcpy(&sh_video->video,&h,sizeof(h));
//	demuxer->video->sh=sh_video;
      } else
      if(h.fccType==streamtypeAUDIO){
	MSG_V("avi: Found audio stream\n");
	sh_audio=demuxer->new_sh_audio(stream_id);
	memcpy(&sh_audio->audio,&h,sizeof(h));
//	demuxer->audio->sh=sh_audio;
      }
      last_fccType=h.fccType;
      if(mp_conf.verbose>=1) print_strh(&h);
      break; }
    case mmioFOURCC('i', 'n', 'd', 'x'): {
      uint32_t i;
      unsigned msize = 0;
      avisuperindex_chunk *s;
      priv->suidx_size++;
      priv->suidx = (avisuperindex_chunk*)mp_realloc(priv->suidx, priv->suidx_size * sizeof (avisuperindex_chunk));
      s = &priv->suidx[priv->suidx_size-1];

      chunksize-=24;
      memcpy(s->fcc, "indx", 4);
      s->dwSize = size2;
      s->wLongsPerEntry = demuxer->stream->read_word_le();
      s->bIndexSubType = demuxer->stream->read_char();
      s->bIndexType = demuxer->stream->read_char();
      s->nEntriesInUse = demuxer->stream->read_dword_le();
      *(uint32_t *)s->dwChunkId = demuxer->stream->read_dword_le();
      demuxer->stream->read( (char *)s->dwReserved, 3*4);
      memset(s->dwReserved, 0, 3*4);

      print_avisuperindex_chunk(s);

      msize = sizeof (uint32_t) * s->wLongsPerEntry * s->nEntriesInUse;
      s->aIndex = (avisuperindex_entry*)mp_mallocz(msize);
      s->stdidx = new(zeromem) avistdindex_chunk[s->nEntriesInUse];

      // now the real index of indices
      for (i=0; i<s->nEntriesInUse; i++) {
	  chunksize-=16;
	  s->aIndex[i].qwOffset = demuxer->stream->read_dword_le() & 0xffffffff;
	  s->aIndex[i].qwOffset |= ((uint64_t)demuxer->stream->read_dword_le() & 0xffffffff)<<32;
	  s->aIndex[i].dwSize = demuxer->stream->read_dword_le();
	  s->aIndex[i].dwDuration = demuxer->stream->read_dword_le();
	  MSG_DBG2("ODML (%.4s): [%d] 0x%016llx 0x%04lx %ld\n",
		  (s->dwChunkId), i,
		  (uint64_t)s->aIndex[i].qwOffset, s->aIndex[i].dwSize, s->aIndex[i].dwDuration);
      }

      break; }
    case ckidSTREAMFORMAT: {      // read 'strf'
      if(last_fccType==streamtypeVIDEO){
	sh_video->bih=(BITMAPINFOHEADER*)mp_mallocz((chunksize<sizeof(BITMAPINFOHEADER))?sizeof(BITMAPINFOHEADER):chunksize);
	MSG_V("found 'bih', %u bytes of %d\n",chunksize,sizeof(BITMAPINFOHEADER));
	demuxer->stream->read((char*) sh_video->bih,chunksize);
	le2me_BITMAPINFOHEADER(sh_video->bih);  // swap to machine endian
	// fixup MS-RLE header (seems to be broken for <256 color files)
	if(sh_video->bih->biCompression<=1 && sh_video->bih->biSize==40)
	    sh_video->bih->biSize=chunksize;
	if(mp_conf.verbose>=1) print_video_header(sh_video->bih,chunksize);
	if(chunksize>0x28 && strncmp((char *)&sh_video->bih->biCompression,"MPGI",4) == 0)
		sh_video->aspect=get_avi_aspect(*(((char *)sh_video->bih)+0x28));
	chunksize=0;
//        sh_video->fps=(float)sh_video->video.dwRate/(float)sh_video->video.dwScale;
//        if(demuxer->video->id==-1) demuxer->video->id=stream_id;
	// IdxFix:
	idxfix_videostream=stream_id;
	switch(sh_video->bih->biCompression){
	case mmioFOURCC('M', 'P', 'G', '4'):
	case mmioFOURCC('m', 'p', 'g', '4'):
	case mmioFOURCC('D', 'I', 'V', '1'):
	  idxfix_divx=3; // set index recovery mpeg4 flavour: msmpeg4v1
	  MSG_V("Regenerating keyframe table for M$ mpg4v1 video\n");
	  break;
	case mmioFOURCC('D', 'I', 'V', '3'):
	case mmioFOURCC('d', 'i', 'v', '3'):
	case mmioFOURCC('D', 'I', 'V', '4'):
	case mmioFOURCC('d', 'i', 'v', '4'):
	case mmioFOURCC('D', 'I', 'V', '5'):
	case mmioFOURCC('d', 'i', 'v', '5'):
	case mmioFOURCC('D', 'I', 'V', '6'):
	case mmioFOURCC('d', 'i', 'v', '6'):
	case mmioFOURCC('M', 'P', '4', '3'):
	case mmioFOURCC('m', 'p', '4', '3'):
	case mmioFOURCC('M', 'P', '4', '2'):
	case mmioFOURCC('m', 'p', '4', '2'):
	case mmioFOURCC('D', 'I', 'V', '2'):
	case mmioFOURCC('A', 'P', '4', '1'):
	  idxfix_divx=1; // set index recovery mpeg4 flavour: msmpeg4v3
	  MSG_V("Regenerating keyframe table for DIVX 3 video\n");
	  break;
	case mmioFOURCC('D', 'I', 'V', 'X'):
	case mmioFOURCC('d', 'i', 'v', 'x'):
	case mmioFOURCC('D', 'X', '5', '0'):
	case mmioFOURCC('X', 'V', 'I', 'D'):
	case mmioFOURCC('x', 'v', 'i', 'd'):
	case mmioFOURCC('F', 'M', 'P', '4'):
	case mmioFOURCC('f', 'm', 'p', '4'):
	  idxfix_divx=2; // set index recovery mpeg4 flavour: generic mpeg4
	  MSG_V("Regenerating keyframe table for DIVX 4 video\n");
	  break;
	}
      } else
      if(last_fccType==streamtypeAUDIO){
	unsigned wf_size = chunksize<sizeof(WAVEFORMATEX)?sizeof(WAVEFORMATEX):chunksize;
	sh_audio->wf=(WAVEFORMATEX*)mp_mallocz(wf_size);
	MSG_V("found 'wf', %d bytes of %d\n",chunksize,sizeof(WAVEFORMATEX));
	demuxer->stream->read((char*) sh_audio->wf,chunksize);
	le2me_WAVEFORMATEX(sh_audio->wf);
	if (sh_audio->wf->cbSize != 0 &&
	    wf_size < sizeof(WAVEFORMATEX)+sh_audio->wf->cbSize) {
	    sh_audio->wf=(WAVEFORMATEX*)mp_realloc(sh_audio->wf, sizeof(WAVEFORMATEX)+sh_audio->wf->cbSize);
	}
	chunksize=0;
	if(mp_conf.verbose>=1) print_wave_header(sh_audio->wf,wf_size);
	++priv->audio_streams;
//        if(demuxer->audio->id==-1) demuxer->audio->id=stream_id;
      }
      break;
    }
    case mmioFOURCC('v', 'p', 'r', 'p'): {
	VideoPropHeader* vprp = (VideoPropHeader*)mp_malloc(chunksize);
	unsigned int i;
	demuxer->stream->read( (any_t*)vprp, chunksize);
	le2me_VideoPropHeader(vprp);
	chunksize -= sizeof(*vprp)-sizeof(vprp->FieldInfo);
	chunksize /= sizeof(VIDEO_FIELD_DESC);
	if (vprp->nbFieldPerFrame > chunksize) {
	    vprp->nbFieldPerFrame = chunksize;
	}
	chunksize = 0;
	for (i=0; i<vprp->nbFieldPerFrame; i++) {
		le2me_VIDEO_FIELD_DESC(&vprp->FieldInfo[i]);
	}
	if (sh_video) {
		sh_video->aspect = GET_AVI_ASPECT(vprp->dwFrameAspectRatio);
	}
	if(mp_conf.verbose>=1) print_vprp(vprp);
	delete vprp;
	break;
    }
    case mmioFOURCC('d', 'm', 'l', 'h'): {
	// dmlh 00 00 00 04 frms
	unsigned int total_frames = demuxer->stream->read_dword_le();
	MSG_V("Found ODML header %lu frames %lu\n", chunksize, total_frames);
	demuxer->stream->skip( chunksize-4);
	chunksize = 0;
    }
    break;
    case ckidAVINEWINDEX:
    if(demuxer->movi_end>demuxer->stream->tell())
	demuxer->movi_end=demuxer->stream->tell(); // fixup movi-end
    if(index_mode && !priv->is_odml){
      int i;
      priv->idx_size=size2>>4;
      MSG_V("Reading INDEX block, %d chunks for %ld frames\n",
	priv->idx_size,avih.dwTotalFrames);
      priv->idx=(AVIINDEXENTRY*)mp_malloc(priv->idx_size<<4);
      demuxer->stream->read((char*)priv->idx,priv->idx_size<<4);
      for (i = 0; i < priv->idx_size; i++) {	// swap index to machine endian
	AVIINDEXENTRY *entry=(AVIINDEXENTRY*)priv->idx + i;
	le2me_AVIINDEXENTRY(entry);
	/*
	 * We (ab)use the upper word for bits 32-47 of the offset, so
	 * we'll clear them here.
	 * FIXME: AFAIK no codec uses them, but if one does it will break
	 */
	entry->dwFlags&=0xffff;
      }
      chunksize-=priv->idx_size<<4;
      if(mp_conf.verbose>=2) print_index(priv->idx,priv->idx_size);
    }
    break;
    /* added May 2002 */
    case mmioFOURCC('R','I','F','F'): {
	unsigned riff_type;

	MSG_V( "additional RIFF header...\n");
	riff_type = demuxer->stream->read_dword_le();
	if(riff_type != mmioFOURCC('A','V','I','X'))
	    MSG_WARN("** warning: this is no AVIX header..\n");
	else {
		/*
		 * We got an extended AVI header, so we need to switch to
		 * ODML to get seeking to work, provided we got indx chunks
		 * in the header (suidx_size > 0).
		 */
		if (priv->suidx_size > 0)
			priv->is_odml = 1;
	}
	chunksize = 0;
	list_end = 0; /* a new list will follow */
	break;
    }
    case ckidAVIPADDING:
	demuxer->stream->skip( chunksize);
	chunksize = 0;
	break;
  }
  if(hdr){
    MSG_DBG2("hdr=%s  size=%d\n",hdr,size2);
    if(size2==3)
      chunksize=1; // empty
    else {
      char buf[256];
      int len=(size2<250)?size2:250;
      demuxer->stream->read(buf,len);
      chunksize-=len;
      buf[len]=0;
      MSG_V("%-10s: %s\n",hdr,buf);
      if(infot!=-1) demuxer->info().add(infot, buf);
      else	    MSG_V("   %s: %s\n",hdr,buf);
    }
  }
  MSG_DBG2("list_end=0x%X  pos=0x%X  chunksize=0x%X  next=0x%X\n",
      (int)list_end, (int)demuxer->stream->tell(),
      chunksize, (int)chunksize+demuxer->stream->tell());
  if(list_end>0 && chunksize+demuxer->stream->tell()>list_end){
      MSG_V("Broken chunk?  chunksize=%d  (id=%.4s)\n",chunksize,(char *) &id);
      demuxer->stream->seek(list_end);
      list_end=0;
  } else
  if(chunksize>0) demuxer->stream->skip(chunksize); else
  if((int)chunksize<0) MSG_WARN("chunksize=%u  (id=%.4s)\n",chunksize,(char *) &id);

}

if (priv->suidx_size > 0 && priv->idx_size == 0) {
    /*
     * No NEWAVIINDEX, but we got an OpenDML index.
     */
    priv->is_odml = 1;
}

if (priv->is_odml && (index_mode==-1 || index_mode==0)) {
    unsigned int i, j, k;

    avisuperindex_chunk *cx;
    AVIINDEXENTRY *idx;


    if (priv->idx_size) delete priv->idx;
    priv->idx_size = 0;
    priv->idx_offset = 0;
    priv->idx = NULL;

    MSG_STATUS("Build ODML indexes %ul bytes\n", priv->suidx_size);

    // read the standard indices
    for (cx = &priv->suidx[0], i=0; i<priv->suidx_size; cx++, i++) {
	demuxer->stream->reset();
	for (j=0; j<cx->nEntriesInUse; j++) {
	    int ret1, ret2;
	    memset(&cx->stdidx[j], 0, 32);
	    ret1 = demuxer->stream->seek( (off_t)cx->aIndex[j].qwOffset);
	    ret2 = demuxer->stream->read( (char *)&cx->stdidx[j], 32);
	    if (ret2 != 32 || cx->stdidx[j].nEntriesInUse==0) {
		// this is a broken file (probably incomplete) let the standard
		// gen_index routine handle this
		priv->is_odml = 0;
		priv->idx_size = 0;
		MSG_WARN("Broken ODML file (off_t=%u cx->aIndex[%li].qwOffset=%llX)\n",sizeof(off_t),j,(off_t)cx->aIndex[j].qwOffset);
		goto freeout;
	    }

	    le2me_avistdindex_chunk(&cx->stdidx[j]);
	    print_avistdindex_chunk(&cx->stdidx[j]);
	    priv->idx_size += cx->stdidx[j].nEntriesInUse;
	    cx->stdidx[j].aIndex = new avistdindex_entry[cx->stdidx[j].nEntriesInUse];
	    demuxer->stream->read( (char *)cx->stdidx[j].aIndex,
		    cx->stdidx[j].nEntriesInUse*sizeof(avistdindex_entry));
	    for (k=0;k<cx->stdidx[j].nEntriesInUse; k++)
		le2me_avistdindex_entry(&cx->stdidx[j].aIndex[k]);

	    cx->stdidx[j].dwReserved3 = 0;

	}
    }

    /*
     * We convert the index by translating all entries into AVIINDEXENTRYs
     * and sorting them by offset.  The result should be the same index
     * we would get with -forceidx.
     */

    idx = priv->idx = new AVIINDEXENTRY[priv->idx_size];

    for (cx = priv->suidx; cx != &priv->suidx[priv->suidx_size]; cx++) {
	avistdindex_chunk *sic;
	for (sic = cx->stdidx; sic != &cx->stdidx[cx->nEntriesInUse]; sic++) {
	    avistdindex_entry *sie;
	    for (sie = sic->aIndex; sie != &sic->aIndex[sic->nEntriesInUse]; sie++) {
		uint64_t off = sic->qwBaseOffset + sie->dwOffset - 8;
		memcpy(&idx->ckid, sic->dwChunkId, 4);
		idx->dwChunkOffset = off;
		idx->dwFlags = (off >> 32) << 16;
		idx->dwChunkLength = sie->dwSize & 0x7fffffff;
		idx->dwFlags |= (sie->dwSize&0x80000000)?0x0:AVIIF_KEYFRAME; // bit 31 denotes !keyframe
		idx++;
	    }
	}
    }
    qsort(priv->idx, priv->idx_size, sizeof(AVIINDEXENTRY), avi_idx_cmp);

    /*
       Hack to work around a "wrong" index in some divx odml files
       (processor_burning.avi as an example)
       They have ##dc on non keyframes but the ix00 tells us they are ##db.
       Read the fcc of a non-keyframe vid frame and check it.
     */

    {
	uint32_t id;
	uint32_t db = 0;
	demuxer->stream->reset();

	// find out the video stream id. I have seen files with 01db.
	for (idx = &((AVIINDEXENTRY *)priv->idx)[0], i=0; i<priv->idx_size; i++, idx++){
	    unsigned char res[2];
	    if (odml_get_vstream_id(idx->ckid, res)) {
		db = mmioFOURCC(res[0], res[1], 'd', 'b');
		break;
	    }
	}

	// find first non keyframe
	for (idx = &((AVIINDEXENTRY *)priv->idx)[0], i=0; i<priv->idx_size; i++, idx++){
	    if (!(idx->dwFlags & AVIIF_KEYFRAME) && idx->ckid == db) break;
	}
	if (i<priv->idx_size && db) {
	    demuxer->stream->seek( avi_idx_offset(idx));
	    id = demuxer->stream->read_dword_le();
	    if (id && id != db) // index fcc and real fcc differ? fix it.
		for (idx = &((AVIINDEXENTRY *)priv->idx)[0], i=0; i<priv->idx_size; i++, idx++){
		    if (!(idx->dwFlags & AVIIF_KEYFRAME) && idx->ckid == db)
			idx->ckid = id;
	    }
	}
    }

    if (mp_conf.verbose>=2) print_index(priv->idx, priv->idx_size);

    demuxer->movi_end=demuxer->stream->end_pos();

freeout:

    // mp_free unneeded stuff
    cx = &priv->suidx[0];
    do {
	for (j=0;j<cx->nEntriesInUse;j++)
	    if (cx->stdidx[j].nEntriesInUse) delete cx->stdidx[j].aIndex;
	delete cx->stdidx;

    } while (cx++ != &priv->suidx[priv->suidx_size-1]);
    delete priv->suidx;

}
/* Read a saved index file */
#if 0
if (index_file_load) {
  FILE *fp;
  char magic[7];
  unsigned int i;

  if ((fp = fopen(index_file_load, "r")) == NULL) {
    MSG_ERR(MSGTR_MPDEMUX_AVIHDR_CantReadIdxFile, index_file_load, strerror(errno));
    goto gen_index;
  }
  fread(&magic, 6, 1, fp);
  if (strncmp(magic, "MPIDX1", 6)) {
    MSG_ERR(MSGTR_MPDEMUX_AVIHDR_NotValidMPidxFile, index_file_load);
    goto gen_index;
  }
  fread(&priv->idx_size, sizeof(priv->idx_size), 1, fp);
  priv->idx=mp_malloc(priv->idx_size*sizeof(AVIINDEXENTRY));
  if (!priv->idx) {
    MSG_ERR(MSGTR_MPDEMUX_AVIHDR_FailedMallocForIdxFile, index_file_load);
    priv->idx_size = 0;
    goto gen_index;
  }

  for (i=0; i<priv->idx_size;i++) {
    AVIINDEXENTRY *idx;
    idx=&((AVIINDEXENTRY *)priv->idx)[i];
    fread(idx, sizeof(AVIINDEXENTRY), 1, fp);
    if (feof(fp)) {
      MSG_ERR(MSGTR_MPDEMUX_AVIHDR_PrematureEOF, index_file_load);
      delete priv->idx;
      priv->idx_size = 0;
      goto gen_index;
    }
  }
  fclose(fp);
  mp_msg(MSGT_HEADER,MSGL_INFO, MSGTR_MPDEMUX_AVIHDR_IdxFileLoaded, index_file_load);
}
#endif
/* Generate indexes */
if(index_mode>=2 || (priv->idx_size==0 && index_mode==1)){
  // build index for file:
  demuxer->stream->reset();
  demuxer->stream->seek(demuxer->movi_start);

  priv->idx_pos=0;
  priv->idx_size=0;
  priv->idx=NULL;

  while(1){
    int id;
    unsigned len;
    off_t skip;
    AVIINDEXENTRY* idx;
    unsigned int c;
    demuxer->filepos=demuxer->stream->tell();
    if(demuxer->filepos>=demuxer->movi_end && demuxer->movi_start<demuxer->movi_end) break;
    id=demuxer->stream->read_dword_le();
    len=demuxer->stream->read_dword_le();
    if(id==mmioFOURCC('L','I','S','T') || id==mmioFOURCC('R', 'I', 'F', 'F')){
      id=demuxer->stream->read_dword_le(); // list or RIFF type
      continue;
    }
    if(demuxer->stream->eof()) break;
    if(!id || avi_stream_id(id)==100) goto skip_chunk; // bad ID (or padding?)

    if(priv->idx_pos>=priv->idx_size){
//      priv->idx_size+=32;
      priv->idx_size+=1024; // +16kB
      priv->idx=(AVIINDEXENTRY*)mp_realloc(priv->idx,priv->idx_size*sizeof(AVIINDEXENTRY));
      if(!priv->idx){priv->idx_pos=0; break;} // error!
    }
    idx=&((AVIINDEXENTRY *)priv->idx)[priv->idx_pos++];
    idx->ckid=id;
    idx->dwFlags=AVIIF_KEYFRAME; // FIXME
    idx->dwFlags|=(demuxer->filepos>>16)&0xffff0000U;
    idx->dwChunkOffset=(unsigned long)demuxer->filepos;
    idx->dwChunkLength=len;

    c=demuxer->stream->read_dword();

    // Fix keyframes for DivX files:
    if(idxfix_divx)
      if(avi_stream_id(id)==idxfix_videostream){
	switch(idxfix_divx){
	    case 3: c=demuxer->stream->read_dword()<<5; //skip 32+5 bits for m$mpeg4v1
	    case 1: if(c&0x40000000) idx->dwFlags&=~AVIIF_KEYFRAME;break; // divx 3
	    case 2: if(c==0x1B6) idx->dwFlags&=~AVIIF_KEYFRAME;break; // divx 4
	}
      }

    // update status line:
    { static off_t lastpos;
      off_t pos;
      off_t _len=demuxer->movi_end-demuxer->movi_start;
      if(_len){
	  pos=100*(demuxer->filepos-demuxer->movi_start)/_len; // %
      } else {
	  pos=(demuxer->filepos-demuxer->movi_start)>>20; // MB
      }
      if(pos!=lastpos){
	  lastpos=pos;
	  MSG_STATUS("Generating Indexes: %3lu %s     \r",(unsigned long)pos, _len?"%":"MB");
      }
    }
    MSG_DBG2("%08X %08X %.4s %08X %X\n",(unsigned int)demuxer->filepos,id,(char *) &id,(int)c,(unsigned int) idx->dwFlags);
skip_chunk:
    skip=(len+1)&(~1UL); // total bytes in this chunk
    demuxer->stream->seek(8+demuxer->filepos+skip);
  }
  priv->idx_size=priv->idx_pos;
  MSG_INFO("Indexed are generated for %ul chunks\n",priv->idx_size);
  if(mp_conf.verbose>=2) print_index(priv->idx,priv->idx_size);

#if 0
  /* Write generated index to a file */
  if (index_file_save) {
    FILE *fp;
    unsigned int i;

    if ((fp=fopen(index_file_save, "w")) == NULL) {
      MSG_ERR(MSGTR_MPDEMUX_AVIHDR_Failed2WriteIdxFile, index_file_save, strerror(errno));
      return;
    }
    fwrite("MPIDX1", 6, 1, fp);
    fwrite(&priv->idx_size, sizeof(priv->idx_size), 1, fp);
    for (i=0; i<priv->idx_size; i++) {
      AVIINDEXENTRY *idx = &((AVIINDEXENTRY *)priv->idx)[i];
      fwrite(idx, sizeof(AVIINDEXENTRY), 1, fp);
    }
    fclose(fp);
    MSG_INFO(MSGTR_MPDEMUX_AVIHDR_IdxFileSaved, index_file_save);
  }
#endif
}
}
#undef MIN

// Select ds from ID
static Demuxer_Stream* demux_avi_select_stream(Demuxer *demux,unsigned int id){
  int stream_id=avi_stream_id(id);

  if(demux->video->id==-1)
    if(demux->get_sh_video(stream_id))
	demux->video->id=stream_id;

  if(demux->audio->id==-1)
    if(demux->get_sh_audio(stream_id))
	demux->audio->id=stream_id;

  if(stream_id==demux->audio->id){
      if(!demux->audio->sh){
	sh_audio_t* sh;
	avi_priv_t *priv=static_cast<avi_priv_t*>(demux->priv);
	demux->audio->sh=sh=demux->get_sh_audio(stream_id);
	MSG_V("Auto-selected AVI audio ID = %d\n",demux->audio->id);
	if(sh->wf){
	  priv->audio_block_size=sh->wf->nBlockAlign;
	  if(!priv->audio_block_size){
	    // for PCM audio we can calculate the blocksize:
	    if(sh->wtag==1)
		priv->audio_block_size=sh->wf->nChannels*(sh->wf->wBitsPerSample/8);
	    else
		priv->audio_block_size=1; // hope the best...
	  } else {
	    // workaround old mencoder's bug:
	    if(sh->audio.dwSampleSize==1 && sh->audio.dwScale==1 &&
	       (sh->wf->nBlockAlign==1152 || sh->wf->nBlockAlign==576)){
		MSG_WARN("AVI: Workarounding CBR-MP3 nBlockAlign header bug!\n");
		priv->audio_block_size=1;
	    }
	  }
	} else {
	  priv->audio_block_size=sh->audio.dwSampleSize;
	}
      }
      return demux->audio;
  }
  if(stream_id==demux->video->id){
      if(!demux->video->sh){
	demux->video->sh=demux->get_sh_video(stream_id);
	MSG_V("Auto-selected AVI video ID = %d\n",demux->video->id);
      }
      return demux->video;
  }
  if(id!=mmioFOURCC('J','U','N','K')){
     // unknown
     MSG_DBG2("Unknown chunk: %.4s (%X)\n",(char *) &id,id);
     //abort();
  }
  return NULL;
}

static int valid_fourcc(unsigned int id){
    unsigned char* fcc=(unsigned char*)(&id);
#define FCC_CHR_CHECK(x) (x<48 || x>=96)
    if(FCC_CHR_CHECK(fcc[0])) return 0;
    if(FCC_CHR_CHECK(fcc[1])) return 0;
    if(FCC_CHR_CHECK(fcc[2])) return 0;
    if(FCC_CHR_CHECK(fcc[3])) return 0;
    return 1;
#undef FCC_CHR_CHECK
}

static int choose_chunk_len(unsigned int len1,unsigned int len2){
    // len1 has a bit more priority than len2. len1!=len2
    // Note: this is a first-idea-logic, may be wrong. comments welcomed.

    // prefer small frames rather than 0
    if(!len1) return (len2>0x80000) ? len1 : len2;
    if(!len2) return (len1>0x100000) ? len2 : len1;

    // choose the smaller value:
    return (len1<len2)? len1 : len2;
}

static int demux_avi_read_packet(Demuxer *demux,Demuxer_Stream *ds,unsigned int id,unsigned int len,int idxpos,dp_flags_e flags){
  avi_priv_t *priv=static_cast<avi_priv_t*>(demux->priv);
  int skip;
  float pts=0;

  MSG_DBG3("demux_avi.read_packet: %X\n",id);

  if(ds==demux->audio){
      if(priv->pts_corrected==0){
	  if(priv->pts_has_video){
	      // we have video pts now
	      float delay=0;
	      if(((sh_audio_t*)(ds->sh))->wf->nAvgBytesPerSec)
		  delay=(float)priv->pts_corr_bytes/((sh_audio_t*)(ds->sh))->wf->nAvgBytesPerSec;
	      MSG_V("XXX initial  v_pts=%5.3f  a_pos=%d (%5.3f) \n",priv->avi_audio_pts,priv->pts_corr_bytes,delay);
	      //priv->pts_correction=-priv->avi_audio_pts+delay;
	      priv->pts_correction=delay-priv->avi_audio_pts;
	      priv->avi_audio_pts+=priv->pts_correction;
	      priv->pts_corrected=1;
	  } else
		priv->pts_corr_bytes+=len;
      }
      pts=priv->avi_audio_pts; //+priv->pts_correction;
      priv->avi_audio_pts=0;
      // update blockcount:
      priv->audio_block_no+=priv->audio_block_size ?
	((len+priv->audio_block_size-1)/priv->audio_block_size) : 1;
  } else
  if(ds==demux->video){
     // video
     if(priv->skip_video_frames>0){
       // drop frame (seeking)
       --priv->skip_video_frames;
       ds=NULL;
     }

     pts = priv->avi_video_pts = priv->video_pack_no *
	 (float)((sh_video_t*)demux->video->sh)->video.dwScale /
	 (float)((sh_video_t*)demux->video->sh)->video.dwRate;
//          printf("\rYYY-V  A: %5.3f  V: %5.3f  \n",priv->avi_audio_pts,priv->avi_video_pts);

     priv->avi_audio_pts=priv->avi_video_pts+priv->pts_correction;
     priv->pts_has_video=1;

     if(ds) ++priv->video_pack_no;

  }

//  len=demux->stream->read_dword_le();
  skip=(len+1)&(~1); // total bytes in this chunk

  if(ds){
    MSG_DBG2("DEMUX_AVI: Read %d data bytes from packet %04X\n",len,id);
    ds->read_packet(demux->stream,len,pts,idxpos,flags);
    skip-=len;
  }
  if(skip){
    MSG_DBG2("DEMUX_AVI: Skipping %d bytes from packet %04X\n",skip,id);
    demux->stream->skip(skip);
  }
  return ds?1:0;
}

static int avi_read_ni(Demuxer *demux,Demuxer_Stream* ds);
static int avi_read_nini(Demuxer *demux,Demuxer_Stream* ds);

static int avi_demux(Demuxer *demux,Demuxer_Stream *__ds){
    avi_priv_t *priv=static_cast<avi_priv_t*>(demux->priv);
    unsigned int id=0;
    unsigned int len;
    int ret=0;
    Demuxer_Stream *ds;

do{
  dp_flags_e flags=DP_KEYFRAME;
  AVIINDEXENTRY *idx=NULL;
  if(priv->idx_size>0 && priv->idx_pos<priv->idx_size){
    off_t pos;

    idx=&((AVIINDEXENTRY *)priv->idx)[priv->idx_pos++];

    //demux->stream->seek(idx.dwChunkOffset);
    //  pos-4,idx->dwChunkLength,idx->dwFlags);
    if(idx->dwFlags&AVIIF_LIST){
      // LIST
      continue;
    }
    if(!demux_avi_select_stream(demux,idx->ckid)){
      MSG_DBG3("Skip chunk %.4s (0x%X)  \n",(char *)&idx->ckid,(unsigned int)idx->ckid);
      continue; // skip this chunk
    }

    pos = (off_t)priv->idx_offset+avi_idx_offset(idx);
    if((pos<demux->movi_start || pos>=demux->movi_end) && (demux->movi_end>demux->movi_start) && (demux->flags & Demuxer::Seekable)){
      MSG_V("ChunkOffset out of range!   idx=0x%X  \n",pos);
      continue;
    }
#if 0
    if(pos!=demux->filepos){
      MSG_V("Warning! pos=0x%X  idx.pos=0x%X  diff=%d   \n",demux->filepos,pos,pos-demux->filepos);
    }
#endif
    demux->stream->seek(pos);
    demux->filepos=demux->stream->tell();
    id=demux->stream->read_dword_le();
    if(demux->stream->eof()) return 0; // EOF!

    if(id!=idx->ckid){
      MSG_V("ChunkID mismatch! raw=%.4s idx=%.4s  \n",(char *)&id,(char *)&idx->ckid);
      if(valid_fourcc(idx->ckid))
	  id=idx->ckid;	// use index if valid
      else
	  if(!valid_fourcc(id)) continue; // drop chunk if both id and idx bad
    }
    len=demux->stream->read_dword_le();
//    if((len&(~1))!=(idx->dwChunkLength&(~1))){
//    if((len)!=(idx->dwChunkLength)){
    if((len!=idx->dwChunkLength)&&((len+1)!=idx->dwChunkLength)){
      MSG_V("ChunkSize mismatch! raw=%d idx=%ld  \n",len,idx->dwChunkLength);
      if(len>0x200000 && idx->dwChunkLength>0x200000) continue; // both values bad :(
      len=choose_chunk_len(idx->dwChunkLength,len);
    }
    if(!(idx->dwFlags&AVIIF_KEYFRAME)) flags=DP_NONKEYFRAME;
  } else {
    demux->filepos=demux->stream->tell();
    if(demux->filepos>=demux->movi_end && demux->movi_end>demux->movi_start && (demux->flags & Demuxer::Seekable)){
	demux->stream->eof(1);
	return 0;
    }
    id=demux->stream->read_dword_le();
    len=demux->stream->read_dword_le();
    if(demux->stream->eof()) return 0; // EOF!

    if(id==mmioFOURCC('L','I','S','T') || id==mmioFOURCC('R', 'I', 'F', 'F')){
      id=demux->stream->read_dword_le(); // list or RIFF type
      continue;
    }
  }
  ds=demux_avi_select_stream(demux,id);
  if(ds)
    if(ds->packs()+1>=MAX_PACKS || ds->bytes()+len>=MAX_PACK_BYTES){
	/* this packet will cause a buffer overflow, switch to -ni mode!!! */
	MSG_WARN("\nBadly interleaved .AVI detected - switching to -ni mode...\n");
	if(priv->idx_size>0){
	    // has index
	    priv->nini=1;
	    --priv->idx_pos; // hack
	} else {
	    // no index
	    priv->nini=1;
	    priv->idx_pos=demux->filepos; // hack
	}
	priv->idx_pos_v=priv->idx_pos_a=priv->idx_pos;
	// quit now, we can't even (no enough buffer memory) read this packet :(
	return -1;
    }
  ret=demux_avi_read_packet(demux,ds,id,len,priv->idx_pos-1,flags);
} while(ret!=1);
  return 1;
}

int index_mode=-1;  // -1=untouched  0=don't use index  1=use (geneate) index
extern demuxer_driver_t demux_ogg;
static Opaque* avi_open(Demuxer* demuxer){
    Demuxer_Stream *d_audio=demuxer->audio;
    Demuxer_Stream *d_video=demuxer->video;
    sh_audio_t *sh_audio=NULL;
    sh_video_t *sh_video=NULL;
    avi_priv_t* priv=new(zeromem) avi_priv_t;

    // priv struct:
    demuxer->priv=priv;

    //---- AVI header:
    read_avi_header(demuxer,(demuxer->stream->type()&Stream::Type_Seekable)?index_mode:-2);

  if(demuxer->audio->id>=0 && !demuxer->get_sh_audio(demuxer->audio->id)){
      MSG_WARN("AVI: invalid audio stream ID: %d - ignoring (nosound)\n",demuxer->audio->id);
      demuxer->audio->id=-2; // disabled
  }
  if(demuxer->video->id>=0 && !demuxer->get_sh_video(demuxer->video->id)){
      MSG_WARN("AVI: invalid video stream ID: %d - ignoring (using default)\n",demuxer->video->id);
      demuxer->video->id=-1; // autodetect
  }

  demuxer->stream->reset();
  demuxer->stream->seek(demuxer->movi_start);
  priv->idx_pos=0;
  priv->idx_pos_a=0;
  priv->idx_pos_v=0;
  if(priv->idx_size>1){
    // decide index format:
#if 1
    if((unsigned long)((AVIINDEXENTRY *)priv->idx)[0].dwChunkOffset<demuxer->movi_start ||
       (unsigned long)((AVIINDEXENTRY *)priv->idx)[1].dwChunkOffset<demuxer->movi_start)
      priv->idx_offset=demuxer->movi_start-4;
    else
      priv->idx_offset=0;
#else
    if((unsigned long)((AVIINDEXENTRY *)priv->idx)[0].dwChunkOffset<demuxer->movi_start)
      priv->idx_offset=demuxer->movi_start-4;
    else
      priv->idx_offset=0;
#endif
    MSG_V("AVI index offset: 0x%X (movi=0x%X idx0=0x%X idx1=0x%X)\n",
	    (int)priv->idx_offset,(int)demuxer->movi_start,
	    (int)((AVIINDEXENTRY *)priv->idx)[0].dwChunkOffset,
	    (int)((AVIINDEXENTRY *)priv->idx)[1].dwChunkOffset);
  }
//  demuxer->endpos=avi_header.movi_end;

  if(priv->idx_size>0){
      // check that file is non-interleaved:
      int i;
      off_t a_pos=-1;
      off_t v_pos=-1;
      for(i=0;i<priv->idx_size;i++){
	AVIINDEXENTRY* idx=&((AVIINDEXENTRY *)priv->idx)[i];
	Demuxer_Stream* ds=demux_avi_select_stream(demuxer,idx->ckid);
	off_t pos = priv->idx_offset + avi_idx_offset(idx);
	if(a_pos==-1 && ds==demuxer->audio){
	  a_pos=pos;
	  if(v_pos!=-1) break;
	}
	if(v_pos==-1 && ds==demuxer->video){
	  v_pos=pos;
	  if(a_pos!=-1) break;
	}
      }
      if(v_pos==-1){
	MSG_ERR("AVI_NI: " MSGTR_MissingVideoStream);
	return NULL;
      }
      if(a_pos==-1){
	MSG_ERR("AVI_NI: " MSGTR_MissingAudioStream);
	sh_audio=NULL;
    }
  } else demuxer->flags &= ~Demuxer::Seekable;
  if(!d_video->fill_buffer()){
    MSG_ERR("AVI: " MSGTR_MissingVideoStreamBug);
    return NULL;
  }
  sh_video=reinterpret_cast<sh_video_t*>(d_video->sh);
  sh_video->ds=d_video;
  if(d_audio->id!=-2){
    MSG_V("AVI: Searching for audio stream (id:%d)\n",d_audio->id);
    if(!priv->audio_streams || !d_audio->fill_buffer()){
      MSG_V("AVI: " MSGTR_MissingAudioStream);
      sh_audio=NULL;
    } else {
      sh_audio=reinterpret_cast<sh_audio_t*>(d_audio->sh);
      sh_audio->ds=d_audio;
      sh_audio->wtag=sh_audio->wf->wFormatTag;
    }
  }
  // calc. FPS:
  sh_video->fps=(float)sh_video->video.dwRate/(float)sh_video->video.dwScale;
  demuxer->movi_length=(unsigned)((float)demuxer->movi_length/sh_video->fps);
  // calculating video bitrate:
  if(priv->idx_size>0){
    // we have index, let's count 'em!
    size_t vsize=0;
    size_t asize=0;
    size_t vsamples=0;
    size_t asamples=0;
    int i;
    for(i=0;i<priv->idx_size;i++){
      int id=avi_stream_id(((AVIINDEXENTRY *)priv->idx)[i].ckid);
      int len=((AVIINDEXENTRY *)priv->idx)[i].dwChunkLength;
      if(sh_video->ds->id == id) {
	vsize+=len;
	++vsamples;
      }
      else if(sh_audio && sh_audio->ds->id == id) {
	asize+=len;
	asamples+=(len+priv->audio_block_size-1)/priv->audio_block_size;
      }
    }
    if(!priv->numberofframes) priv->numberofframes=vsamples;
    MSG_V("AVI video size=%lu (%lu) audio size=%lu (%lu)\n",vsize,priv->numberofframes,asize,asamples);
    if(sh_audio) sh_audio->i_bps=((float)asize/(float)asamples)*(float)sh_audio->audio.dwRate/(float)sh_audio->audio.dwScale;
  } else {
    // guessing, results may be inaccurate:
    size_t vsize;
    size_t asize=0;

    if(!priv->numberofframes) priv->numberofframes=sh_video->video.dwLength;
    if(priv->numberofframes<=1)
      // bad video header, try to get number of frames from audio
      if(sh_audio && sh_audio->wf->nAvgBytesPerSec) priv->numberofframes=sh_video->fps*sh_audio->audio.dwLength/sh_audio->audio.dwRate*sh_audio->audio.dwScale;
    if(priv->numberofframes<=1){
      MSG_WARN("Could not determine number of frames (for SOF seek)\n");
      priv->numberofframes=0;
    }

    if(sh_audio){
      if(sh_audio->wf->nAvgBytesPerSec && sh_audio->audio.dwSampleSize!=1){
	asize=(float)sh_audio->wf->nAvgBytesPerSec*sh_audio->audio.dwLength*sh_audio->audio.dwScale/sh_audio->audio.dwRate;
	sh_audio->i_bps=sh_audio->wf->nAvgBytesPerSec;
      } else {
	asize=sh_audio->audio.dwLength;
	sh_audio->i_bps=(float)asize*sh_video->fps/priv->numberofframes;
      }
    }
    vsize=demuxer->movi_end-demuxer->movi_start-asize-8*priv->numberofframes;
    MSG_V("AVI video size=%lu (%lu)  audio size=%lu\n",vsize,priv->numberofframes,asize);
  }
  MSG_V("VIDEO:  [%.4s]  %ldx%ld  %dbpp  %4.2f fps\n",
  (char *)&sh_video->bih->biCompression,
  sh_video->bih->biWidth,
  sh_video->bih->biHeight,
  sh_video->bih->biBitCount,
  sh_video->fps);
    check_pin("demuxer",demuxer->pin,DEMUX_PIN);
    return priv;
}

static void avi_seek(Demuxer *demuxer,const seek_args_t* seeka){
    avi_priv_t *priv=static_cast<avi_priv_t*>(demuxer->priv);
    Demuxer_Stream *d_audio=demuxer->audio;
    Demuxer_Stream *d_video=demuxer->video;
    sh_audio_t *sh_audio=reinterpret_cast<sh_audio_t*>(d_audio->sh);
    sh_video_t *sh_video=reinterpret_cast<sh_video_t*>(d_video->sh);
    float skip_audio_secs=0;

  //FIXME: OFF_T - Didn't check AVI case yet (avi files can't be >2G anyway?)
  //================= seek in AVI ==========================
    int rel_seek_frames=seeka->secs*sh_video->fps;
    int video_chunk_pos=d_video->pos;
    int i;

      if(seeka->flags&DEMUX_SEEK_SET){
	// seek SOF
	video_chunk_pos=0;
      }

      if(seeka->flags&DEMUX_SEEK_PERCENTS){
	rel_seek_frames=seeka->secs*priv->numberofframes;
      }

      priv->skip_video_frames=0;
      priv->avi_audio_pts=0;

      MSG_DBG2("AVI_SEEK:  v_pos=%08X\n",video_chunk_pos);
// ------------ STEP 1: find nearest video keyframe chunk ------------
      // find nearest video keyframe chunk pos:
      if(rel_seek_frames>0){
	// seek forward
	while(video_chunk_pos<priv->idx_size-1){
	  int id=((AVIINDEXENTRY *)priv->idx)[video_chunk_pos].ckid;
	  if(avi_stream_id(id)==d_video->id){  // video frame
	    if((--rel_seek_frames)<0 && ((AVIINDEXENTRY *)priv->idx)[video_chunk_pos].dwFlags&AVIIF_KEYFRAME) break;
	  }
	  ++video_chunk_pos;
	}
      } else {
	// seek backward
	while(video_chunk_pos>0){
	  int id=((AVIINDEXENTRY *)priv->idx)[video_chunk_pos].ckid;
	  if(avi_stream_id(id)==d_video->id){  // video frame
	    if((++rel_seek_frames)>0 && ((AVIINDEXENTRY *)priv->idx)[video_chunk_pos].dwFlags&AVIIF_KEYFRAME) break;
	  }
	  --video_chunk_pos;
	}
      }
      priv->idx_pos_a=priv->idx_pos_v=priv->idx_pos=video_chunk_pos;

      // re-calc video pts:
      d_video->pack_no=0;
      for(i=0;i<video_chunk_pos;i++){
	  int id=((AVIINDEXENTRY *)priv->idx)[i].ckid;
	  if(avi_stream_id(id)==d_video->id) ++d_video->pack_no;
      }
      priv->video_pack_no=d_video->pack_no;
      priv->avi_video_pts=d_video->pack_no*(float)sh_video->video.dwScale/(float)sh_video->video.dwRate;
      d_video->pos=video_chunk_pos;

      MSG_DBG2("V_SEEK:  pack=%d  pts=%5.3f  chunk=%d  \n",d_video->pack_no,priv->avi_video_pts,video_chunk_pos);

// ------------ STEP 2: seek audio, find the right chunk & pos ------------

      d_audio->pack_no=0;
      d_audio->dpos=0;
      priv->audio_block_no=0;

      if(sh_audio){
	int len=0;
	int skip_audio_bytes=0;
	int curr_audio_pos=-1;
	int audio_chunk_pos=-1;
	int chunk_max=!(priv->nini)?video_chunk_pos:priv->idx_size;

	if(sh_audio->audio.dwSampleSize){
	    // constant rate audio stream
#if 0
	    int align;
	    curr_audio_pos=(priv->avi_video_pts) * sh_audio->wf->nAvgBytesPerSec;
	    if(curr_audio_pos<0)curr_audio_pos=0;
	    align=sh_audio->audio.dwSampleSize;
	    if(sh_audio->wf->nBlockAlign>align) align=sh_audio->wf->nBlockAlign;
	    curr_audio_pos/=align;
	    curr_audio_pos*=align;
#else
	    curr_audio_pos=(priv->avi_video_pts)*(float)sh_audio->audio.dwRate/(float)sh_audio->audio.dwScale;
	    curr_audio_pos-=sh_audio->audio.dwStart;
	    curr_audio_pos*=sh_audio->audio.dwSampleSize;
#endif

	// find audio chunk pos:
	  for(i=0;i<chunk_max;i++){
	    int id=((AVIINDEXENTRY *)priv->idx)[i].ckid;
	    if(avi_stream_id(id)==d_audio->id){
		len=((AVIINDEXENTRY *)priv->idx)[i].dwChunkLength;
		if(d_audio->dpos<=curr_audio_pos && curr_audio_pos<(d_audio->dpos+len)){
		  break;
		}
		++d_audio->pack_no;
		priv->audio_block_no+=priv->audio_block_size ?
		    ((len+priv->audio_block_size-1)/priv->audio_block_size) : 1;
		d_audio->dpos+=len;
	    }
	  }
	  audio_chunk_pos=i;
	  skip_audio_bytes=curr_audio_pos-d_audio->dpos;

	  MSG_V("SEEK: i=%d (max:%d) dpos=%d (wanted:%d)  \n",
	      i,chunk_max,(int)d_audio->dpos,curr_audio_pos);

	} else {
	    // VBR audio
	    int chunks=(priv->avi_video_pts)*(float)sh_audio->audio.dwRate/(float)sh_audio->audio.dwScale;
	    audio_chunk_pos=0;

	// find audio chunk pos:
	  for(i=0;i<priv->idx_size && chunks>0;i++){
	    int id=((AVIINDEXENTRY *)priv->idx)[i].ckid;
	    if(avi_stream_id(id)==d_audio->id){
		len=((AVIINDEXENTRY *)priv->idx)[i].dwChunkLength;
		if(i>chunk_max){
		  skip_audio_bytes+=len;
		} else {
		  ++d_audio->pack_no;
		  priv->audio_block_no+=priv->audio_block_size ?
		    ((len+priv->audio_block_size-1)/priv->audio_block_size) : 1;
		  d_audio->dpos+=len;
		  audio_chunk_pos=i;
		}
//		--chunks;
		if(priv->audio_block_size)
		    chunks-=(len+priv->audio_block_size-1)/priv->audio_block_size;
	    }
	  }
	  //if(audio_chunk_pos>chunk_max) audio_chunk_pos=chunk_max;

	}

	// Now we have:
	//      audio_chunk_pos = chunk no in index table (it's <=chunk_max)
	//      skip_audio_bytes = bytes to be skipped after chunk seek
	//      d-audio->pack_no = chunk_no in stream at audio_chunk_pos
	//      d_audio->dpos = bytepos in stream at audio_chunk_pos
	// let's seek!

	  // update stream position:
	  d_audio->pos=audio_chunk_pos;
//          d_audio->dpos=apos;
//	  d_audio->pts=initial_pts_delay+(float)apos/(float)sh_audio->wf->nAvgBytesPerSec;

	if(!priv->nini){
	  // interleaved stream:
	  if(audio_chunk_pos<video_chunk_pos){
	    // calc priv->skip_video_frames & adjust video pts counter:
	    for(i=audio_chunk_pos;i<video_chunk_pos;i++){
	      int id=((AVIINDEXENTRY *)priv->idx)[i].ckid;
	      if(avi_stream_id(id)==d_video->id) ++priv->skip_video_frames;
	    }
	    // requires for correct audio pts calculation (demuxer):
	    priv->avi_video_pts-=priv->skip_video_frames*(float)sh_video->video.dwScale/(float)sh_video->video.dwRate;
	    priv->avi_audio_pts=priv->avi_video_pts;
	    // set index position:
	    priv->idx_pos_a=priv->idx_pos_v=priv->idx_pos=audio_chunk_pos;
	  }
	} else {
	    // non-interleaved stream:
	    priv->idx_pos_a=audio_chunk_pos;
	    priv->idx_pos_v=video_chunk_pos;
	    priv->idx_pos=(audio_chunk_pos<video_chunk_pos)?audio_chunk_pos:video_chunk_pos;
	}

	  MSG_V("SEEK: idx=%d  (a:%d v:%d)  v.skip=%d  a.skip=%d/%4.3f  \n",
	    (int)priv->idx_pos,audio_chunk_pos,video_chunk_pos,
	    (int)priv->skip_video_frames,skip_audio_bytes,skip_audio_secs);

	  if(skip_audio_bytes){
	    d_audio->read_data(NULL,skip_audio_bytes);
	    //d_audio->pts=0; // PTS is outdated because of the raw data skipping
	  }
      }
    d_video->pts=priv->avi_video_pts; // OSD
}

#define formtypeON2             mmioFOURCC('O', 'N', '2', 'f')
static MPXP_Rc avi_probe(Demuxer *demuxer)
{
  uint32_t riff,id;

  riff = demuxer->stream->read_dword_le();
  demuxer->stream->read_dword_le(); /*filesize */
  id=demuxer->stream->read_dword_le(); /* "AVI " */
  demuxer->file_format=Demuxer::Type_AVI;
  if(riff == mmioFOURCC('R','I','F','F') && id == formtypeAVI) return MPXP_Ok;
  if(riff == mmioFOURCC('O','N','2',' ') && id == formtypeON2) return MPXP_Ok;
  return MPXP_False;
}

static void avi_close(Demuxer *demuxer)
{
  avi_priv_t* priv=static_cast<avi_priv_t*>(demuxer->priv);

  if(!priv) return;

  delete priv;
}

static MPXP_Rc avi_control(const Demuxer *demuxer,int cmd,any_t*args)
{
    UNUSED(demuxer);
    UNUSED(cmd);
    UNUSED(args);
    return MPXP_Unknown;
}

static const mpxp_option_t avi_options[] = {
    {"noidx", &index_mode, CONF_TYPE_FLAG, 0, -1, 0, "disables INDEXES for AVI's demuxing"},
    {"idx", &index_mode, CONF_TYPE_FLAG, 0, -1, 1, "builds internal INDEXES of incomplete AVIs"},
    {"forceidx", &index_mode, CONF_TYPE_FLAG, 0, -1, 2, "forces rebuilding of INDEXES for broken AVIs"},
    { NULL, NULL, 0, 0, 0, 0, NULL}
};

static const mpxp_option_t avi_opts[] = {
    { "avi", (any_t*)&avi_options, CONF_TYPE_SUBCONFIG, 0, 0, 0, "AVI related options" },
    { NULL, NULL, 0, 0, 0, 0, NULL}
};

extern const demuxer_driver_t demux_avi =
{
    "avi",
    "AVI - Audio Video Interleaved parser",
    ".avi",
    avi_opts,
    avi_probe,
    avi_open,
    avi_demux,
    avi_seek,
    avi_close,
    avi_control
};
