#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace	usr;
#include "mplayerxp.h"

#ifdef HAVE_LIBCDIO

#include "stream.h"
#include "libmpconf/cfgparser.h"

#include <stdio.h>
#include <stdlib.h>

#include "cdio/cdtext.h"
#include "cdd.h"
#include "stream_msg.h"

namespace	usr {
static int speed = -1;
static int search_overlap = -1;
static int no_skip = 0;

static const mpxp_option_t cdda_opts[] = {
  { "speed", &speed, CONF_TYPE_INT, CONF_RANGE,1,100, "sets driver speed" },
  { "overlap", &search_overlap, CONF_TYPE_INT, CONF_RANGE,0,75, "reserved" },
  { "noskip", &no_skip, CONF_TYPE_FLAG, 0 , 0, 1, "reserved" },
  { "skip", &no_skip, CONF_TYPE_FLAG, 0 , 1, 0, "reserved" },
  {NULL, NULL, 0, 0, 0, 0, NULL}
};

static const mpxp_option_t cdda_conf[] = {
  { "cdda", (any_t*)&cdda_opts, CONF_TYPE_SUBCONFIG, 0, 0, 0, "CD-DA related options"},
  { NULL,NULL, 0, 0, 0, 0, NULL}
};

void cdda_register_options(M_Config& cfg) {
    cfg.register_options(cdda_conf);
}

static unsigned cdda_parse_tracks(unsigned char *arr,unsigned nelem,const std::string& arg)
{
    const char *st,*end;
    unsigned rval=0;
    unsigned slen=arg.length();
    st=arg.c_str();
    memset(arr,0,sizeof(unsigned char)*nelem);
    do {
	size_t datalen,value,evalue,i;
	char data[100],*range;
	end=strchr(st,',');
	if(!end) end = &st[strlen(st)];
	datalen=end-st;
	memcpy(data,st,datalen);
	data[datalen]='\0';
	range=strchr(data,'-');
	if(range) {
	    *range='\0';
	    value=atoi(data);
	    evalue=atoi(&range[1]);
	    if(evalue>value && evalue<nelem) {
		for(i=value;i<=evalue;i++) arr[i]=1;
		rval=evalue;
	    }
	    else break;
	}
	else {
	    value=atoi(data);
	    if(value>nelem) break;
	    arr[value]=1;
	    rval=value;
	}
	st=end+1;
	if(st>arg.c_str()+slen) break;
    }while(end);
    return rval;
}

MPXP_Rc CDD_Interface::open_cdda(const std::string& dev,const std::string& arg) {
    unsigned cd_tracks;
    unsigned int audiolen=0;
    unsigned i;
    unsigned char arr[256];
    int st_inited;

    cd = cdio_cddap_identify(dev.c_str(),mp_conf.verbose?1:0,NULL);

    if(!cd) {
	mpxp_err<<"Can't open cdda device: "<<dev<<std::endl;
	return MPXP_False;
    }

    cdio_cddap_verbose_set(cd, mp_conf.verbose?CDDA_MESSAGE_PRINTIT:CDDA_MESSAGE_FORGETIT, mp_conf.verbose?CDDA_MESSAGE_PRINTIT:CDDA_MESSAGE_FORGETIT);

    if(cdio_cddap_open(cd) != 0) {
	mpxp_err<<"Can't open disc"<<std::endl;
	cdda_close(cd);
	return MPXP_False;
    }

    cd_tracks=cdio_cddap_tracks(cd);
    mpxp_v<<"Found "<<cd_tracks<<" tracks on disc"<<std::endl;
    if(!arg[0])
	for(i=1;i<=cd_tracks;i++) tracks[i-1].play=1;
    cdda_parse_tracks(arr,sizeof(arr)/sizeof(unsigned),arg);
    for(i=1;i<=256;i++) if (arr[i]) tracks[i-1].play=1;

    st_inited=0;
    mpxp_v<<"[CDDA] Queued tracks:";
    for(i=0;i<cd_tracks;i++) {
	if(tracks[i].play) {
	    tracks[i].start_sector=cdio_cddap_track_firstsector(cd,i+1);
	    tracks[i].end_sector=cdio_cddap_track_lastsector(cd,i+1);
	    mpxp_v<<" "<<i+1<<"["<<tracks[i].start_sector<<"-"<<tracks[i].end_sector<<"]";
	    if(!st_inited) { start_sector=tracks[i].start_sector; st_inited=1; }
	    end_sector=tracks[i].end_sector;
	    audiolen +=tracks[i].end_sector-tracks[i].start_sector+1;
	}
    }
    for(;i<256;i++) tracks[i].play=0;
    mpxp_v<<std::endl;
    min  = (unsigned int)(audiolen/(60*75));
    sec  = (unsigned int)((audiolen/75)%60);
    msec = (unsigned int)(audiolen%75);

    if(speed) cdio_cddap_speed_set(cd,speed);

    sector = start_sector;
    return MPXP_Ok;
}

off_t CDD_Interface::start() const { return start_sector*CDIO_CD_FRAMESIZE_RAW; }
off_t CDD_Interface::size() const { return end_sector*CDIO_CD_FRAMESIZE_RAW; }
int CDD_Interface::channels(unsigned track_idx) const { return cdio_cddap_track_channels(cd, track_idx); }
lsn_t CDD_Interface::map_sector(lsn_t _sector,track_t *tr)
{
    unsigned i,j;
    lsn_t cd_track=_sector;
    for(i=0;i<256;i++){
	if(tracks[i].play && tracks[i].end_sector==_sector) {
	    cd_track=0;
	    mpxp_v<<"Found track changing. old track="<<i<<" Sector="<<_sector<<std::endl;
	    for(j=i+1;j<256;j++) {
		if(tracks[j].play && tracks[j].start_sector==_sector+1) {
		    cd_track=tracks[j].start_sector;
		    if(tr) *tr=j;
		    mpxp_v<<"new track="<<j<<" Sector="<<cd_track<<std::endl;
		}
	    }
	}
    }
    return cd_track;
}

/* return physical sector address */
unsigned long CDD_Interface::psa(unsigned long _sector)
{
    unsigned i;
    unsigned long got_sectors=start_sector,track_len;
    for(i=0;i<256;i++){
	if(tracks[i].play) {
	    track_len=tracks[i].end_sector-tracks[i].start_sector;
	    if(_sector>=got_sectors && _sector <= track_len) return _sector+tracks[i].start_sector;
	    got_sectors+=track_len;
	}
    }
    return 0;
}

int CDD_Interface::read(char *buf,track_t *tr) {
    track_t i=255;

    if(cdio_cddap_read(cd, buf, sector, 1)==0) {
	mpxp_err<<"[CD-DA]: read error occured"<<std::endl;
	return -1; /* EOF */
    }
    sector++;
    if(sector == end_sector) {
	mpxp_dbg2<<"EOF was reached"<<std::endl;
	return -1; /* EOF */
    }

    sector=map_sector(sector,&i);
    if(!sector) return -1;
    if(i!=255) {
	*tr=i+1;
	mpxp_v<<"Track "<<*tr<<", sector="<<sector<<std::endl;
    } else mpxp_dbg2<<"Track "<<*tr<<", sector="<<sector<<std::endl;
    return CDIO_CD_FRAMESIZE_RAW;
}

void CDD_Interface::seek(off_t pos,track_t *tr) {
    long _sec;
    long seeked_track=0;
    track_t j=255;

    _sec = pos/CDIO_CD_FRAMESIZE_RAW;
    mpxp_dbg2<<"[CDDA] prepare seek to "<<_sec<<std::endl;
    seeked_track=_sec;
    *tr=255;
    if( sector!=seeked_track ) {
	seeked_track = map_sector(seeked_track,&j);
	if(seeked_track) *tr=j+1;
    }
    sector=seeked_track;
}

off_t CDD_Interface::tell() const {
    return sector*CDIO_CD_FRAMESIZE_RAW;
}

void CDD_Interface::close() {
    cdio_cddap_close(cd);
}

CDD_Interface::CDD_Interface() {}
CDD_Interface::~CDD_Interface() {}
} // namespace	usr
#endif
