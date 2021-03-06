#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace	usr;
/*
    Copyright (C) 2004 Michael Niedermayer <michaelni@gmx.at>

    This program is mp_free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/

#include <dlfcn.h> /* GLIBC specific. Exists under cygwin too! */
#include <stdlib.h>

#include "libmpstream2/stream.h"
#include "demuxer.h"
#include "demuxer_internal.h"
#include "stheader.h"
#include "mpxp_help.h"

#include "mpxp_conf_lavc.h"
#include "libmpcodecs/codecs_ld.h"
#include "libmpconf/cfgparser.h"
#include "aviprint.h"
#include "libao3/afmt.h"
#include "demux_msg.h"

static const int PROBE_BUF_SIZE=2048;
static const int BIO_BUFFER_SIZE=32768;

struct lavf_priv_t : public Opaque {
    public:
	lavf_priv_t() {}
	virtual ~lavf_priv_t();

	AVInputFormat *avif;
	AVFormatContext *avfc;
	AVIOContext *pb;
	uint8_t buffer[BIO_BUFFER_SIZE];
	int audio_streams;
	int video_streams;
	int64_t last_pts;
};

lavf_priv_t::~lavf_priv_t() {
    if(avfc) {
	av_freep(&avfc->key);
	avformat_close_input(&avfc);
    }
    av_freep(&pb);
}

static char *opt_format;
static char *opt_cryptokey;
extern int ts_prog;

static const mpxp_option_t lavf_opts[] = {
	{"format",    &(opt_format),    CONF_TYPE_STRING,       0,  0,       0, "forces format of lavf demuxer"},
	{"cryptokey", &(opt_cryptokey), CONF_TYPE_STRING,       0,  0,       0, "specifies cryptokey for lavf demuxer"},
	{NULL, NULL, 0, 0, 0, 0, NULL}
};

static const mpxp_option_t lavfdopts_conf[] = {
	{"lavf", (any_t*)&lavf_opts, CONF_TYPE_SUBCONFIG, 0, 0, 0, "LAVF-demuxer related options"},
	{NULL, NULL, 0, 0, 0, 0, NULL}
};

static int64_t mpxp_gcd(int64_t a, int64_t b){
    if(b) return mpxp_gcd(b, a%b);
    else  return a;
}

typedef struct __CodecTag
{
    uint32_t	id;
    uint32_t	tag;
}mpxpCodecTag;

static const mpxpCodecTag mp_wav_tags[] = {
    { CODEC_ID_ADPCM_4XM,         MKTAG('4', 'X', 'M', 'A')},
    { CODEC_ID_ADPCM_EA,          MKTAG('A', 'D', 'E', 'A')},
    { CODEC_ID_ADPCM_IMA_WS,      MKTAG('A', 'I', 'W', 'S')},
    { CODEC_ID_DSICINAUDIO,       MKTAG('D', 'C', 'I', 'A')},
    { CODEC_ID_INTERPLAY_DPCM,    MKTAG('I', 'N', 'P', 'A')},
    { CODEC_ID_PCM_S24BE,         MKTAG('i', 'n', '2', '4')},
    { CODEC_ID_PCM_S8,            MKTAG('t', 'w', 'o', 's')},
    { CODEC_ID_ROQ_DPCM,          MKTAG('R', 'o', 'Q', 'A')},
    { CODEC_ID_SHORTEN,           MKTAG('s', 'h', 'r', 'n')},
    { CODEC_ID_TTA,               MKTAG('T', 'T', 'A', '1')},
    { CODEC_ID_WAVPACK,           MKTAG('W', 'V', 'P', 'K')},
    { CODEC_ID_WESTWOOD_SND1,     MKTAG('S', 'N', 'D', '1')},
    { CODEC_ID_XAN_DPCM,          MKTAG('A', 'x', 'a', 'n')},
    { 0, 0 },
};

static const mpxpCodecTag mp_bmp_tags[] = {
    { CODEC_ID_DSICINVIDEO,       MKTAG('D', 'C', 'I', 'V')},
    { CODEC_ID_FLIC,              MKTAG('F', 'L', 'I', 'C')},
    { CODEC_ID_IDCIN,             MKTAG('I', 'D', 'C', 'I')},
    { CODEC_ID_INTERPLAY_VIDEO,   MKTAG('I', 'N', 'P', 'V')},
    { CODEC_ID_ROQ,               MKTAG('R', 'o', 'Q', 'V')},
    { CODEC_ID_TIERTEXSEQVIDEO,   MKTAG('T', 'S', 'E', 'Q')},
    { CODEC_ID_VMDVIDEO,          MKTAG('V', 'M', 'D', 'V')},
    { CODEC_ID_WS_VQA,            MKTAG('V', 'Q', 'A', 'V')},
    { CODEC_ID_XAN_WC3,           MKTAG('W', 'C', '3', 'V')},
    { 0, 0 },
};


static unsigned int mpxp_codec_get_tag(const mpxpCodecTag *tags, uint32_t id)
{
    while (tags->id != CODEC_ID_NONE) {
	if (tags->id == id)
	    return tags->tag;
	tags++;
    }
    return 0;
}

static int mpxp_read(any_t*opaque, unsigned char *buf, int size){
    Stream* stream=reinterpret_cast<Stream*>(opaque);
    int ret;

    if(stream->eof()) //needed?
	return -1;
    binary_packet bp=stream->read(size);
    memcpy(buf,bp.data(),bp.size());

    MSG_DBG2("%d=mp_read(%p, %p, %d), eof:%d\n", bp.size(), stream, buf, size, stream->eof());
    return bp.size();
}

static int64_t mpxp_seek(any_t*opaque, int64_t pos, int whence){
    Stream* stream=reinterpret_cast<Stream*>(opaque);
    MSG_DBG2("mpxp_seek(%p, %d, %d)\n", stream, (int)pos, whence);
    if(whence == SEEK_CUR)
	pos +=stream->tell();
    else if(whence == SEEK_END)
	pos += stream->end_pos();
    else if(whence != SEEK_SET)
	return -1;

    if(pos<stream->end_pos() && stream->eof())
	stream->reset();
    if(stream->seek( pos)==0)
	return -1;

    return pos;
}

static void list_formats(void) {
    AVInputFormat *fmt=NULL;
    MSG_INFO("Available lavf input formats:\n");
    for (fmt = av_iformat_next(fmt); fmt;)
	MSG_INFO( "%15s : %s\n", fmt->name, fmt->long_name);
}

static uint8_t char2int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static void parse_cryptokey(AVFormatContext *avfc, const char *str) {
    int len = strlen(str) / 2;
    uint8_t *key = new(zeromem) uint8_t[len];
    int i;
    avfc->keylen = len;
    avfc->key = key;
    for (i = 0; i < len; i++, str += 2)
	*key++ = (char2int(str[0]) << 4) | char2int(str[1]);
}

static MPXP_Rc lavf_probe(Demuxer *demuxer){
    AVProbeData avpd;
    uint8_t buf[PROBE_BUF_SIZE];
    lavf_priv_t *priv;
    priv=new(zeromem) lavf_priv_t;
    demuxer->priv=priv;

    av_register_all();
    if(mp_conf.verbose>1) av_log_set_level(AV_LOG_DEBUG);
    else if(mp_conf.verbose) av_log_set_level(AV_LOG_VERBOSE);
    else av_log_set_level(AV_LOG_INFO);

    binary_packet bp=demuxer->stream->read(PROBE_BUF_SIZE);
    if(bp.size()!=PROBE_BUF_SIZE) {
	delete demuxer->priv;
	return MPXP_False;
    }
    memcpy(buf,bp.data(),bp.size());
    avpd.filename= "xxx";
    avpd.buf= buf;
    avpd.buf_size= PROBE_BUF_SIZE;

    if (opt_format) {
	if (strcmp(opt_format, "help") == 0) {
	   list_formats();
	   return MPXP_False;
	}
	priv->avif= av_find_input_format(opt_format);
	if (!priv->avif) {
	    MSG_FATAL("Unknown lavf format %s\n", opt_format);
	    return MPXP_False;
	}
	MSG_INFO("Forced lavf %s demuxer\n", priv->avif->long_name);
	return MPXP_Ok;
    }
    priv->avif= av_probe_input_format(&avpd, 1);
    if(!priv->avif){
	MSG_V("LAVF_check: file format not recognized!\n");
	delete demuxer->priv;
	return MPXP_False;
    }else
	MSG_V("LAVF_check: %s\n", priv->avif->long_name);
    demuxer->file_format=Demuxer::Type_ASF;

    return MPXP_Ok;
}

static Opaque* lavf_open(Demuxer *demuxer){
    AVFormatContext *avfc;
    lavf_priv_t *priv= static_cast<lavf_priv_t*>(demuxer->priv);
    unsigned j;
    int err,i,g;
    char mp_filename[256]="mpxp:";
    char err_buff[256];
    demuxer->stream->seek( 0);

    strncpy(mp_filename + 5, "foobar.dummy", sizeof(mp_filename)-5);

    avfc = avformat_alloc_context();

    if (opt_cryptokey)
	parse_cryptokey(avfc, opt_cryptokey);

    priv->pb = avio_alloc_context(priv->buffer, BIO_BUFFER_SIZE, 0,
				demuxer->stream, mpxp_read, NULL/*mpxp_write*/, mpxp_seek);
    avfc->pb = priv->pb;

    if((err=avformat_open_input(&avfc, mp_filename, priv->avif, NULL))<0){
	av_strerror(err,err_buff,sizeof(err_buff));
	MSG_ERR("LAVF_header: avformat_open_input() failed: %s\n",err_buff);
	return NULL;
    }

    priv->avfc= avfc;

    if(av_find_stream_info(avfc) < 0){
	MSG_ERR("LAVF_header: av_find_stream_info() failed\n");
	return NULL;
    }

    AVDictionaryEntry *tag = NULL;
    while((tag = av_dict_get(avfc->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))) {
	if(strcmp(tag->key,"title")==0)    demuxer->info().add(INFOT_NAME,tag->value);
	else if(strcmp(tag->key,"author")==0)demuxer->info().add(INFOT_AUTHOR,tag->value);
	else if(strcmp(tag->key,"copyright")==0)demuxer->info().add(INFOT_COPYRIGHT,tag->value);
	else if(strcmp(tag->key,"comment")==0) demuxer->info().add(INFOT_COMMENTS,tag->value);
	else if(strcmp(tag->key,"album")==0) demuxer->info().add(INFOT_ALBUM,tag->value);
	else if(strcmp(tag->key,"genre")==0) demuxer->info().add(INFOT_GENRE,tag->value);
	else if(strcmp(tag->key,"track")==0) demuxer->info().add(INFOT_TRACK,tag->value);
	else if(strcmp(tag->key,"date")==0) demuxer->info().add(INFOT_DATE,tag->value);
    }

    for(j=0; j<avfc->nb_streams; j++){
	AVStream *st= avfc->streams[j];
	AVCodecContext *codec= st->codec;
	i=j;
	switch(codec->codec_type){
	case AVMEDIA_TYPE_AUDIO:{
	    WAVEFORMATEX *wf= (WAVEFORMATEX*)mp_calloc(sizeof(WAVEFORMATEX) + codec->extradata_size, 1);
	    sh_audio_t* sh_audio=demuxer->new_sh_audio(i);
	    priv->audio_streams++;
	    if(!codec->codec_tag)
		codec->codec_tag= ff_codec_get_tag(ff_codec_wav_tags,codec->codec_id);
	    if(!codec->codec_tag)
		codec->codec_tag= mpxp_codec_get_tag(mp_wav_tags, codec->codec_id);
	    wf->wFormatTag= codec->codec_tag;
	    wf->nChannels= codec->channels;
	    wf->nSamplesPerSec= codec->sample_rate;
	    wf->nAvgBytesPerSec= codec->bit_rate/8;
	    wf->nBlockAlign= codec->block_align;
	    wf->wBitsPerSample= codec->bits_per_coded_sample;
	    wf->cbSize= codec->extradata_size;
	    if(codec->extradata_size){
		memcpy(
		    wf + 1,
		    codec->extradata,
		    codec->extradata_size);
	    }
	    sh_audio->wf= wf;
	    sh_audio->audio.dwSampleSize= codec->block_align;
	    if(codec->frame_size && codec->sample_rate){
		sh_audio->audio.dwScale=codec->frame_size;
		sh_audio->audio.dwRate= codec->sample_rate;
	    }else{
		sh_audio->audio.dwScale= codec->block_align ? codec->block_align*8 : 8;
		sh_audio->audio.dwRate = codec->bit_rate;
	    }
	    g= mpxp_gcd(sh_audio->audio.dwScale, sh_audio->audio.dwRate);
	    sh_audio->audio.dwScale /= g;
	    sh_audio->audio.dwRate  /= g;
//            printf("sca:%d rat:%d fs:%d sr:%d ba:%d\n", sh_audio->audio.dwScale, sh_audio->audio.dwRate, codec->frame_size, codec->sample_rate, codec->block_align);
	    sh_audio->ds= demuxer->audio;
	    sh_audio->wtag= codec->codec_tag;
	    sh_audio->nch= codec->channels;
	    sh_audio->rate= codec->sample_rate;
	    sh_audio->i_bps= codec->bit_rate/8;
	    switch (codec->codec_id) {
	      case CODEC_ID_PCM_S8:
	      case CODEC_ID_PCM_U8:
		sh_audio->afmt=bps2afmt(1);
		break;
	      case CODEC_ID_PCM_S16LE:
	      case CODEC_ID_PCM_S16BE:
	      case CODEC_ID_PCM_U16LE:
	      case CODEC_ID_PCM_U16BE:
		sh_audio->afmt=bps2afmt(2);
		break;
	      case CODEC_ID_PCM_ALAW:
		sh_audio->wtag = 0x6;
		break;
	      case CODEC_ID_PCM_MULAW:
		sh_audio->wtag = 0x7;
		break;
	      default:
		break;
	    }
	    if(mp_conf.verbose>=1) print_wave_header(sh_audio->wf,wf->cbSize);
	    if(demuxer->audio->id != i && demuxer->audio->id != -1)
		st->discard= AVDISCARD_ALL;
	    else{
		demuxer->audio->id = i;
		demuxer->audio->sh= demuxer->get_sh_audio(i);
	    }
	    break;}
	case AVMEDIA_TYPE_VIDEO:{
	    BITMAPINFOHEADER *bih=(BITMAPINFOHEADER*)mp_calloc(sizeof(BITMAPINFOHEADER) + codec->extradata_size,1);
	    sh_video_t* sh_video=demuxer->new_sh_video(i);

	    priv->video_streams++;
	    if(!codec->codec_tag)
		codec->codec_tag= ff_codec_get_tag(ff_codec_bmp_tags,codec->codec_id);
	    if(!codec->codec_tag)
		codec->codec_tag= mpxp_codec_get_tag(mp_bmp_tags, codec->codec_id);
	    bih->biSize= sizeof(BITMAPINFOHEADER) + codec->extradata_size;
	    bih->biWidth= codec->width;
	    bih->biHeight= codec->height;
	    bih->biBitCount= codec->bits_per_coded_sample;
	    bih->biSizeImage = bih->biWidth * bih->biHeight * bih->biBitCount/8;
	    bih->biCompression= codec->codec_tag;
	    sh_video->bih= bih;
	    sh_video->src_w= codec->width;
	    sh_video->src_h= codec->height;
	    if (st->time_base.den) { /* if container has time_base, use that */
		sh_video->video.dwRate= st->time_base.den;
		sh_video->video.dwScale= st->time_base.num;
	    } else {
		sh_video->video.dwRate= codec->time_base.den;
		sh_video->video.dwScale= codec->time_base.num;
	    }
	    sh_video->fps=av_q2d(st->r_frame_rate);
	    sh_video->fourcc= bih->biCompression;
	    sh_video->aspect=   codec->width * codec->sample_aspect_ratio.num
			      / (float)(codec->height * codec->sample_aspect_ratio.den);
	    MSG_DBG2("aspect= %d*%d/(%d*%d)\n",
		codec->width, codec->sample_aspect_ratio.num,
		codec->height, codec->sample_aspect_ratio.den);

	    sh_video->ds= demuxer->video;
	    if(codec->extradata_size)
		memcpy(sh_video->bih + 1, codec->extradata, codec->extradata_size);
	    if(mp_conf.verbose>=1) print_video_header(sh_video->bih,bih->biSize);
/*    short 	biPlanes;
    int  	biXPelsPerMeter;
    int  	biYPelsPerMeter;
    int 	biClrUsed;
    int 	biClrImportant;*/
	    if(demuxer->video->id != i && demuxer->video->id != -1)
		st->discard= AVDISCARD_ALL;
	    else{
		demuxer->video->id = i;
		demuxer->video->sh= demuxer->get_sh_video(i);
	    }
	    break;}
	default:
	    st->discard= AVDISCARD_ALL;
	}
    }

    MSG_V("LAVF: %d audio and %d video streams found\n",priv->audio_streams,priv->video_streams);
    MSG_V("LAVF: build %d\n", LIBAVFORMAT_BUILD);
    if(!priv->audio_streams) demuxer->audio->id=-2;  // nosound
//    else if(best_audio > 0 && demuxer->audio->id == -1) demuxer->audio->id=best_audio;
    if(!priv->video_streams){
	if(!priv->audio_streams){
	    MSG_ERR("LAVF: no audio or video headers found - broken file?\n");
	    return NULL;
	}
	demuxer->video->id=-2; // audio-only
    } //else if (best_video > 0 && demuxer->video->id == -1) demuxer->video->id = best_video;
    check_pin("demuxer",demuxer->pin,DEMUX_PIN);
    return priv;
}

static int lavf_demux(Demuxer *demux, Demuxer_Stream *dsds){
    UNUSED(dsds);
    lavf_priv_t *priv= static_cast<lavf_priv_t*>(demux->priv);
    AVPacket pkt;
    Demuxer_Packet *dp;
    Demuxer_Stream *ds;
    int id;
    MSG_DBG2("lavf_demux()\n");

    demux->filepos=demux->stream->tell();

    if(demux->stream->eof()){
//        demuxre->stream->eof=1;
	return 0;
    }

    if(av_read_frame(priv->avfc, &pkt) < 0)
	return 0;

    id= pkt.stream_index;

    if(id==demux->audio->id){
	// audio
	ds=demux->audio;
	if(!ds->sh){
	    ds->sh=demux->get_sh_audio(id);
	    MSG_V("Auto-selected LAVF audio ID = %d\n",ds->id);
	}
    } else if(id==demux->video->id){
	// video
	ds=demux->video;
	if(!ds->sh){
	    ds->sh=demux->get_sh_video(id);
	    MSG_V("Auto-selected LAVF video ID = %d\n",ds->id);
	}
    } else {
	av_free_packet(&pkt);
	return 1;
    }

    dp=new(zeromem) Demuxer_Packet(pkt.size);
    memcpy(dp->buffer(), pkt.data, pkt.size);
    av_free_packet(&pkt);

    if(pkt.pts != AV_NOPTS_VALUE){
	dp->pts=pkt.pts * av_q2d(priv->avfc->streams[id]->time_base);
	priv->last_pts= dp->pts * AV_TIME_BASE;
    }
    dp->pos=demux->filepos;
    dp->flags= (pkt.flags&AV_PKT_FLAG_KEY)?DP_KEYFRAME:DP_NONKEYFRAME;
    // append packet to DS stream:
    ds->add_packet(dp);
    return 1;
}

static void lavf_seek(Demuxer *demuxer,const seek_args_t* seeka){
    lavf_priv_t *priv = static_cast<lavf_priv_t*>(demuxer->priv);
    MSG_DBG2("lavf_demux(%p, %f, %d)\n", demuxer, seeka->secs, seeka->flags);

    av_seek_frame(priv->avfc, -1, priv->last_pts + seeka->secs*AV_TIME_BASE, seeka->secs < 0 ? AVSEEK_FLAG_BACKWARD : 0);
}

static MPXP_Rc lavf_control(const Demuxer *demuxer, int cmd, any_t*arg)
{
    UNUSED(demuxer);
    UNUSED(cmd);
    UNUSED(arg);
    return MPXP_Unknown;
}

static void lavf_close(Demuxer *demuxer)
{
    lavf_priv_t* priv = static_cast<lavf_priv_t*>(demuxer->priv);
    if (priv){
	delete priv;
	demuxer->priv=NULL;
    }
}

extern const demuxer_driver_t demux_lavf =
{
    "lavf",
    "libavformat - supports many formats, requires libavformat",
    ".xxx",
    lavfdopts_conf,
    lavf_probe,
    lavf_open,
    lavf_demux,
    lavf_seek,
    lavf_close,
    lavf_control
};

