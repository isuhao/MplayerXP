/*
 * demuxer for PVA files, such as the ones produced by software to manage DVB boards
 * like the Hauppauge WinTV DVBs, for MPlayer.
 *
 * Uses info from the PVA file specifications found at
 * 
 * http://www.technotrend.de/download/av_format_v1.pdf
 * 
 * WARNING: Quite a hack was required in order to get files by MultiDec played back correctly.
 * If it breaks anything else, just comment out the "#define DEMUX_PVA_MULTIDEC_HACK" below
 * and it will not be compiled in.
 *
 * Feedback is appreciated.
 *
 * written by Matteo Giani

    TODO: demuxer->movi_length
    TODO: DP_KEYFRAME

 */

#define DEMUX_PVA_MULTIDEC_HACK
#define PVA_NEW_PREBYTES_CODE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mp_config.h"
#include "help_mp.h"

#include "stream.h"
#include "demuxer.h"
#include "stheader.h"

#include "bswap.h"
#include "demux_msg.h"

/*
 * #defines below taken from PVA spec (see URL above)
 */

#define PVA_MAX_VIDEO_PACK_LEN 6*1024

#define VIDEOSTREAM 0x01	
#define MAINAUDIOSTREAM 0x02

typedef struct {
	off_t offset;
	long size;
	uint8_t type;
	uint8_t is_packet_start;
	float pts;
} pva_payload_t;


typedef struct {
	float last_audio_pts;
	float last_video_pts;
#ifdef PVA_NEW_PREBYTES_CODE
	float video_pts_after_prebytes;
	long video_size_after_prebytes;
	uint8_t prebytes_delivered;
#endif
	uint8_t just_synced;
	uint8_t synced_stream_id;
} pva_priv_t;


static int pva_sync(demuxer_t * demuxer)
{
	uint8_t buffer[5]={0,0,0,0,0};
	int count;
	pva_priv_t * priv = (pva_priv_t *) demuxer->priv;

	
	 /* This function is used to find the next nearest PVA packet start after a seek, since a PVA file
	  * is not indexed.
	  * The just_synced field is in the priv structure so that pva_get_payload knows pva_sync
	  * has already read (part of) the PVA header. This way we can avoid to seek back and (hopefully)
	  * be able to read from pipes and such.
	  */
	
	
	for(count=0 ; count<PVA_MAX_VIDEO_PACK_LEN && !stream_eof(demuxer->stream) && !priv->just_synced ; count++)
	{
		buffer[0]=buffer[1];
		buffer[1]=buffer[2];
		buffer[2]=buffer[3];
		buffer[3]=buffer[4];
		buffer[4]=stream_read_char(demuxer->stream);
		/*
		 * Check for a PVA packet beginning sequence: we check both the "AV" word at the 
		 * very beginning and the "0x55" reserved byte (which is unused and set to 0x55 by spec)
		 */
		if(buffer[0]=='A' && buffer[1] == 'V' && buffer[4] == 0x55) priv->just_synced=1;
		//printf("demux_pva: pva_sync(): current offset= %ld\n",stream_tell(demuxer->stream));
	}
	if(priv->just_synced)
	{
		if(priv!=NULL) priv->synced_stream_id=buffer[2];
		return 1;
	}
	else
	{
		return 0;
	}
}

static int pva_probe(demuxer_t * demuxer)
{
	uint8_t buffer[5]={0,0,0,0,0};
	MSG_V("Checking for PVA\n");
	stream_read(demuxer->stream,buffer,5);
	if(buffer[0]=='A' && buffer[1] == 'V' && buffer[4] == 0x55)
	{
		MSG_DBG2("Success: PVA\n");
		demuxer->file_format=DEMUXER_TYPE_PVA;
		return 1;
	}
	else
	{
		MSG_DBG2("Failed: PVA\n");
		return 0;
	}
}

static demuxer_t* pva_open (demuxer_t * demuxer)
{
	sh_video_t *sh_video = new_sh_video(demuxer,0);
        sh_audio_t *sh_audio = new_sh_audio(demuxer,0);

	
	pva_priv_t * priv;
	
	stream_reset(demuxer->stream);
	stream_seek(demuxer->stream,0);

	priv=malloc(sizeof(pva_priv_t));
	demuxer->flags|=DEMUXF_SEEKABLE;
	
	demuxer->priv=priv;
	memset(demuxer->priv,0,sizeof(pva_priv_t));
			
	if(!pva_sync(demuxer))
	{
		MSG_ERR("Not a PVA file.\n");
		return NULL;
	}

	//printf("priv->just_synced %s after initial sync!\n",priv->just_synced?"set":"UNSET");
	
	demuxer->video->sh=sh_video;
	
	//printf("demuxer->stream->end_pos= %d\n",demuxer->stream->end_pos);

	
	MSG_V("Opened PVA demuxer...\n");
	
	/*
	 * Audio and Video codecs:
	 * the PVA spec only allows MPEG2 video and MPEG layer II audio. No need to check the formats then.
	 * Moreover, there would be no way to do that since the PVA stream format has no fields to describe
	 * the used codecs.
	 */
	
	sh_video->fourcc=0x10000002;
	sh_video->ds=demuxer->video;
		
	/*
	printf("demuxer->video->id==%d\n",demuxer->video->id);
	printf("demuxer->audio->id==%d\n",demuxer->audio->id);
	*/
	
	demuxer->audio->sh=sh_audio;
	sh_audio->format=0x50;
	sh_audio->ds=demuxer->audio;
	
	demuxer->movi_start=0;
	demuxer->movi_end=demuxer->stream->end_pos;
	
	priv->last_video_pts=-1;
	priv->last_audio_pts=-1;

	return demuxer;
}

static int pva_get_payload(demuxer_t * d,pva_payload_t * payload);

// 0 = EOF or no stream found
// 1 = successfully read a packet
static int pva_demux(demuxer_t * demux,demux_stream_t *__ds)
{
	uint8_t done=0;
	demux_packet_t * dp;
	pva_priv_t * priv=demux->priv;
	pva_payload_t current_payload;

	while(!done)
	{
		if(!pva_get_payload(demux,&current_payload)) return 0;
		switch(current_payload.type)
		{
			case VIDEOSTREAM:
				if(demux->video->id==-1) demux->video->id=0;
				if(!current_payload.is_packet_start && priv->last_video_pts==-1)
				{
					/* We should only be here at the beginning of a stream, when we have
					 * not yet encountered a valid Video PTS, or after a seek.
					 * So, skip these starting packets in order not to deliver the
					 * player a bogus PTS.
					 */
					done=0;
				}
				else
				{
					/*
					 * In every other condition, we are delivering the payload. Set this
					 * so that the following code knows whether to skip it or read it.
					 */
					done=1;
				}
				if(demux->video->id!=0) done=0;
				if(current_payload.is_packet_start)
				{
					priv->last_video_pts=current_payload.pts;
					//MSG_DBG2("demux_pva: Video PTS=%llu , delivered %f\n",current_payload.pts,priv->last_video_pts);
				}
				if(done)
				{
					int l;
					dp=new_demux_packet(current_payload.size);
					dp->pts=priv->last_video_pts;
					dp->flags=DP_NONKEYFRAME;
					l=stream_read(demux->stream,dp->buffer,current_payload.size);
					resize_demux_packet(dp,l);
					ds_add_packet(demux->video,dp);
				}
				else
				{
					//printf("Skipping %u video bytes\n",current_payload.size);
					stream_skip(demux->stream,current_payload.size);
				}
				break;
			case MAINAUDIOSTREAM:
				if(demux->audio->id==-1) demux->audio->id=0;
				if(!current_payload.is_packet_start && priv->last_audio_pts==-1)
				{
					/* Same as above for invalid video PTS, just for audio. */
					done=0;
				}
				else
				{
					done=1;
				}
				if(current_payload.is_packet_start)
				{
					priv->last_audio_pts=current_payload.pts;
				}
				if(demux->audio->id!=0) done=0;
				if(done)
				{
					int l;
					dp=new_demux_packet(current_payload.size);
					dp->pts=priv->last_audio_pts;
					if(current_payload.offset != stream_tell(demux->stream))
						stream_seek(demux->stream,current_payload.offset);
					l=stream_read(demux->stream,dp->buffer,current_payload.size);
					resize_demux_packet(dp,l);
					ds_add_packet(demux->audio,dp);
				}
				else
				{
					stream_skip(demux->stream,current_payload.size);
				}
				break;
		}
	}
	return 1;
}

static int pva_get_payload(demuxer_t * d,pva_payload_t * payload)
{
	uint8_t flags,pes_head_len;
	uint16_t pack_size;
	off_t next_offset,pva_payload_start;
	unsigned char buffer[256];
#ifndef PVA_NEW_PREBYTES_CODE
	demux_packet_t * dp; 	//hack to deliver the preBytes (see PVA doc)
#endif
	pva_priv_t * priv=(pva_priv_t *) d->priv;

	
	if(d==NULL)
	{
		MSG_ERR("demux_pva: pva_get_payload got passed a NULL pointer!\n");
		return 0;
	}

	d->filepos=stream_tell(d->stream);
	
	
	
	
	if(stream_eof(d->stream))
	{
		MSG_V("demux_pva: pva_get_payload() detected stream->eof!!!\n");
		return 0;
	}

	//printf("priv->just_synced %s\n",priv->just_synced?"SET":"UNSET");
	
#ifdef PVA_NEW_PREBYTES_CODE
	if(priv->prebytes_delivered)
		/* The previous call to this fn has delivered the preBytes. Then we are already inside
		 * the payload. Let's just deliver the video along with its right PTS, the one we stored
		 * in the priv structure and was in the PVA header before the PreBytes.
		 */
	{
		//printf("prebytes_delivered=1. Resetting.\n");
		payload->size = priv->video_size_after_prebytes;
		payload->pts = priv->video_pts_after_prebytes;
		payload->is_packet_start = 1;
		payload->offset = stream_tell(d->stream);
		payload->type = VIDEOSTREAM;
		priv->prebytes_delivered = 0;
		return 1;
	}
#endif	
	if(!priv->just_synced)
	{
		if(stream_read_word(d->stream) != (('A'<<8)|'V'))
		{
			MSG_V("demux_pva: pva_get_payload() missed a SyncWord at %ld!! Trying to sync...\n",stream_tell(d->stream));
			if(!pva_sync(d))
			{
				if (!stream_eof(d->stream))
				{
					MSG_ERR("demux_pva: couldn't sync! (broken file?)");
				}
				return 0;
			}
		}
	}
	if(priv->just_synced)
	{
		payload->type=priv->synced_stream_id;
		priv->just_synced=0;
	}
	else
	{
		payload->type=stream_read_char(d->stream);
		stream_skip(d->stream,2); //counter and reserved
	}
	flags=stream_read_char(d->stream);
	payload->is_packet_start=flags & 0x10;
	pack_size=le2me_16(stream_read_word(d->stream));
	MSG_DBG2("demux_pva::pva_get_payload(): pack_size=%u field read at offset %lu\n",pack_size,stream_tell(d->stream)-2);
	pva_payload_start=stream_tell(d->stream);
	next_offset=pva_payload_start+pack_size;


	/*
	 * The code in the #ifdef directive below is a hack needed to get badly formatted PVA files
	 * such as the ones written by MultiDec played back correctly.
	 * Basically, it works like this: if the PVA packet does not signal a PES header, but the
	 * payload looks like one, let's assume it IS one. It has worked for me up to now.
	 * It can be disabled since it's quite an ugly hack and could potentially break things up
	 * if the PVA audio payload happens to start with 0x000001 even without being a non signalled
	 * PES header start.
	 * Though it's quite unlikely, it potentially could (AFAIK).
	 */
#ifdef DEMUX_PVA_MULTIDEC_HACK
	if(payload->type==MAINAUDIOSTREAM)
	{
		stream_read(d->stream,buffer,3);
		if(buffer[0]==0x00 && buffer[1]==0x00 && buffer[2]==0x01 && !payload->is_packet_start)
		{
			MSG_V("demux_pva: suspecting non signaled audio PES packet start. Maybe file by MultiDec?\n");
			payload->is_packet_start=1;
		}
		stream_seek(d->stream,stream_tell(d->stream)-3);
	}
#endif

	
	if(!payload->is_packet_start)
	{
		payload->offset=stream_tell(d->stream);
		payload->size=pack_size;
	}
	else
	{	//here comes the good part...
		switch(payload->type)
		{
			case VIDEOSTREAM:
				payload->pts=(float)(le2me_32(stream_read_dword(d->stream)))/90000;
				//printf("Video PTS: %f\n",payload->pts);
				if((flags&0x03) 
#ifdef PVA_NEW_PREBYTES_CODE
						&& !priv->prebytes_delivered
#endif
						)
				{
#ifndef PVA_NEW_PREBYTES_CODE					
					dp=new_demux_packet(flags&0x03);
					stream_read(d->stream,dp->buffer,flags & 0x03); //read PreBytes
					ds_add_packet(d->video,dp);
#else
					//printf("Delivering prebytes. Setting prebytes_delivered.");
					payload->offset=stream_tell(d->stream);
					payload->size = flags & 0x03;
					priv->video_pts_after_prebytes = payload->pts;
					priv->video_size_after_prebytes = pack_size - 4 - (flags & 0x03);
					payload->pts=priv->last_video_pts;
					payload->is_packet_start=0;
					priv->prebytes_delivered=1;
					return 1;
#endif
				}
				

				//now we are at real beginning of payload.
				payload->offset=stream_tell(d->stream);
				//size is pack_size minus PTS size minus PreBytes size.
				payload->size=pack_size - 4 - (flags & 0x03);
				break;
			case MAINAUDIOSTREAM:
				stream_skip(d->stream,3); //FIXME properly parse PES header.
				//printf("StreamID in audio PES header: 0x%2X\n",stream_read_char(d->stream));
				stream_skip(d->stream,4);
				
				buffer[255]=stream_read_char(d->stream);			
				pes_head_len=stream_read_char(d->stream);
				stream_read(d->stream,buffer,pes_head_len);
				if(!buffer[255]&0x80) //PES header does not contain PTS.
				{
					MSG_V("Audio PES packet does not contain PTS. (pes_head_len=%d)\n",pes_head_len);
					payload->pts=priv->last_audio_pts;
					break;
				}			
				else		//PES header DOES contain PTS
				{
					if((buffer[0] & 0xf0)!=0x20) // PTS badly formatted
					{
						MSG_V("demux_pva: expected audio PTS but badly formatted... (read 0x%02X). Falling back to previous PTS (hack).\n",buffer[0]);
						payload->pts=priv->last_audio_pts;
					//	return 0;
					}
					else
					{
						uint64_t temp_pts;
					
						temp_pts=0LL;
						temp_pts|=((uint64_t)(buffer[0] & 0x0e) << 29);
						temp_pts|=buffer[1]<<22;
						temp_pts|=(buffer[2] & 0xfe) << 14;
						temp_pts|=buffer[3]<<7;
						temp_pts|=(buffer[4] & 0xfe) >> 1;
						/*
					 	* PTS parsing is hopefully finished.
					 	*/
						payload->pts=(float)le2me_64(temp_pts)/90000;
					}
				}
				payload->offset=stream_tell(d->stream);
				payload->size=pack_size-stream_tell(d->stream)+pva_payload_start;
				break;
		}
	}
	return 1;
}

static void pva_seek(demuxer_t * demuxer,const seek_args_t* seeka)
{
	int total_bitrate=0;
	off_t dest_offset;
	pva_priv_t * priv=demuxer->priv;

	total_bitrate=((sh_audio_t *)demuxer->audio->sh)->i_bps;// + ((sh_video_t *)demuxer->video->sh)->i_bps;

	/*
	 * Compute SOF offset inside the stream. Approximate total bitrate with sum of bitrates
	 * reported by the audio and video codecs. The seek is not accurate because, just like
	 * with MPEG streams, the bitrate is not constant. Moreover, we do not take into account
	 * the overhead caused by PVA and PES headers.
	 * If the calculated SOF offset is negative, seek to the beginning of the file.
	 */
	
	dest_offset=(seeka->flags&DEMUX_SEEK_SET?demuxer->movi_start:stream_tell(demuxer->stream))+seeka->secs*total_bitrate;
	if(dest_offset<0) dest_offset=0;
	
	stream_seek(demuxer->stream,dest_offset);

	if(!pva_sync(demuxer)) { MSG_V("demux_pva: Couldn't seek!\n"); return ; }
	
	/*
	 * Reset the PTS info inside the pva_priv_t structure. This way we don't deliver
	 * data with the wrong PTSs (the ones we had before seeking).
	 *
	 */
	
	priv->last_video_pts=-1;
	priv->last_audio_pts=-1;
	
}



static void pva_close(demuxer_t * demuxer)
{
	if(demuxer->priv)
	{
		free(demuxer->priv);
		demuxer->priv=NULL;
	}
}

static int pva_control(demuxer_t *demuxer,int cmd,any_t*args)
{
    return DEMUX_UNKNOWN;
}
		
demuxer_driver_t demux_pva =
{
    "PVA (for DVB boards) parser",
    ".pva",
    NULL,
    pva_probe,
    pva_open,
    pva_demux,
    pva_seek,
    pva_close,
    pva_control
};
