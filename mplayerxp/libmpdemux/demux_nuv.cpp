#include "mp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;
/*
 * NuppelVideo 0.05 file parser
 * for MPlayer
 * by Panagiotis Issaris <takis@lumumba.luc.ac.be>
 *
 * Reworked by alex

    TODO: demuxer->movi_length
    TODO: DP_KEYFRAME

 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "help_mp.h"
#include "libmpstream/stream.h"
#include "demuxer.h"
#include "stheader.h"
#include "nuppelvideo.h"
#include "demux_msg.h"

struct nuv_signature
{
	char finfo[12];     /* "NuppelVideo" + \0 */
	char version[5];    /* "0.05" + \0 */
};

typedef struct _nuv_position_t nuv_position_t;

struct _nuv_position_t
{
	off_t offset;
	float time;
	int frame;
	nuv_position_t* next;
};

typedef struct _nuv_info_t
{
	int current_audio_frame;
	int current_video_frame;
	nuv_position_t *index_list;
	nuv_position_t *current_position;
} nuv_priv_t;


/**
 * Seek to a position relative to the current position, indicated in time.
 */
static void nuv_seek ( demuxer_t *demuxer, const seek_args_t* seeka )
{
#define MAX_TIME 1000000
	nuv_priv_t* priv = reinterpret_cast<nuv_priv_t*>(demuxer->priv);
	struct rtframeheader rtjpeg_frameheader;
	off_t orig_pos;
	off_t curr_pos;
	float current_time = 0;
	float start_time = MAX_TIME;
	float target_time = start_time + seeka->secs * 1000; /* target_time, start_time are ms, rel_seek_secs s */

	orig_pos = stream_tell ( demuxer->stream );

	if ( seeka->secs > 0 )
	{
		/* Seeking forward */


		while(current_time < target_time )
		{
			if ((unsigned)stream_read ( demuxer->stream, (char*)& rtjpeg_frameheader, sizeof ( rtjpeg_frameheader ) ) < sizeof(rtjpeg_frameheader))
				return; /* EOF */
			le2me_rtframeheader(&rtjpeg_frameheader);

			if ( rtjpeg_frameheader.frametype == 'V' )
			{
				priv->current_position->next = (nuv_position_t*) mp_malloc ( sizeof ( nuv_position_t ) );
				priv->current_position = priv->current_position->next;
				priv->current_position->frame = priv->current_video_frame++;
				priv->current_position->time = rtjpeg_frameheader.timecode;
				priv->current_position->offset = orig_pos;
				priv->current_position->next = NULL;

				if ( start_time == MAX_TIME )
				{
					start_time = rtjpeg_frameheader.timecode;
					/* Recalculate target time with real start time */
					target_time = start_time + seeka->secs*1000;
				}

				current_time = rtjpeg_frameheader.timecode;

				curr_pos = stream_tell ( demuxer->stream );
				stream_seek ( demuxer->stream, curr_pos + rtjpeg_frameheader.packetlength );

				/* Adjust current sequence pointer */
			}
			else if ( rtjpeg_frameheader.frametype == 'A' )
			{
				if ( start_time == MAX_TIME )
				{
					start_time = rtjpeg_frameheader.timecode;
					/* Recalculate target time with real start time */
					target_time = start_time + seeka->secs * 1000;
				}
				current_time = rtjpeg_frameheader.timecode;


				curr_pos = stream_tell ( demuxer->stream );
				stream_seek ( demuxer->stream, curr_pos + rtjpeg_frameheader.packetlength );
			}
		}
	}
	else
	{
		/* Seeking backward */
		nuv_position_t* p;
		start_time = priv->current_position->time;

		/* Recalculate target time with real start time */
		target_time = start_time + seeka->secs * 1000;


		if(target_time < 0)
			target_time = 0;

		// Search the target time in the index list, get the offset
		// and go to that offset.
		p = priv->index_list;
		while ( ( p->next != NULL ) && ( p->time < target_time ) )
		{
			p = p->next;
		}
		stream_seek ( demuxer->stream, p->offset );
		priv->current_video_frame = p->frame;
	}
}


static int nuv_demux( demuxer_t *demuxer, demux_stream_t *__ds )
{
	struct rtframeheader rtjpeg_frameheader;
	off_t orig_pos;
	nuv_priv_t* priv = reinterpret_cast<nuv_priv_t*>(demuxer->priv);
	int want_audio = (demuxer->audio)&&(demuxer->audio->id!=-2);

	demuxer->filepos = orig_pos = stream_tell ( demuxer->stream );
	if ((unsigned)stream_read ( demuxer->stream, (char*)& rtjpeg_frameheader, sizeof ( rtjpeg_frameheader ) ) < sizeof(rtjpeg_frameheader))
	    return 0; /* EOF */
	le2me_rtframeheader(&rtjpeg_frameheader);

	/* Skip Seekpoint, Text and Sync for now */
	if ((rtjpeg_frameheader.frametype == 'R') ||
	    (rtjpeg_frameheader.frametype == 'T') ||
	    (rtjpeg_frameheader.frametype == 'S'))
	    return 1;

	if (((rtjpeg_frameheader.frametype == 'D') &&
	    (rtjpeg_frameheader.comptype == 'R')) ||
	    (rtjpeg_frameheader.frametype == 'V'))
	{
	    if ( rtjpeg_frameheader.frametype == 'V' )
	    {
		priv->current_video_frame++;
		priv->current_position->next = (nuv_position_t*) mp_malloc(sizeof(nuv_position_t));
		priv->current_position = priv->current_position->next;
		priv->current_position->frame = priv->current_video_frame;
		priv->current_position->time = rtjpeg_frameheader.timecode;
		priv->current_position->offset = orig_pos;
		priv->current_position->next = NULL;
	    }
	    /* put RTjpeg tables, Video info to video buffer */
	    stream_seek ( demuxer->stream, orig_pos );
	    ds_read_packet ( demuxer->video, demuxer->stream, rtjpeg_frameheader.packetlength + 12,
		    rtjpeg_frameheader.timecode*0.001, orig_pos, DP_NONKEYFRAME);


	} else
	/* copy PCM only */
	if (demuxer->audio && (rtjpeg_frameheader.frametype == 'A') &&
	    (rtjpeg_frameheader.comptype == '0'))
	{
	    priv->current_audio_frame++;
	    if (want_audio) {
	    /* put Audio to audio buffer */
		ds_read_packet ( demuxer->audio, demuxer->stream, rtjpeg_frameheader.packetlength,
			rtjpeg_frameheader.timecode*0.001, orig_pos + 12, DP_NONKEYFRAME);
	    } else {
	      /* skip audio block */
		stream_seek ( demuxer->stream,
			    stream_tell ( demuxer->stream )
			    + rtjpeg_frameheader.packetlength );
	    }
	}

	return 1;
}


static demuxer_t* nuv_open ( demuxer_t* demuxer )
{
	sh_video_t *sh_video = NULL;
	sh_audio_t *sh_audio = NULL;
	struct rtfileheader rtjpeg_fileheader;
	nuv_priv_t* priv = new(zeromem) nuv_priv_t;
	demuxer->priv = priv;
	priv->current_audio_frame = 0;
	priv->current_video_frame = 0;


	/* Go to the start */
	stream_reset(demuxer->stream);
	stream_seek(demuxer->stream, demuxer->stream->start_pos);

	stream_read ( demuxer->stream, (char*)& rtjpeg_fileheader, sizeof(rtjpeg_fileheader) );
	le2me_rtfileheader(&rtjpeg_fileheader);

	/* no video */
	if (rtjpeg_fileheader.videoblocks == 0)
	{
	    MSG_ERR("No video blocks in file\n");
	    return NULL;
	}

	/* Create a new video stream header */
	sh_video = new_sh_video ( demuxer, 0 );

	/* Make sure the demuxer knows about the new video stream header
	 * (even though new_sh_video() ought to take care of it)
	 */
	demuxer->video->sh = sh_video;

	/* Make sure that the video demuxer stream header knows about its
	 * parent video demuxer stream (this is getting wacky), or else
	 * video_read_properties() will choke
	 */
	sh_video->ds = demuxer->video;

	/* Custom fourcc for internal MPlayer use */
	sh_video->fourcc = mmioFOURCC('N', 'U', 'V', '1');

	sh_video->src_w = rtjpeg_fileheader.width;
	sh_video->src_h = rtjpeg_fileheader.height;

	/* NuppelVideo uses pixel aspect ratio
	   here display aspect ratio is used.
	   For the moment NuppelVideo only supports 1.0 thus
	   1.33 == 4:3 aspect ratio.
	*/
	if(rtjpeg_fileheader.aspect == 1.0)
		sh_video->aspect = (float) 4.0f/3.0f;

	/* Get the FPS */
	sh_video->fps = rtjpeg_fileheader.fps;

	if (rtjpeg_fileheader.audioblocks != 0)
	{
	    sh_audio = new_sh_audio(demuxer, 0);
	    demuxer->audio->sh = sh_audio;
	    sh_audio->ds = demuxer->audio;
	    sh_audio->wtag = 0x1;
	    sh_audio->nch = 2;
	    sh_audio->rate = 44100;

	    sh_audio->wf = new(zeromem) WAVEFORMATEX;
	    sh_audio->wf->wFormatTag = sh_audio->wtag;
	    sh_audio->wf->nChannels = sh_audio->nch;
	    sh_audio->wf->wBitsPerSample = 16;
	    sh_audio->wf->nSamplesPerSec = sh_audio->rate;
	    sh_audio->wf->nAvgBytesPerSec = sh_audio->wf->nChannels*
		sh_audio->wf->wBitsPerSample*sh_audio->wf->nSamplesPerSec/8;
	    sh_audio->wf->nBlockAlign = sh_audio->nch * 2;
	    sh_audio->wf->cbSize = 0;
	}

	priv->index_list = (nuv_position_t*) mp_malloc(sizeof(nuv_position_t));
	priv->index_list->frame = 0;
	priv->index_list->time = 0;
	priv->index_list->offset = stream_tell ( demuxer->stream );
	priv->index_list->next = NULL;
	priv->current_position = priv->index_list;
    check_pin("demuxer",demuxer->pin,DEMUX_PIN);
    return demuxer;
}

static MPXP_Rc nuv_probe ( demuxer_t* demuxer )
{
    struct nuv_signature ns;

    /* Store original position */
    off_t orig_pos = stream_tell(demuxer->stream);

    MSG_V( "Checking for NuppelVideo\n" );

    stream_read(demuxer->stream,(char*)&ns,sizeof(ns));

    if ( strncmp ( ns.finfo, "NuppelVideo", 12 ) )
	return MPXP_False; /* Not a NuppelVideo file */
    if ( strncmp ( ns.version, "0.05", 5 ) )
	return MPXP_False; /* Wrong version NuppelVideo file */

    /* Return to original position */
    stream_seek ( demuxer->stream, orig_pos );
    demuxer->file_format=DEMUXER_TYPE_NUV;
    return MPXP_Ok;
}

static void nuv_close(demuxer_t* demuxer) {
  nuv_priv_t* priv = reinterpret_cast<nuv_priv_t*>(demuxer->priv);
  nuv_position_t* pos;
  if(!priv)
    return;
  for(pos = priv->index_list ; pos != NULL ; ) {
    nuv_position_t* p = pos;
    pos = pos->next;
    delete p;
  }
  delete priv;
}

static MPXP_Rc nuv_control(const demuxer_t *demuxer,int cmd,any_t*args)
{
    UNUSED(demuxer);
    UNUSED(cmd);
    UNUSED(args);
    return MPXP_Unknown;
}

extern const demuxer_driver_t demux_nuv =
{
    "NuppelVideo 0.05 parser",
    ".nuv",
    NULL,
    nuv_probe,
    nuv_open,
    nuv_demux,
    nuv_seek,
    nuv_close,
    nuv_control
};
