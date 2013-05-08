#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace	usr;
/*
	FLI file parser for the MPlayer program
	by Mike Melanson

	TODO: demuxer->movi_length
	TODO: DP_KEYFRAME
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "mpxp_help.h"

#include "libmpstream2/stream.h"
#include "demuxer.h"
#include "demuxer_internal.h"
#include "stheader.h"
#include "demux_msg.h"

struct fli_frames_t : public Opaque {
    public:
	fli_frames_t() {}
	virtual ~fli_frames_t();

	int num_frames;
	int current_frame;
	off_t *filepos;
	unsigned int *frame_size;
};

fli_frames_t::~fli_frames_t() {
    if(filepos) delete filepos;
    if(frame_size) delete frame_size;
}

static void fli_seek(Demuxer *demuxer,const seek_args_t* seeka){
  fli_frames_t *frames = static_cast<fli_frames_t*>(demuxer->priv);
  sh_video_t *sh_video = reinterpret_cast<sh_video_t*>(demuxer->video->sh);
  int newpos=(seeka->flags&DEMUX_SEEK_SET)?0:frames->current_frame;
  newpos+=seeka->secs*(seeka->flags&DEMUX_SEEK_PERCENTS?frames->num_frames:sh_video->fps);
  if(newpos<0) newpos=0; else
  if(newpos>frames->num_frames) newpos=frames->num_frames;
  frames->current_frame=newpos;
}

// return value:
//     0 = EOF or no stream found
//     1 = successfully read a packet
static int fli_demux(Demuxer *demuxer,Demuxer_Stream *__ds){
  fli_frames_t *frames = static_cast<fli_frames_t*>(demuxer->priv);
  sh_video_t *sh_video = reinterpret_cast<sh_video_t*>(demuxer->video->sh);

  // see if the end has been reached
  if (frames->current_frame >= frames->num_frames)
    return 0;

  // fetch the frame from the file
  // first, position the file properly since ds_read_packet() doesn't
  // seem to do it, even though it takes a file offset as a parameter
  demuxer->stream->seek( frames->filepos[frames->current_frame]);
  demuxer->video->read_packet(demuxer->stream,
			    frames->frame_size[frames->current_frame],
			    frames->current_frame/sh_video->fps,
			    frames->filepos[frames->current_frame],
			    DP_NONKEYFRAME /* what flags? -> demuxer.h (alex) */
  );

  // get the next frame ready
  frames->current_frame++;

  return 1;
}

static MPXP_Rc fli_probe(Demuxer* demuxer){
  unsigned magic_number;
  demuxer->movi_end = demuxer->stream->skip(4);
  magic_number = demuxer->stream->read_word_le();
  if ((magic_number != 0xAF11) && (magic_number != 0xAF12)) return MPXP_False;
  demuxer->file_format=Demuxer::Type_FLI;
  return MPXP_Ok;
}

static Opaque* fli_open(Demuxer* demuxer){
  sh_video_t *sh_video = NULL;
  fli_frames_t *frames = new(zeromem) fli_frames_t;
  int frame_number;
  int speed;
  unsigned int frame_size;
  int magic_number;
  unsigned char * header;

  // go back to the beginning
  demuxer->stream->reset( );
  demuxer->stream->seek( 0);

  header = new unsigned char[sizeof(BITMAPINFOHEADER) + 128];
  demuxer->stream->read( header + sizeof(BITMAPINFOHEADER), 128);
  demuxer->stream->seek( 0);

  demuxer->movi_start = 128;
  demuxer->movi_end = demuxer->stream->read_dword_le();

  magic_number = demuxer->stream->read_word_le();

  if ((magic_number != 0xAF11) && (magic_number != 0xAF12))
  {
    MSG_ERR("Bad/unknown magic number (%04x)\n",
	magic_number);
    delete header;
    delete frames;
    return NULL;
  }

  // fetch the number of frames
  frames->num_frames = demuxer->stream->read_word_le();
  frames->current_frame = 0;

  // allocate enough entries for the indices
  frames->filepos = (off_t *)mp_malloc(frames->num_frames * sizeof(off_t));
  frames->frame_size = new unsigned int [frames->num_frames];

  // create a new video stream header
  sh_video = demuxer->new_sh_video();

  // make sure the demuxer knows about the new video stream header
  // (even though new_sh_video() ought to take care of it)
  demuxer->video->sh = sh_video;

  // make sure that the video demuxer stream header knows about its
  // parent video demuxer stream (this is getting wacky), or else
  // video_read_properties() will choke
  sh_video->ds = demuxer->video;

  // custom fourcc for internal MPlayer use
  sh_video->fourcc = mmioFOURCC('F', 'L', 'I', 'C');

  sh_video->src_w = demuxer->stream->read_word_le();
  sh_video->src_h = demuxer->stream->read_word_le();

  // pass extradata to codec
  sh_video->bih = (BITMAPINFOHEADER*)header;
  sh_video->bih->biSize = sizeof(BITMAPINFOHEADER) + 128;
  sh_video->bih->biCompression=sh_video->fourcc;
  sh_video->bih->biWidth=sh_video->src_w;
  sh_video->bih->biPlanes=0;
  sh_video->bih->biBitCount=0; /* depth */
  sh_video->bih->biHeight=sh_video->src_h;
  sh_video->bih->biSizeImage=sh_video->bih->biWidth*sh_video->bih->biHeight;
  // skip the video depth and flags
  demuxer->stream->skip( 4);

  // get the speed
  speed = demuxer->stream->read_word_le();
  if (speed == 0)
    speed = 1;
  if (magic_number == 0xAF11)
    speed *= 1000.0f/70.0f;
  sh_video->fps = 1000.0f / speed;

  // build the frame index
  demuxer->stream->seek( demuxer->movi_start);
  frame_number = 0;
  while ((!demuxer->stream->eof()) && (frame_number < frames->num_frames))
  {
    frames->filepos[frame_number] = demuxer->stream->tell();
    frame_size = demuxer->stream->read_dword_le();
    magic_number = demuxer->stream->read_word_le();
    demuxer->stream->skip( frame_size - 6);

    // if this chunk has the right magic number, index it
    if ((magic_number == 0xF1FA) || (magic_number == 0xF5FA))
    {
      frames->frame_size[frame_number] = frame_size;
      frame_number++;
    }
  }

    // save the actual number of frames indexed
    frames->num_frames = frame_number;

    check_pin("demuxer",demuxer->pin,DEMUX_PIN);
    return frames;
}

static void fli_close(Demuxer* demuxer) {
    fli_frames_t *frames = static_cast<fli_frames_t*>(demuxer->priv);

    if(!frames) return;

    delete frames;
}

static MPXP_Rc fli_control(const Demuxer *demuxer,int cmd,any_t*args)
{
    UNUSED(demuxer);
    UNUSED(cmd);
    UNUSED(args);
    return MPXP_Unknown;
}

extern const demuxer_driver_t demux_fli =
{
    "fli",
    "FLI parser",
    ".fli",
    NULL,
    fli_probe,
    fli_open,
    fli_demux,
    fli_seek,
    fli_close,
    fli_control
};
