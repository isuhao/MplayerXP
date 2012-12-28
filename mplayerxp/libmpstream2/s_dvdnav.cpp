#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;
/*
    s_dvdnav - DVDNAV's stream interface
*/
#ifdef USE_DVDNAV
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "stream.h"
#include "stream_internal.h"
#include "mpxp_help.h"
#include "libmpsub/spudec.h"
#include "libvo2/sub.h"
#include "input2/input.h"
#include "mplayerxp.h"
#include <algorithm>

#include "stream_msg.h"

#include <dvdnav/dvdnav.h>
#include <stdio.h>
#include <unistd.h>
#include "osdep/timer.h"
#include "mrl.h"

namespace mpxp {
static const int DVD_BLOCK_SIZE=2048;
    struct dvdnav_conf_t {
	int skip_opening;     /* skip opening stalls? */
	int title,chapter;
    };
    static struct dvdnav_conf_t dvdnav_conf = {0, -1, -1 };

    static const mrl_config_t dvdnavopts_conf[]={
	{ "skipopening", &dvdnav_conf.skip_opening, MRL_TYPE_BOOL, 0, 1 },
	{ "T", &dvdnav_conf.title, MRL_TYPE_INT, 0, 999 },
	{ "C", &dvdnav_conf.chapter, MRL_TYPE_INT, 0, 999 },
	{ NULL, NULL, 0, 0, 0 }
    };

    struct dvdnav_event_t {
	int event;             /* event number fromd dvdnav_events.h */
	uint8_t* details;        /* event details */
	int len;               /* bytes in details */
    };

    class DvdNav_Stream_Interface : public Stream_Interface {
	public:
	    DvdNav_Stream_Interface(libinput_t& libinput);
	    virtual ~DvdNav_Stream_Interface();

	    virtual MPXP_Rc	open(const std::string& filename,unsigned flags);
	    virtual int		read(stream_packet_t * sp);
	    virtual off_t	seek(off_t off);
	    virtual off_t	tell() const;
	    virtual void	close();
	    virtual MPXP_Rc	ctrl(unsigned cmd,any_t* param);
	    virtual Stream::type_e type() const;
	    virtual off_t	start_pos() const;
	    virtual off_t	size() const;
	    virtual off_t	sector_size() const;
	    virtual float	stream_pts() const;
	    virtual std::string mime_type() const;
	private:
	    MPXP_Rc		new_stream(const std::string& filename);
	    void		stream_ignore_timers(int ignore);
	    void		stream_sleep(int seconds);
	    int			stream_sleeping();
	    unsigned*		stream_get_palette() const;
	    void		stream_read(dvdnav_event_t*de);
	    void		get_highlight (rect_highlight_t *hl) const;
	    void		event_handler(const stream_packet_t*sp);
	    void		cmd_handler(unsigned cmd);

	    dvdnav_t*		dvdnav;              /* handle to libdvdnav stuff */
	    std::string		filename;            /* path */
	    int			ignore_timers;       /* should timers be skipped? */
	    int			sleeping;            /* are we sleeping? */
	    unsigned int	sleep_until;         /* timer */
	    int			started;             /* Has mplayer initialization finished? */
	    unsigned char	prebuf[STREAM_BUFFER_SIZE]; /* prefill buffer */
	    int			prelen;              /* length of prefill buffer */
	    off_t		cpos;
	    float		vobu_s_pts,vobu_e_pts;
	    int			menu_mode;
	    dvdnav_highlight_event_t& hlev;
	    int			dvd_nav_still;            /* are we on a still picture? */
	    dvdnav_event_t*	tevent;
	    int			tevent_full;
	    float		_stream_pts;
    };

DvdNav_Stream_Interface::DvdNav_Stream_Interface(libinput_t& libinput)
			:Stream_Interface(libinput),
			hlev(*new(zeromem) dvdnav_highlight_event_t) {}
DvdNav_Stream_Interface::~DvdNav_Stream_Interface() {
    dvdnav_close(dvdnav);
    delete &hlev;
}

void DvdNav_Stream_Interface::stream_ignore_timers(int ignore) {
    ignore_timers=ignore;
}

MPXP_Rc DvdNav_Stream_Interface::new_stream(const std::string& _filename) {
    const char* title_str;

    if (_filename.empty()) return MPXP_False;
    filename=_filename;

    if(dvdnav_open(&dvdnav,filename.c_str())!=DVDNAV_STATUS_OK) {
	return MPXP_False;
    }

    stream_ignore_timers(dvdnav_conf.skip_opening);

    if(1) {	//from vlc: if not used dvdnav from cvs will fail
	int len, event;
	uint8_t buf[2048];

	dvdnav_get_next_block(dvdnav,buf,&event,&len);
	dvdnav_sector_search(dvdnav, 0, SEEK_SET);
    }

    /* turn on/off dvdnav caching */
    dvdnav_set_readahead_flag(dvdnav,mp_conf.s_cache_size?0:1);

    /* report the title?! */
     if (dvdnav_get_title_string(dvdnav,&title_str)==DVDNAV_STATUS_OK) {
	    mpxp_info<<"Title: "<<title_str<<std::endl;
    }
    return MPXP_Ok;
}

void DvdNav_Stream_Interface::stream_sleep(int seconds) {

    if (!started) return;

    sleeping=0;
    switch (seconds) {
	case 0:
	    return;
	case 0xff:
	    mpxp_v<<"Sleeping indefinately"<<std::endl;
	    sleeping=2;
	    break;
	default:
	    mpxp_v<<"Sleeping "<<seconds<<"sec(s)"<<std::endl;
	    sleep_until = GetTimer();// + seconds*1000000;
	    sleeping=1;
	    break;
    }
    //if (dvdnav_priv->started) dvd_nav_still=1;
}

int DvdNav_Stream_Interface::stream_sleeping() {
    unsigned int now;

    if(sleeping) {
	now=GetTimer();
        while(sleeping>1 || now<sleep_until) {
//        usec_sleep(1000); /* 1ms granularity */
	    return 1;
	}
	dvdnav_still_skip(dvdnav); // continue past...
	sleeping=0;
	mpxp_v<<"woke up!"<<std::endl;
    }
    dvd_nav_still=0;
    mpxp_v<<"active"<<std::endl;
    return 0;
}

unsigned* DvdNav_Stream_Interface::stream_get_palette() const {
    return 0;
}

MPXP_Rc DvdNav_Stream_Interface::open(const std::string& _filename,unsigned flags)
{
    const char *param;
    char *dvd_device;
    int ntitles;
    UNUSED(flags);
    filename=_filename;
    param=mrl_parse_line(_filename,NULL,NULL,&dvd_device,NULL);
    if(strcmp(param,"help") == 0) {
	mpxp_hint<<"Usage: dvdnav://<title>,<chapter>"<<std::endl;
	return MPXP_False;
    }
    param=mrl_parse_params(param,dvdnavopts_conf);
    if (new_stream(dvd_device?dvd_device:DEFAULT_DVD_DEVICE)!=MPXP_Ok) {
	mpxp_err<<MSGTR_CantOpenDVD<<":"<<(dvd_device?dvd_device:DEFAULT_DVD_DEVICE)<<std::endl;
	if(!dvd_device) {
	    if (new_stream(DEFAULT_CDROM_DEVICE)!=MPXP_Ok)
		mpxp_err<<MSGTR_CantOpenDVD<<":"<<DEFAULT_CDROM_DEVICE<<std::endl;
	    else
		goto dvd_ok;
	}
	if(dvd_device) delete dvd_device;
	return MPXP_False;
    }
    dvd_ok:
    if(dvd_device) delete dvd_device;
    started=1;
    if(mp_conf.s_cache_size) {
	tevent = new(zeromem) dvdnav_event_t;
	tevent->details=new uint8_t[DVD_BLOCK_SIZE];
    }
    tevent_full=0;
    /* By rumours 1 PGC == whole movie */
    dvdnav_set_PGC_positioning_flag(dvdnav,1);
    ntitles=0;
    dvdnav_get_number_of_titles(dvdnav,&ntitles);
    mpxp_info<<MSGTR_DVDnumTitles<<":"<<ntitles<<std::endl;
    if(dvdnav_conf.title != -1) {
	int nparts;
	dvdnav_get_number_of_parts(dvdnav,dvdnav_conf.title,&nparts);
	mpxp_info<<MSGTR_DVDnumChapters<<":"<<dvdnav_conf.title<<":"<<nparts<<std::endl;
	if(dvdnav_conf.chapter != -1)	dvdnav_part_play(dvdnav,dvdnav_conf.title,dvdnav_conf.chapter);
	else				dvdnav_title_play(dvdnav,dvdnav_conf.title);
	cpos=2048; /* disallow dvdnav_reset */
	dvdnav_current_title_info(dvdnav,&dvdnav_conf.title,&dvdnav_conf.chapter);
	mpxp_info<<"Playing "<<dvdnav_conf.chapter<<" part of "<<dvdnav_conf.title<<" title"<<std::endl;
    }
    return MPXP_Ok;
}
Stream::type_e DvdNav_Stream_Interface::type() const {
    if(	dvdnav_is_domain_vmgm(dvdnav) ||
	dvdnav_is_domain_vtsm(dvdnav) || menu_mode )
		return Stream::Type_Menu|Stream::Type_Seekable;
    return Stream::Type_Seekable|Stream::Type_Program;
}
off_t	DvdNav_Stream_Interface::start_pos() const { return cpos; }
off_t	DvdNav_Stream_Interface::size() const { return -1; }
off_t	DvdNav_Stream_Interface::sector_size() const { return tevent?DVD_BLOCK_SIZE*10:DVD_BLOCK_SIZE; }
float	DvdNav_Stream_Interface::stream_pts() const { return _stream_pts; }
std::string DvdNav_Stream_Interface::mime_type() const { return "application/octet-stream"; }

void DvdNav_Stream_Interface::stream_read(dvdnav_event_t*de) {
    int event = DVDNAV_NOP;
    int done;

    if (!de->len) return;
    de->len=-1;
    if (!de->details) return;

    if (dvd_nav_still) {
	mpxp_v<<"got a stream_read while I should be asleep!"<<std::endl;
	de->event=DVDNAV_STILL_FRAME;
	de->len=0;
	return;
    }
    done=0;
    while(!done) {
	if (dvdnav_get_next_block(dvdnav,reinterpret_cast<uint8_t*>(de->details),&event,&de->len)!=DVDNAV_STATUS_OK) {
	    mpxp_err<<"Error getting next block from DVD: "<<dvdnav_err_to_string(dvdnav)<<std::endl;
	    de->len=-1;
	}
	if(event == DVDNAV_STILL_FRAME) {
	    dvdnav_still_skip(dvdnav); /* don't let dvdnav stall on this image */
	    while (stream_sleeping()) yield_timeslice(); /* 10ms */
	}
#ifdef DVDNAV_WAIT
	else if(event == DVDNAV_WAIT) {
	    yield_timeslice();
	    dvdnav_wait_skip(dvdnav); /* don't let dvdnav stall on this image */
	}
#endif
	else if(event == DVDNAV_NAV_PACKET) {
	    /* Try to suppress PTS discontinuity here!!! */
	    pci_t *_this;
	    _this=dvdnav_get_current_nav_pci(dvdnav);
	    vobu_s_pts=_this->pci_gi.vobu_s_ptm/90000.;
	    vobu_e_pts=_this->pci_gi.vobu_e_ptm/90000.;
	    mpxp_v<<"Handling NAV_PACKET: vobu_s_ptm="<<vobu_s_pts
		<<" vobu_e_ptm="<<vobu_e_pts<<" e_eltm="<<((float)_this->pci_gi.e_eltm.second+_this->pci_gi.e_eltm.minute*60.+_this->pci_gi.e_eltm.hour*3600.)<<std::endl;
	    if(vobu_s_pts < vobu_e_pts) {
		_stream_pts += vobu_e_pts-vobu_s_pts;
		mpxp_v<<"DVD's discontinuities found! Applying delta: "<<_stream_pts<<std::endl;
	    }
	    else _stream_pts = vobu_s_pts;
	    vobu_s_pts = vobu_s_pts;
	    vobu_e_pts = vobu_e_pts;
	} else if(event == DVDNAV_CELL_CHANGE) {
	    int ct,cc;
	    dvdnav_current_title_info(dvdnav, &ct, &cc);
	    if(ct<=0) {
		menu_mode=1;
		mpxp_v<<"entering menu mode: "<<ct<<" "<<cc<<std::endl;
		mpxp_v<<"vmgm: "<<dvdnav_is_domain_vmgm(dvdnav)<<" vtsm: "<<dvdnav_is_domain_vtsm(dvdnav)<<std::endl;
	    } else {
		menu_mode=0;
		mpxp_v<<"leaving menu mode: "<<ct<<" "<<cc<<std::endl;
	    }
	} else done=1;
    }
    if(!event) cpos += DVD_BLOCK_SIZE;
    de->event=event;
}

int DvdNav_Stream_Interface::read(stream_packet_t *sp)
{
    dvdnav_event_t de;
    unsigned len=sp->len;
    if(tevent && tevent_full) {
	sp->len=tevent->len;
	sp->type=tevent->event;
	memcpy(sp->buf,tevent->details,tevent->len);
	tevent_full=0;
	return sp->len;
    }
    de.len=sp->len;
    de.details=reinterpret_cast<uint8_t*>(sp->buf);
    stream_read(&de);
    sp->len=de.len;
    sp->type=de.event;
    if(tevent && !sp->type) {
	len -= sp->len;
	while(len) {
	    de.len=len;
	    de.details=reinterpret_cast<uint8_t*>(&sp->buf[sp->len]);
	    stream_read(&de);
	    if(de.event) {
		tevent->len=de.len;
		tevent->event=de.event;
		memcpy(tevent->details,de.details,de.len);
		tevent_full=1;
		break;
	    }
	    if(de.len<0 || (!de.event&&de.len==0)) break;
	    sp->len += de.len;
	    len-=de.len;
	}
    }
    return sp->len;
}

off_t DvdNav_Stream_Interface::seek(off_t pos)
{
    uint32_t newpos=0;
    uint32_t length=1;
    uint32_t sector;

    if (pos==0) {
	started=0;
	cpos=0;
	return 0;
    }
    sector=pos/DVD_BLOCK_SIZE;
    dvdnav_sector_search(dvdnav,sector,SEEK_SET);
    yield_timeslice(); /* wait for HOP_CHANNEL event */
    dvdnav_get_position(dvdnav, &newpos, &length);
    if(newpos > sector) newpos=sector;
    cpos = (newpos)*2048;
  /* reset pts_fix after seeking */
    vobu_s_pts=vobu_e_pts=_stream_pts=0;
    return cpos;
}

off_t DvdNav_Stream_Interface::tell() const { return (off_t)cpos; }

void DvdNav_Stream_Interface::close()
{
    if(tevent) { delete tevent->details; delete tevent; }
}

/**
 * \brief mp_dvdnav_get_highlight() get dvdnav highlight struct
 * \param stream: - stream pointer
 * \param hl    : - highlight struct pointer
 */
void DvdNav_Stream_Interface::get_highlight (rect_highlight_t *hl) const {
    int button;
    dvdnav_highlight_area_t ha;
    pci_t *pnavpci = NULL;

    dvdnav_get_current_highlight(dvdnav, &button);
    pnavpci = dvdnav_get_current_nav_pci (dvdnav);
    /* highlight mode: 0 - hide, 1 - show, 2 - activate, currently always 1 */
    dvdnav_get_highlight_area(pnavpci, button, 1, &ha);

    hl->sx = ha.sx;
    hl->sy = ha.sy;
    hl->ex = ha.ex;
    hl->ey = ha.ey;
}

void DvdNav_Stream_Interface::event_handler(const stream_packet_t*sp)
{
    switch(sp->type) {
	case DVDNAV_BLOCK_OK: /* be silent about this one */
	    break;
	case DVDNAV_HIGHLIGHT: {
	    pci_t *pnavpci = NULL;
	    dvdnav_highlight_event_t *_hlev = (dvdnav_highlight_event_t*)(sp->buf);
	    int btnum;
	    int display_mode=1;
	    mpxp_v<<"DVDNAV_HIGHLIGHT: "<<_hlev->sx<<" "<<_hlev->sy<<" "<<_hlev->ex<<" "<<_hlev->ey<<std::endl;
	    if (!dvdnav)  return;
	    memcpy(&hlev,_hlev,sizeof(dvdnav_highlight_event_t));
	    pnavpci = dvdnav_get_current_nav_pci (dvdnav);
	    if (!pnavpci)   return;

	    dvdnav_get_current_highlight (dvdnav, reinterpret_cast<int32_t*>(&hlev.buttonN));
	    hlev.display = display_mode; /* show */

	    if (hlev.buttonN > 0 && pnavpci->hli.hl_gi.btn_ns > 0 && hlev.display) {
		for (btnum = 0; btnum < pnavpci->hli.hl_gi.btn_ns; btnum++) {
		    btni_t *btni = &(pnavpci->hli.btnit[btnum]);

		    if (hlev.buttonN == (unsigned)btnum + 1) {
			hlev.sx = std::min (btni->x_start, btni->x_end);
			hlev.ex = std::max (btni->x_start, btni->x_end);
			hlev.sy = std::min (btni->y_start, btni->y_end);
			hlev.ey = std::max (btni->y_start, btni->y_end);

			hlev.palette = (btni->btn_coln == 0) ? 0 :
					pnavpci->hli.btn_colit.btn_coli[btni->btn_coln - 1][0];
			break;
		    }
		}
	    } else { /* hide button or no button */
		hlev.sx = hlev.ex = 0;
		hlev.sy = hlev.ey = 0;
		hlev.palette = hlev.buttonN = 0;
	    }
	    break;
	}
	case DVDNAV_STILL_FRAME: {
	    const dvdnav_still_event_t *still_event = (const dvdnav_still_event_t*)(sp->buf);
		mpxp_dbg2<<"######## DVDNAV Event: Still Frame: "<<still_event->length<<" sec(s)"<<std::endl;
		while (stream_sleeping()) {
		    yield_timeslice();
		}
		stream_sleep(still_event->length);
		break;
	    }
	case DVDNAV_STOP:
	    mpxp_dbg2<<"DVDNAV Event: Nav Stop"<<std::endl;
	    break;
	case DVDNAV_NOP:
	    mpxp_v<<"DVDNAV Event: Nav NOP"<<std::endl;
	    break;
#if 0
	case DVDNAV_SPU_STREAM_CHANGE: {
	    const dvdnav_spu_stream_change_event_t * stream_change=(const dvdnav_spu_stream_change_event_t*)(sp->buf);
	    if (mpxp_context().video().output->spudec && mp_conf.dvdsub_id!=stream_change->physical_wide) {
		    // FIXME: need a better way to change SPU id
		d_dvdsub->id=mp_conf.dvdsub_id=stream_change->physical_wide;
		if (mpxp_context().video().output->spudec) spudec_reset(mpxp_context().video().output->spudec);
	    }
	    break;
	}
	case DVDNAV_AUDIO_STREAM_CHANGE: {
	    int aid_temp;
	    const dvdnav_audio_stream_change_event_t *stream_change = (const dvdnav_audio_stream_change_event_t*)(sp->buf);
	    aid_temp=stream_change->physical;
	    if (aid_temp>=0) aid_temp+=128; // FIXME: is this sane?
	    if (d_audio && mp_conf.audio_id!=aid_temp) {
		// FIXME: need a bettery way to change audio stream id
		d_audio->id=mp_conf.dvdsub_id=aid_temp;
		mpxp_resync_audio_stream();
	    }
	    break;
	}
#endif
	case DVDNAV_VTS_CHANGE:{
	    const dvdnav_vts_change_event_t *evts = (const dvdnav_vts_change_event_t *)(sp->buf);
	    mpxp_v<<"DVDNAV Event: Nav VTS Change "<<evts->new_domain<<std::endl;
	    }
	    break;
	case DVDNAV_CELL_CHANGE: {
	    const dvdnav_cell_change_event_t *ecell=(const dvdnav_cell_change_event_t*)(sp->buf);
	    mpxp_v<<"DVDNAV_CELL_CHANGE: N="<<ecell->cellN
		<<" pgN="<<ecell->pgN<<" cell_start="<<(ecell->cell_start/90000.)
		<<" pg_start="<<(ecell->pg_start/90000.)
		<<" cell_length="<<(ecell->cell_length/90000.)
		<<" pg_length="<<(ecell->pg_length/90000.)
		<<" pgc_length="<<(ecell->pgc_length/90000.)<<std::endl;
	    }
	    break;
	case DVDNAV_NAV_PACKET:
	    mpxp_v<<"DVDNAV Event: Nav Packet"<<std::endl;
	    break;
	case DVDNAV_SPU_CLUT_CHANGE:
	    mpxp_dbg2<<"DVDNAV Event: Nav SPU CLUT Change"<<std::endl;
	    if(sp->len!=64) mpxp_warn<<"DVDNAV Event: Nav SPU CLUT Change: "<<sp->len<<" bytes <> 64"<<std::endl;
	    // send new palette to SPU decoder
	    if (mpxp_context().video().output->spudec) spudec_update_palette(mpxp_context().video().output->spudec,(const unsigned int *)(sp->buf));
	    break;
    }
}

void DvdNav_Stream_Interface::cmd_handler(unsigned cmd)
{
    int button;
    pci_t *pci = dvdnav_get_current_nav_pci(dvdnav);
    switch (cmd) {
	case MP_CMD_DVDNAV_UP:
	  dvdnav_upper_button_select(dvdnav,pci);
	  break;
	case MP_CMD_DVDNAV_DOWN:
	  dvdnav_lower_button_select(dvdnav,pci);
	  break;
	case MP_CMD_DVDNAV_LEFT:
	  dvdnav_left_button_select(dvdnav,pci);
	  break;
	case MP_CMD_DVDNAV_RIGHT:
	  dvdnav_right_button_select(dvdnav,pci);
	  break;
	case MP_CMD_DVDNAV_MENU: {
	    int title,part;
	    mpxp_v<<"Menu call"<<std::endl;
	    dvdnav_current_title_info(dvdnav, &title, &part);
	    if(title>0) {
		if(dvdnav_menu_call(dvdnav, DVD_MENU_Part) == DVDNAV_STATUS_OK
		|| dvdnav_menu_call(dvdnav, DVD_MENU_Title) == DVDNAV_STATUS_OK)
			break;
	    }
	    dvdnav_menu_call(dvdnav, DVD_MENU_Root);
	    dvdnav_button_select(dvdnav, pci, 1);
	}
	break;
	case MP_CMD_DVDNAV_SELECT:
	  dvdnav_button_activate(dvdnav,pci);
	  break;
	default:
	  mpxp_v<<"Weird DVD Nav cmd "<<cmd<<std::endl;
	  break;
    }
    dvdnav_get_current_highlight(dvdnav, &button);
    dvdnav_button_select(dvdnav,pci,button);
}

MPXP_Rc DvdNav_Stream_Interface::ctrl(unsigned cmd,any_t*args)
{
    switch(cmd) {
	case SCTRL_TXT_GET_STREAM_NAME: {
	    const char *title_str;
	    if (dvdnav_get_title_string(dvdnav,&title_str)==DVDNAV_STATUS_OK) {
		strncpy(reinterpret_cast<char*>(args),title_str,256);
		((char *)args)[255]=0;
		return MPXP_Ok;
	    }
	}
	break;
	case SCTRL_VID_GET_PALETTE: {
	    unsigned* pal;
	    pal=stream_get_palette();
	    *((unsigned **)args)=pal;
	    return MPXP_Ok;
	}
	break;
	case SCTRL_VID_GET_HILIGHT: {
	    get_highlight (reinterpret_cast<rect_highlight_t*>(args));
	    return MPXP_Ok;
	}
	case SCRTL_EVT_HANDLE: {
	    event_handler(reinterpret_cast<stream_packet_t*>(args));
	    return MPXP_Ok;
	}
	break;
	case SCRTL_MPXP_CMD: {
	    cmd_handler((long)args);
	    return MPXP_Ok;
	}
	default: break;
    }
    return MPXP_False;
}

static Stream_Interface* query_interface(libinput_t& libinput) { return new(zeromem) DvdNav_Stream_Interface(libinput); }

extern const stream_interface_info_t dvdnav_stream =
{
    "dvdnav://",
    "reads multimedia stream with using of libdvdnav library",
    query_interface
};
} // namespace mpxp
#endif
