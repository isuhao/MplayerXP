#include "mp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;
/*
  Y4M file parser by Rik Snel (using yuv4mpeg*.[ch] from
  mjpeg.sourceforge.net) (derived from demux_viv.c)
  older YUV4MPEG (used by xawtv) support by Alex Beregszaszi

    TODO: demuxer->movi_length
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h> /* strtok */

#include "help_mp.h"
#include "yuv4mpeg.h"

//#include "stream.h"
#include "libmpstream2/stream.h"
#include "demuxer.h"
#include "demuxer_internal.h"
#include "stheader.h"
#include "osdep/bswap.h"
#include "demux_msg.h"

struct y4m_priv_t : public Opaque {
    public:
	y4m_priv_t() {}
	virtual ~y4m_priv_t();

	int framenum;
	y4m_stream_info_t* si;
	int is_older;
};

y4m_priv_t::~y4m_priv_t() {
    if (!is_older)
	y4m_fini_stream_info(si);
    delete si;
}

static MPXP_Rc y4m_probe(Demuxer* demuxer){
    int orig_pos = demuxer->stream->tell();
    char buf[10];
    y4m_priv_t* priv;

    MSG_V( "Checking for YUV4MPEG2\n");

    demuxer->stream->read( buf, 9);
    buf[9] = 0;

    if (strncmp("YUV4MPEG2", buf, 9) && strncmp("YUV4MPEG ", buf, 9)) {
	MSG_DBG2( "Failed: YUV4MPEG2\n");
	return MPXP_False;
    }

    priv = new(zeromem) y4m_priv_t;
    demuxer->priv = priv;

    priv->is_older = 0;

    if (!strncmp("YUV4MPEG ", buf, 9)) {
	MSG_V( "Found older YUV4MPEG format (used by xawtv)\n");
	priv->is_older = 1;
    }

    MSG_DBG2("Success: YUV4MPEG2\n");

    demuxer->stream->seek( orig_pos);
    demuxer->file_format=Demuxer::Type_Y4M;
    return MPXP_Ok;
}


// return value:
//     0 = EOF or no stream found
//     1 = successfully read a packet
static int y4m_demux(Demuxer *demux,Demuxer_Stream *__ds) {
    UNUSED(__ds);
  Demuxer_Stream *ds=demux->video;
  Demuxer_Packet *dp;
  y4m_priv_t *priv=static_cast<y4m_priv_t*>(demux->priv);
  y4m_frame_info_t fi;
  unsigned char *buf[3];
  int err, size;

  demux->filepos=demux->stream->tell();

  size = ((sh_video_t*)ds->sh)->src_w*((sh_video_t*)ds->sh)->src_h;

  dp = new(zeromem) Demuxer_Packet(3*size/2);

  /* swap U and V components */
  buf[0] = dp->buffer();
  buf[1] = dp->buffer() + 5*size/4;
  buf[2] = dp->buffer() + size;

  if (priv->is_older)
  {
    int c;

    c = demux->stream->read_char(); /* F */
    if (c == -256)
	return 0; /* EOF */
    if (c != 'F')
    {
	MSG_V( "Bad frame at %d\n", (int)demux->stream->tell()-1);
	return 0;
    }
    demux->stream->skip( 5); /* RAME\n */
    demux->stream->read( buf[0], size);
    demux->stream->read( buf[1], size/4);
    demux->stream->read( buf[2], size/4);
  }
  else
  {
    if ((err=y4m_read_frame(*demux->stream, priv->si, &fi, buf)) != Y4M_OK) {
      MSG_V( "error reading frame %s\n", y4m_strerr(err));
      return 0;
    }
  }

  /* This seems to be the right way to calculate the presentation time stamp */
  dp->pts=(float)priv->framenum/((sh_video_t*)ds->sh)->fps;
  priv->framenum++;
  dp->pos=demux->filepos;
  dp->flags=DP_KEYFRAME; // every frame is keyframe
  ds->add_packet(dp);

  return 1;
}

static Opaque* y4m_open(Demuxer* demuxer){
    y4m_priv_t* priv = static_cast<y4m_priv_t*>(demuxer->priv);
    y4m_ratio_t ratio;
    sh_video_t* sh=demuxer->new_sh_video();
    int err;

    priv->framenum = 0;
    priv->si = new(zeromem) y4m_stream_info_t;

    if (priv->is_older)
    {
	char buf[4];
	int frame_rate_code;

	demuxer->stream->skip( 8); /* YUV4MPEG */
	demuxer->stream->skip( 1); /* space */
	demuxer->stream->read( (char *)&buf[0], 3);
	buf[3] = 0;
	sh->src_w = atoi(buf);
	demuxer->stream->skip( 1); /* space */
	demuxer->stream->read( (char *)&buf[0], 3);
	buf[3] = 0;
	sh->src_h = atoi(buf);
	demuxer->stream->skip( 1); /* space */
	demuxer->stream->read( (char *)&buf[0], 1);
	buf[1] = 0;
	frame_rate_code = atoi(buf);
	demuxer->stream->skip( 1); /* new-line */

	if (!sh->fps)
	{
	    /* values from xawtv */
	    switch(frame_rate_code)
	    {
		case 1:
		    sh->fps = 23.976f;
		    break;
		case 2:
		    sh->fps = 24.0f;
		    break;
		case 3:
		    sh->fps = 25.0f;
		    break;
		case 4:
		    sh->fps = 29.97f;
		    break;
		case 5:
		    sh->fps = 30.0f;
		    break;
		case 6:
		    sh->fps = 50.0f;
		    break;
		case 7:
		    sh->fps = 59.94f;
		    break;
		case 8:
		    sh->fps = 60.0f;
		    break;
		default:
		    sh->fps = 25.0f;
	    }
	}
    }
    else
    {
	y4m_init_stream_info(priv->si);
	if ((err=y4m_read_stream_header(*demuxer->stream, priv->si)) != Y4M_OK)
	    MSG_FATAL( "error parsing YUV4MPEG header: %s\n", y4m_strerr(err));

	if(!sh->fps) {
	    ratio = y4m_si_get_framerate(priv->si);
	    if (ratio.d != 0)
		sh->fps=(float)ratio.n/(float)ratio.d;
	    else
		sh->fps=15.0f;
	}

	ratio = y4m_si_get_sampleaspect(priv->si);
	if (ratio.d != 0 && ratio.n != 0)
	    sh->aspect = (float)ratio.n/(float)ratio.d;

	sh->src_w = y4m_si_get_width(priv->si);
	sh->src_h = y4m_si_get_height(priv->si);
	demuxer->flags &= ~Demuxer::Seekable;
    }

    sh->fourcc = mmioFOURCC('Y', 'V', '1', '2');

    sh->bih=new(zeromem) BITMAPINFOHEADER;
    sh->bih->biSize=40;
    sh->bih->biWidth = sh->src_w;
    sh->bih->biHeight = sh->src_h;
    sh->bih->biPlanes=3;
    sh->bih->biBitCount=12;
    sh->bih->biCompression=sh->fourcc;
    sh->bih->biSizeImage=sh->bih->biWidth*sh->bih->biHeight*3/2; /* YV12 */

    demuxer->video->sh=sh;
    sh->ds=demuxer->video;
    demuxer->video->id=0;


    MSG_V( "YUV4MPEG2 Video stream %d size: display: %dx%d, codec: %ux%u\n",
	    demuxer->video->id, sh->src_w, sh->src_h, sh->bih->biWidth,
	    sh->bih->biHeight);
    check_pin("demuxer",demuxer->pin,DEMUX_PIN);
    return priv;
}

static void y4m_seek(Demuxer *demuxer,const seek_args_t* seeka) {
    sh_video_t* sh = reinterpret_cast<sh_video_t*>(demuxer->video->sh);
    y4m_priv_t* priv = static_cast<y4m_priv_t*>(demuxer->priv);
    int rel_seek_frames = sh->fps*seeka->secs;
    int size = 3*sh->src_w*sh->src_h/2;
    off_t curr_pos = demuxer->stream->tell();

    if (priv->framenum + rel_seek_frames < 0) rel_seek_frames = -priv->framenum;

    priv->framenum += rel_seek_frames;

    if (priv->is_older) {
	/* Well this is easy: every frame takes up size+6 bytes
	 * in the stream and we may assume that the stream pointer
	 * is always at the beginning of a frame.
	 * framenum is the number of the frame that is about to be
	 * demuxed (counting from ONE (see demux_open_y4m)) */
	demuxer->stream->seek( curr_pos + rel_seek_frames*(size+6));
    } else {
	    /* should never come here, because seeking for YUV4MPEG2
	     * is disabled. */
	    MSG_WARN( "Seeking for YUV4MPEG2 not yet implemented!\n");
    }
}

static void y4m_close(Demuxer *demuxer)
{
    y4m_priv_t* priv = static_cast<y4m_priv_t*>(demuxer->priv);

    if(!priv) return;
    delete priv;
    return;
}

static MPXP_Rc y4m_control(const Demuxer *demuxer,int cmd,any_t*args)
{
    UNUSED(demuxer);
    UNUSED(cmd);
    UNUSED(args);
    return MPXP_Unknown;
}

extern const demuxer_driver_t demux_y4m =
{
    "y4m",
    "YUV4MPEG2 parser",
    ".y4m",
    NULL,
    y4m_probe,
    y4m_open,
    y4m_demux,
    y4m_seek,
    y4m_close,
    y4m_control
};
