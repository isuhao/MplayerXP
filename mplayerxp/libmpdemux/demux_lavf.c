/*
    Copyright (C) 2004 Michael Niedermayer <michaelni@gmx.at>

    This program is free software; you can redistribute it and/or modify
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
#include "../mp_config.h"

#include "stream.h"
#include "demuxer.h"
#include "stheader.h"
#include "help_mp.h"

#include "libavformat/avformat.h"
#include "libavformat/url.h"
#include "../libmpcodecs/codecs_ld.h"
#include "demux_msg.h"

#define PROBE_BUF_SIZE 2048

typedef struct lavf_priv_t{
    AVInputFormat *avif;
    AVFormatContext *avfc;
    AVIOContext *pb;
    URLContext *urlc;
    int audio_streams;
    int video_streams;
    int64_t last_pts;
}lavf_priv_t;

extern void print_wave_header(WAVEFORMATEX *h);
extern void print_video_header(BITMAPINFOHEADER *h);

static char *opt_format;
static char *opt_cryptokey;
extern int ts_prog;

const config_t lavf_opts[] = {
	{"format",    &(opt_format),    CONF_TYPE_STRING,       0,  0,       0, NULL, "forces format of lavf demuxer"},
	{"cryptokey", &(opt_cryptokey), CONF_TYPE_STRING,       0,  0,       0, NULL, "specifies cryptokey for lavf demuxer"},
	{NULL, NULL, 0, 0, 0, 0, NULL, NULL}
};

const config_t lavfdopts_conf[] = {
	{"lavf", &lavf_opts, CONF_TYPE_SUBCONFIG, 0, 0, 0, NULL, "LAVF-demuxer related options"},
	{NULL, NULL, 0, 0, 0, 0, NULL, NULL}
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

static int mpxp_open(URLContext *h, const char *filename, int flags){
    return 0;
}

static int mpxp_read(URLContext *h, unsigned char *buf, int size){
    stream_t *stream = (stream_t*)h->priv_data;
    int ret;

    if(stream_eof(stream)) //needed?
        return -1;
    ret=stream_read(stream, buf, size);

    MSG_DBG2("%d=mp_read(%p, %p, %d), eof:%d\n", ret, h, buf, size, stream->eof);
    return ret;
}

static int mpxp_write(URLContext *h,const unsigned char *buf, int size){
    return -1;
}

static int64_t mpxp_seek(URLContext *h, int64_t pos, int whence){
    stream_t *stream = (stream_t*)h->priv_data;

    MSG_DBG2("mpxp_seek(%p, %d, %d)\n", h, (int)pos, whence);
    if(whence == SEEK_CUR)
        pos +=stream_tell(stream);
    else if(whence == SEEK_END)
        pos += stream->end_pos;
    else if(whence != SEEK_SET)
        return -1;

    if(pos<stream->end_pos && stream->eof)
        stream_reset(stream);
    if(stream_seek(stream, pos)==0)
        return -1;

    return pos;
}

static int mpxp_close(URLContext *h){
    return 0;
}

static URLProtocol mp_protocol = {
    "mpxp",
    mpxp_open,
    NULL,      /* open2() */
    mpxp_read,
    mpxp_write,
    mpxp_seek,
    mpxp_close,
    NULL,
    NULL, /*int (*url_read_pause)(URLContext *h);*/
    NULL, /*int (*url_read_seek)(URLContext *h,*/
    NULL, /*int (*url_get_file_handle)(URLContext *h);*/
    NULL, /*int url_get_multi_file_handle(URLContext *h, int **handles, int *numhandles) */
    NULL, /*int (*url_shutdown)(URLContext *h, int flags);*/
    0,
    NULL, /* priv_data */
    0,    /* flags */
    NULL  /* (*url_check() */
};

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
    uint8_t *key = calloc(1,len);
    int i;
    avfc->keylen = len;
    avfc->key = key;
    for (i = 0; i < len; i++, str += 2)
        *key++ = (char2int(str[0]) << 4) | char2int(str[1]);
}

static int lavf_probe(demuxer_t *demuxer){
    AVProbeData avpd;
    uint8_t buf[PROBE_BUF_SIZE];
    lavf_priv_t *priv;
    if(!demuxer->priv)
        demuxer->priv=calloc(sizeof(lavf_priv_t),1);
    priv= demuxer->priv;

    av_register_all();

    if(stream_read(demuxer->stream, buf, PROBE_BUF_SIZE)!=PROBE_BUF_SIZE)
    {
	free(demuxer->priv);
        return 0;
    }
    avpd.filename= "xxx";
    avpd.buf= buf;
    avpd.buf_size= PROBE_BUF_SIZE;

    if (opt_format) {
        if (strcmp(opt_format, "help") == 0) {
           list_formats();
           return 0;
        }
        priv->avif= av_find_input_format(opt_format);
        if (!priv->avif) {
            MSG_FATAL("Unknown lavf format %s\n", opt_format);
            return 0;
        }
        MSG_INFO("Forced lavf %s demuxer\n", priv->avif->long_name);
        return 1;
    }
    priv->avif= av_probe_input_format(&avpd, 1);
    if(!priv->avif){
        MSG_V("LAVF_check: no clue about this gibberish!\n");
	free(demuxer->priv);
        return 0;
    }else
        MSG_V("LAVF_check: %s\n", priv->avif->long_name);
    demuxer->file_format=DEMUXER_TYPE_ASF;

    return 1;
}

extern const unsigned char ff_codec_bmp_tags[];
extern const unsigned char ff_codec_wav_tags[];
extern unsigned int ff_codec_get_tag(const void *tags, int id);

static int ffmpeg_int_cb(void *op) { return 0; } /* non interrupt blicking */
static AVIOInterruptCB int_cb = { ffmpeg_int_cb, NULL };

static demuxer_t* lavf_open(demuxer_t *demuxer){
    AVFormatContext *avfc;
    AVFormatParameters ap;
    const void *opt;
    lavf_priv_t *priv= demuxer->priv;
    int i,g;
    char mp_filename[256]="mpxp:";

    memset(&ap, 0, sizeof(AVFormatParameters));

    stream_seek(demuxer->stream, 0);

    ffurl_register_protocol(&mp_protocol,sizeof(URLProtocol));

    strncpy(mp_filename + 5, "foobar.dummy", sizeof(mp_filename)-5);

    if (opt_cryptokey)
        parse_cryptokey(avfc, opt_cryptokey);

    ffurl_open((URLContext**)&priv->pb->opaque, mp_filename, 0, &int_cb, NULL);

    ((URLContext*)(priv->pb->opaque))->priv_data= demuxer->stream;

    if(av_open_input_stream(&avfc, priv->pb, mp_filename, priv->avif, &ap)<0){
        MSG_ERR("LAVF_header: av_open_input_stream() failed\n");
        return NULL;
    }

    priv->avfc= avfc;

#if 0 /* TODO: switch to METADATA AVDictionary*/
    if(av_find_stream_info(avfc) < 0){
        MSG_ERR("LAVF_header: av_find_stream_info() failed\n");
        return NULL;
    }
    if(avfc->title    [0]) demux_info_add(demuxer, INFOT_NAME     , avfc->title    );
    if(avfc->author   [0]) demux_info_add(demuxer, INFOT_AUTHOR   , avfc->author   );
    if(avfc->copyright[0]) demux_info_add(demuxer, INFOT_COPYRIGHT, avfc->copyright);
    if(avfc->comment  [0]) demux_info_add(demuxer, INFOT_COMMENTS , avfc->comment  );
    if(avfc->album    [0]) demux_info_add(demuxer, INFOT_ALBUM    , avfc->album    );
    if(avfc->year        )
    {
			sprintf(mp_filename,"%u",avfc->year);
			demux_info_add(demuxer, INFOT_DATE    , mp_filename);
    }
    if(avfc->track       )
    {
			sprintf(mp_filename,"%u",avfc->track);
			demux_info_add(demuxer, INFOT_TRACK    , mp_filename);
    }
    if(avfc->genre    [0]) demux_info_add(demuxer, INFOT_GENRE    , avfc->genre    );
#endif
    for(i=0; i<avfc->nb_streams; i++){
        AVStream *st= avfc->streams[i];
#if LIBAVFORMAT_BUILD >= 4629
        AVCodecContext *codec= st->codec;
#else
        AVCodecContext *codec= &st->codec;
#endif

        switch(codec->codec_type){
        case AVMEDIA_TYPE_AUDIO:{
            WAVEFORMATEX *wf= calloc(sizeof(WAVEFORMATEX) + codec->extradata_size, 1);
            sh_audio_t* sh_audio=new_sh_audio(demuxer, i);
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
            sh_audio->format= codec->codec_tag;
            sh_audio->channels= codec->channels;
            sh_audio->samplerate= codec->sample_rate;
            sh_audio->i_bps= codec->bit_rate/8;
            switch (codec->codec_id) {
              case CODEC_ID_PCM_S8:
              case CODEC_ID_PCM_U8:
                sh_audio->samplesize = 1;
                break;
              case CODEC_ID_PCM_S16LE:
              case CODEC_ID_PCM_S16BE:
              case CODEC_ID_PCM_U16LE:
              case CODEC_ID_PCM_U16BE:
                sh_audio->samplesize = 2;
                break;
              case CODEC_ID_PCM_ALAW:
                sh_audio->format = 0x6;
                break;
              case CODEC_ID_PCM_MULAW:
                sh_audio->format = 0x7;
                break;
	      default:
                break;
            }
            if(verbose>=1) print_wave_header(sh_audio->wf);
            if(demuxer->audio->id != i && demuxer->audio->id != -1)
                st->discard= AVDISCARD_ALL;
            else{
                demuxer->audio->id = i;
                demuxer->audio->sh= demuxer->a_streams[i];
            }
            break;}
        case AVMEDIA_TYPE_VIDEO:{
            BITMAPINFOHEADER *bih=calloc(sizeof(BITMAPINFOHEADER) + codec->extradata_size,1);
            sh_video_t* sh_video=new_sh_video(demuxer, i);

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
            sh_video->disp_w= codec->width;
            sh_video->disp_h= codec->height;
            if (st->time_base.den) { /* if container has time_base, use that */
                sh_video->video.dwRate= st->time_base.den;
                sh_video->video.dwScale= st->time_base.num;
            } else {
#if LIBAVFORMAT_BUILD >= 4624
            sh_video->video.dwRate= codec->time_base.den;
            sh_video->video.dwScale= codec->time_base.num;
#else
            sh_video->video.dwRate= codec->frame_rate;
            sh_video->video.dwScale= codec->frame_rate_base;
#endif
            }
            sh_video->fps=av_q2d(st->r_frame_rate);
            sh_video->frametime=1/av_q2d(st->r_frame_rate);
            sh_video->format = bih->biCompression;
            sh_video->aspect=   codec->width * codec->sample_aspect_ratio.num 
                              / (float)(codec->height * codec->sample_aspect_ratio.den);
            sh_video->i_bps= codec->bit_rate/8;
            MSG_DBG2("aspect= %d*%d/(%d*%d)\n", 
                codec->width, codec->sample_aspect_ratio.num,
                codec->height, codec->sample_aspect_ratio.den);

            sh_video->ds= demuxer->video;
            if(codec->extradata_size)
                memcpy(sh_video->bih + 1, codec->extradata, codec->extradata_size);
            if(verbose>=1) print_video_header(sh_video->bih);
/*    short 	biPlanes;
    int  	biXPelsPerMeter;
    int  	biYPelsPerMeter;
    int 	biClrUsed;
    int 	biClrImportant;*/
            if(demuxer->video->id != i && demuxer->video->id != -1)
                st->discard= AVDISCARD_ALL;
            else{
                demuxer->video->id = i;
                demuxer->video->sh= demuxer->v_streams[i];
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

    return demuxer;
}

static int lavf_demux(demuxer_t *demux, demux_stream_t *dsds){
    lavf_priv_t *priv= demux->priv;
    AVPacket pkt;
    demux_packet_t *dp;
    demux_stream_t *ds;
    int id;
    MSG_DBG2("lavf_demux()\n");

    demux->filepos=stream_tell(demux->stream);

    if(stream_eof(demux->stream)){
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
            ds->sh=demux->a_streams[id];
            MSG_V("Auto-selected LAVF audio ID = %d\n",ds->id);
        }
    } else if(id==demux->video->id){
        // video
        ds=demux->video;
        if(!ds->sh){
            ds->sh=demux->v_streams[id];
            MSG_V("Auto-selected LAVF video ID = %d\n",ds->id);
        }
    } else {
        av_free_packet(&pkt);
        return 1;
    }

    if(0/*pkt.destruct == av_destruct_packet*/){
        //ok kids, dont try this at home :)
        dp=(demux_packet_t*)malloc(sizeof(demux_packet_t));
        dp->len=pkt.size;
        dp->next=NULL;
//        dp->refcount=1;
//        dp->master=NULL;
        dp->buffer=pkt.data;
        pkt.destruct= NULL;
    }else{
        dp=new_demux_packet(pkt.size);
        memcpy(dp->buffer, pkt.data, pkt.size);
        av_free_packet(&pkt);
    }

    if(pkt.pts != AV_NOPTS_VALUE){
#if LIBAVFORMAT_BUILD >= 4624
        dp->pts=pkt.pts * av_q2d(priv->avfc->streams[id]->time_base);
        priv->last_pts= dp->pts * AV_TIME_BASE;
#else
        priv->last_pts= pkt.pts;
        dp->pts=pkt.pts / (float)AV_TIME_BASE;
#endif
    }
    dp->pos=demux->filepos;
    dp->flags= (pkt.flags&AV_PKT_FLAG_KEY)?DP_KEYFRAME:DP_NONKEYFRAME;
    // append packet to DS stream:
    ds_add_packet(ds,dp);
    return 1;
}

static void lavf_seek(demuxer_t *demuxer, float rel_seek_secs, int flags){
    lavf_priv_t *priv = demuxer->priv;
    MSG_DBG2("lavf_demux(%p, %f, %d)\n", demuxer, rel_seek_secs, flags);

#if LIBAVFORMAT_BUILD < 4619
    av_seek_frame(priv->avfc, -1, priv->last_pts + rel_seek_secs*AV_TIME_BASE);
#else
    av_seek_frame(priv->avfc, -1, priv->last_pts + rel_seek_secs*AV_TIME_BASE, rel_seek_secs < 0 ? AVSEEK_FLAG_BACKWARD : 0);
#endif
}

static int lavf_control(demuxer_t *demuxer, int cmd, void *arg)
{
    return DEMUX_UNKNOWN;
}

static void lavf_close(demuxer_t *demuxer)
{
    lavf_priv_t* priv = demuxer->priv;
    if (priv){
        if(priv->avfc)
	{
	    av_close_input_file(priv->avfc); priv->avfc= NULL;
	}
	free(priv); demuxer->priv= NULL;
    }
}

demuxer_driver_t demux_lavf =
{
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

