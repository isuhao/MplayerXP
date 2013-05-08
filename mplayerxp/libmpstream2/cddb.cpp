#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace	usr;
/*
 * CDDB HTTP protocol
 * by Bertrand Baudet <bertrand_baudet@yahoo.com>
 * (C) 2002, MPlayer team.
 *
 * Implementation follow the freedb.howto1.06.txt specification
 * from http://freedb.freedb.org
 *
 * discid computation by Jeremy D. Zawodny
 *	 Copyright (c) 1998-2000 Jeremy D. Zawodny <Jeremy@Zawodny.com>
 *	 Code release under GPL
 *
 */
#include "mplayerxp.h"

#if defined(HAVE_LIBCDIO) && defined(HAVE_STREAMING)

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>
#include <errno.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__bsdi__)
#define SYS_BSD 1
#endif

#if defined(__linux__)
#include <linux/cdrom.h>
#elif defined(SYS_BSD)
#include <sys/cdio.h>
#endif

#include "stream.h"
#include "libmpconf/cfgparser.h"

#include "cdd.h"
#include "tcp.h"
#include "version.h"
#include "osdep/get_path.h"
#include "network.h"
#include "stream_msg.h"

namespace	usr {

static const char* DEFAULT_FREEDB_SERVER="freedb.freedb.org";
static const char* DEFAULT_CACHE_DIR	="/.cddb/";

static cd_toc_t cdtoc[100];

#if defined(__linux__)
static int read_toc(void) {
	int drive = open("/dev/cdrom", O_RDONLY | O_NONBLOCK);
	struct cdrom_tochdr tochdr;
	struct cdrom_tocentry tocentry;
	int i;

	ioctl(drive, CDROMREADTOCHDR, &tochdr);
	for (i = tochdr.cdth_trk0; i <= tochdr.cdth_trk1; i++) {
		tocentry.cdte_track = i;
		tocentry.cdte_format = CDROM_MSF;
		ioctl(drive, CDROMREADTOCENTRY, &tocentry);
		cdtoc[i-1].min = tocentry.cdte_addr.msf.minute;
		cdtoc[i-1].sec = tocentry.cdte_addr.msf.second;
		cdtoc[i-1].frame = tocentry.cdte_addr.msf.frame;
		cdtoc[i-1].frame += cdtoc[i-1].min*60*75;
		cdtoc[i-1].frame += cdtoc[i-1].sec*75;
	}
	tocentry.cdte_track = 0xAA;
	tocentry.cdte_format = CDROM_MSF;
	ioctl(drive, CDROMREADTOCENTRY, &tocentry);
	cdtoc[tochdr.cdth_trk1].min = tocentry.cdte_addr.msf.minute;
	cdtoc[tochdr.cdth_trk1].sec = tocentry.cdte_addr.msf.second;
	cdtoc[tochdr.cdth_trk1].frame = tocentry.cdte_addr.msf.frame;
	cdtoc[tochdr.cdth_trk1].frame += cdtoc[tochdr.cdth_trk1].min*60*75;
	cdtoc[tochdr.cdth_trk1].frame += cdtoc[tochdr.cdth_trk1].sec*75;
	close(drive);
	return tochdr.cdth_trk1;
}

#elif defined(SYS_BSD)
static int read_toc(void) {
	int drive = open("/dev/acd0c", O_RDONLY | O_NONBLOCK);
	struct ioc_toc_header tochdr;
	struct ioc_read_toc_single_entry tocentry;
	int i;

	ioctl(drive, CDIOREADTOCHEADER, &tochdr);
	for (i = tochdr.starting_track; i <= tochdr.ending_track; i++) {
		tocentry.track = i;
		tocentry.address_format = CD_MSF_FORMAT;
		ioctl(drive, CDIOREADTOCENTRY, &tocentry);
		cdtoc[i-1].min = tocentry.entry.addr.msf.minute;
		cdtoc[i-1].sec = tocentry.entry.addr.msf.second;
		cdtoc[i-1].frame = tocentry.entry.addr.msf.frame;
		cdtoc[i-1].frame += cdtoc[i-1].min*60*75;
		cdtoc[i-1].frame += cdtoc[i-1].sec*75;
	}
	tocentry.track = 0xAA;
	tocentry.address_format = CD_MSF_FORMAT;
	ioctl(drive, CDIOREADTOCENTRY, &tocentry);
	cdtoc[tochdr.ending_track].min = tocentry.entry.addr.msf.minute;
	cdtoc[tochdr.ending_track].sec = tocentry.entry.addr.msf.second;
	cdtoc[tochdr.ending_track].frame = tocentry.entry.addr.msf.frame;
	cdtoc[tochdr.ending_track].frame += cdtoc[tochdr.ending_track].min*60*75;
	cdtoc[tochdr.ending_track].frame += cdtoc[tochdr.ending_track].sec*75;
	close(drive);
	return tochdr.ending_track;
}
#endif

static unsigned int __FASTCALL__ cddb_sum(int n) {
	unsigned int ret;

	ret = 0;
	while (n > 0) {
		ret += (n % 10);
		n /= 10;
	}
	return ret;
}

static unsigned long __FASTCALL__ cddb_discid(int tot_trks) {
	unsigned int i, t = 0, n = 0;

	i = 0;
	while (i < (unsigned)tot_trks) {
		n = n + cddb_sum((cdtoc[i].min * 60) + cdtoc[i].sec);
		i++;
	}
	t = ((cdtoc[tot_trks].min * 60) + cdtoc[tot_trks].sec) -
		((cdtoc[0].min * 60) + cdtoc[0].sec);
	return ((n % 0xff) << 24 | t << 8 | tot_trks);
}

static int __FASTCALL__ cddb_http_request(const char *command, int (*reply_parser)(HTTP_Header&,cddb_data_t*), cddb_data_t *cddb_data) {
    char request[4096];
    int ret = 0;
    Tcp tcp(cddb_data->libinput,-1);
    URL& url=*new(zeromem) URL("");

    if( reply_parser==NULL || command==NULL || cddb_data==NULL ) return -1;

    sprintf( request, "http://%s/~cddb/cddb.cgi?cmd=%s%s&proto=%d", cddb_data->freedb_server, command, cddb_data->cddb_hello.c_str(), cddb_data->freedb_proto_level );
    mpxp_v<<"Request["<<request<<"]"<<std::endl;

    if(url.redirect(request)!=MPXP_Ok) {
	mpxp_err<<"Not valid URL: "<<request<<std::endl;
	return -1;
    }

    if(http_send_request(tcp,url,0)!=MPXP_Ok) {
	mpxp_err<<"failed to send the http request"<<std::endl;
	return -1;
    }

    HTTP_Header* http_hdr = http_read_response( tcp );
    if( http_hdr==NULL ) {
	mpxp_err<<"Failed to read the http response"<<std::endl;
	return -1;
    }

    http_hdr->debug_hdr();

    switch(http_hdr->get_status()) {
	case 200:
	    ret = reply_parser(*http_hdr, cddb_data);
	    break;
	case 400:
	    mpxp_v<<"Not Found"<<std::endl;
	    break;
	default:
	    mpxp_v<<"Unknown Error code"<<std::endl;
    }

    delete http_hdr;
    delete &url;

    return ret;
}

static int __FASTCALL__ cddb_read_cache(cddb_data_t *cddb_data) {
    char file_name[100];
    struct stat stats;
    int file_fd, ret;
    size_t file_size;

    if( cddb_data==NULL || cddb_data->cache_dir.empty()) return -1;

    sprintf( file_name, "%s%08lx", cddb_data->cache_dir.c_str(), cddb_data->disc_id);

    file_fd = ::open(file_name, O_RDONLY);
    if( file_fd<0 ) {
	mpxp_err<<"No cache found"<<std::endl;
	return -1;
    }

    ret = fstat( file_fd, &stats );
    if( ret<0 ) {
	::perror("fstat");
	file_size = 4096;
    } else {
	file_size = stats.st_size;
    }

    cddb_data->xmcd_file = new char [file_size];
    if( cddb_data->xmcd_file==NULL ) {
	mpxp_fatal<<"Memory allocation failed"<<std::endl;
	::close(file_fd);
	return -1;
    }
    cddb_data->xmcd_file_size = ::read(file_fd, cddb_data->xmcd_file, file_size);
    if( cddb_data->xmcd_file_size!=file_size ) {
	mpxp_fatal<<"Not all the xmcd file has been read"<<std::endl;
	::close(file_fd);
	return -1;
    }
    ::close(file_fd);
    return 0;
}

static int __FASTCALL__ cddb_write_cache(cddb_data_t *cddb_data) {
    // We have the file, save it for cache.
    char file_name[100];
    int file_fd;
    int wrote=0;

    if( cddb_data==NULL || cddb_data->cache_dir.empty() ) return -1;

    sprintf( file_name, "%s%08lx", cddb_data->cache_dir.c_str(), cddb_data->disc_id);

    file_fd = ::creat(file_name, S_IREAD|S_IWRITE);
    if( file_fd<0 ) {
	::perror("open");
	return -1;
    }

    wrote = ::write(file_fd, cddb_data->xmcd_file, cddb_data->xmcd_file_size);
    if( wrote<0 ) {
	mpxp_err<<"write: "<<strerror(errno)<<std::endl;
	::close(file_fd);
	return -1;
    }
    if( (unsigned)wrote!=cddb_data->xmcd_file_size ) {
	mpxp_fatal<<"Not all the xmcd file has been written"<<std::endl;
	::close(file_fd);
	return -1;
    }
    ::close(file_fd);
    return 0;
}

static int cddb_read_parse(HTTP_Header& http_hdr, cddb_data_t *cddb_data) {
    unsigned long disc_id;
    char category[100];
    char *ptr=NULL, *ptr2=NULL;
    int ret, status;

    if( cddb_data==NULL ) return -1;

    ret = sscanf(http_hdr.get_body(), "%d ", &status);
    if( ret!=1 ) {
	mpxp_err<<"Parse error"<<std::endl;
	return -1;
    }

    switch(status) {
	case 210:
	    ret = sscanf(http_hdr.get_body(), "%d %s %08lx", &status, category, &disc_id);
	    if( ret!=3 ) {
		mpxp_err<<"Parse error"<<std::endl;
		return -1;
	    }
	    // Check if it's a xmcd database file
	    ptr = strstr(const_cast<char*>(http_hdr.get_body()), "# xmcd");
	    if( ptr==NULL ) {
		mpxp_err<<"Invalid xmcd database file returned"<<std::endl;
		return -1;
	    }
	    // Ok found the beginning of the file
	    // look for the end
	    ptr2 = strstr(ptr, "\r\n.\r\n");
	    if( ptr2==NULL ) {
		ptr2 = strstr(ptr, "\n.\n");
		if( ptr2==NULL ) {
		    mpxp_err<<"Unable to find '.'"<<std::endl;
		    return -1;
		}
	    }
	    // Ok found the end
	    // do a sanity check
	    if( http_hdr.get_body_size()<(unsigned long)(ptr2-ptr) ) {
		mpxp_err<<"Unexpected fix me"<<std::endl;
		return -1;
	    }
	    cddb_data->xmcd_file = ptr;
	    cddb_data->xmcd_file_size = ptr2-ptr+2;
	    cddb_data->xmcd_file[cddb_data->xmcd_file_size] = '\0';
	    // Avoid the http_free function to mp_free the xmcd file...save a mempcy...
	    http_hdr.erase_body();
	    return cddb_write_cache(cddb_data);
	default:
	    mpxp_err<<"Unhandled code"<<std::endl;
    }
    return 0;
}

static int __FASTCALL__ cddb_request_titles(cddb_data_t *cddb_data) {
    char command[1024];
    sprintf( command, "cddb+read+%s+%08lx", cddb_data->category.c_str(), cddb_data->disc_id);
    return cddb_http_request(command, cddb_read_parse, cddb_data);
}

static int cddb_query_parse(HTTP_Header& http_hdr, cddb_data_t *cddb_data) {
    char album_title[100];
    char *ptr = NULL;
    int ret, status;

    ret = sscanf(http_hdr.get_body(), "%d ", &status);
    if( ret!=1 ) {
	mpxp_err<<"Parse error"<<std::endl;
	return -1;
    }

    switch(status) {
	case 200:
	    // Found exact match
	    ret = sscanf(http_hdr.get_body(), "%d %s %08lx %s", &status, cddb_data->category.c_str(), &(cddb_data->disc_id), album_title);
	    if( ret!=4 ) {
		mpxp_err<<"Parse error"<<std::endl;
		return -1;
	    }
	    ptr = strstr(const_cast<char*>(http_hdr.get_body()), album_title);
	    if( ptr!=NULL ) {
		char *ptr2;
		int len;
		ptr2 = strstr(ptr, "\n");
		if( ptr2==NULL ) {
		    len = http_hdr.get_body_size()-(ptr-http_hdr.get_body());
		} else {
		    len = ptr2-ptr+1;
		}
		strncpy(album_title, ptr, len);
		album_title[len-2]='\0';
	    }
	    mpxp_v<<"Parse OK, found: "<<album_title<<std::endl;
	    return cddb_request_titles(cddb_data);
	case 202:
	    // No match found
	    mpxp_err<<"Album not found"<<std::endl;
	    break;
	case 210:
	    // Found exact matches, list follows
	    ptr = strstr(const_cast<char*>(http_hdr.get_body()), "\n");
	    if( ptr==NULL ) {
		mpxp_err<<"Unable to find end of line"<<std::endl;
		return -1;
	    }
	    ptr++;
	    // We have a list of exact matches, so which one do
	    // we use? So let's take the first one.
	    ret = sscanf(ptr, "%s %08lx %s", cddb_data->category.c_str(), &(cddb_data->disc_id), album_title);
	    if( ret!=3 ) {
		mpxp_err<<"Parse error"<<std::endl;
		return -1;
	    }
	    ptr = strstr(const_cast<char*>(http_hdr.get_body()), album_title);
	    if( ptr!=NULL ) {
		char *ptr2;
		int len;
		ptr2 = strstr(ptr, "\n");
		if( ptr2==NULL ) {
		    len = (http_hdr.get_body_size())-(ptr-http_hdr.get_body());
		} else {
		    len = ptr2-ptr+1;
		}
		strncpy(album_title, ptr, len);
		album_title[len-2]='\0';
	    }
	    mpxp_v<<"Parse OK, found: "<<album_title<<std::endl;
	    return cddb_request_titles(cddb_data);
/*
body=[210 Found exact matches, list follows (until terminating `.')
misc c711930d Santana / Supernatural
rock c711930d Santana / Supernatural
blues c711930d Santana / Supernatural
.]
*/
	case 211:
	    // Found inexact matches, list follows
	    mpxp_warn<<"No exact matches found, list follows"<<std::endl;
	    break;
	default:
	    mpxp_err<<"Unhandled code"<<std::endl;
    }
    return -1;
}

static int cddb_proto_level_parse(HTTP_Header& http_hdr, cddb_data_t *cddb_data) {
    int max;
    int ret, status;
    char *ptr;

    ret = sscanf(http_hdr.get_body(), "%d ", &status);
    if( ret!=1 ) {
	mpxp_err<<"Parse error"<<std::endl;
	return -1;
    }

    switch(status) {
	case 210:
	    ptr = strstr(const_cast<char*>(http_hdr.get_body()), "max proto:");
	    if( ptr==NULL ) {
		mpxp_err<<"Parse error"<<std::endl;
		return -1;
	    }
	    ret = sscanf(ptr, "max proto: %d", &max);
	    if( ret!=1 ) {
		mpxp_err<<"Parse error"<<std::endl;
		return -1;
	    }
	    cddb_data->freedb_proto_level = max;
	    return 0;
	default:
	    mpxp_err<<"Unhandled code"<<std::endl;
    }
    return -1;
}

static int __FASTCALL__ cddb_get_proto_level(cddb_data_t *cddb_data) {
    return cddb_http_request("stat", cddb_proto_level_parse, cddb_data);
}

static int cddb_freedb_sites_parse(HTTP_Header& http_hdr, cddb_data_t *cddb_data) {
    int ret, status;
    UNUSED(cddb_data);
    ret = sscanf((const char*)http_hdr.get_body(), "%d ", &status);
    if( ret!=1 ) {
	mpxp_err<<"Parse error"<<std::endl;
	return -1;
    }

    switch(status) {
	case 210:
	    // Parse the sites
	    return 0;
	case 401:
	    mpxp_err<<"No sites information available"<<std::endl;
	    break;
	default:
	    mpxp_err<<"Unhandled code"<<std::endl;
    }
    return -1;
}

static int __FASTCALL__ cddb_get_freedb_sites(cddb_data_t *cddb_data) {
    return cddb_http_request("sites", cddb_freedb_sites_parse, cddb_data);
}

static void __FASTCALL__ cddb_create_hello(cddb_data_t *cddb_data) {
    char host_name[51];
    std::string user_name;

    if( cddb_data->anonymous ) {	// Default is anonymous
	/* Note from Eduardo P�rez Ureta <eperez@it.uc3m.es> :
	 * We don't send current user/host name in hello to prevent spam.
	 * Software that sends this is considered spyware
	 * that most people don't like.
	 */
	user_name = "anonymous";
	strcpy(host_name, "localhost");
    } else {
	if( gethostname(host_name, 50)<0 ) {
	    strcpy(host_name, "localhost");
	}
	const std::map<std::string,std::string>& envm=mpxp_get_environment();
	std::map<std::string,std::string>::const_iterator it;
	it = envm.find("LOGNAME");
	if(it!=envm.end()) user_name = (*it).second;
    }
    cddb_data->cddb_hello=std::string("&hello=")+user_name+"+"+host_name+"+"+"MPlayerXP"+"+"+VERSION;
}

static int __FASTCALL__ cddb_retrieve(cddb_data_t *cddb_data) {
	char offsets[1024], command[1024];
	char *ptr;
	unsigned idx;
	int i, time_len;

	ptr = offsets;
	for( idx=0; idx<cddb_data->tracks ; idx++ ) {
		ptr += sprintf(ptr, "%d+", cdtoc[idx].frame );
	}
	time_len = (cdtoc[cddb_data->tracks].frame)/75;

	cddb_data->freedb_server = (char*)DEFAULT_FREEDB_SERVER;
	cddb_data->freedb_proto_level = 1;
	cddb_data->xmcd_file = NULL;

	cddb_create_hello(cddb_data);
	if( cddb_get_proto_level(cddb_data)<0 ) {
		mpxp_err<<"Failed to get the protocol level"<<std::endl;
		return -1;
	}

	//cddb_get_freedb_sites(&cddb_data);

	sprintf(command, "cddb+query+%08lx+%d+%s%d", cddb_data->disc_id, cddb_data->tracks, offsets, time_len );
	i = cddb_http_request(command, cddb_query_parse, cddb_data);
	if( i<0 ) return -1;

	return 0;
}

static MPXP_Rc __FASTCALL__ cddb_resolve(libinput_t&libinput,char **xmcd_file) {
    std::string cddb_cache_dir = DEFAULT_CACHE_DIR;
    std::string home_dir;
    cddb_data_t cddb_data(libinput);

    cddb_data.tracks = read_toc();
    cddb_data.disc_id = cddb_discid(cddb_data.tracks);
    cddb_data.anonymous = 1;	// Don't send user info by default

    cddb_data.cache_dir=get_path(mpxp_get_environment(),cddb_cache_dir);
    // Check for a cached file
    if( cddb_read_cache(&cddb_data)<0 ) {
	// No Cache found
	if( cddb_retrieve(&cddb_data)<0 ) {
	    return MPXP_False;
	}
    }

    if( cddb_data.xmcd_file!=NULL ) {
	*xmcd_file = cddb_data.xmcd_file;
	return MPXP_Ok;
    }
    return MPXP_False;
}

/*******************************************************************************************************************
 *
 * xmcd parser, cd info list
 *
 *******************************************************************************************************************/

CD_Info::CD_Info() {}
CD_Info::~CD_Info() {
    cd_track_t *cd_track, *cd_track_next;
    cd_track_next = first;
    while( cd_track_next!=NULL ) {
	cd_track = cd_track_next;
	cd_track_next = cd_track->next;
	delete cd_track;
    }
}

cd_track_t* CD_Info::add_track(const std::string& track_name,
				unsigned int track_nb,
				unsigned int _min,
				unsigned int _sec,
				unsigned int _msec,
				unsigned long frame_begin,
				unsigned long frame_length) {
    cd_track_t *cd_track;

    if( track_name.empty()) return NULL;

    cd_track = new(zeromem) cd_track_t;
    if( cd_track==NULL ) {
	mpxp_fatal<<"Memory allocation failed"<<std::endl;
	return NULL;
    }
    cd_track->name=track_name;
    cd_track->track_nb = track_nb;
    cd_track->min = _min;
    cd_track->sec = _sec;
    cd_track->msec = _msec;
    cd_track->frame_begin = frame_begin;
    cd_track->frame_length = frame_length;

    if( first==NULL ) first = cd_track;
    if( last!=NULL ) last->next = cd_track;

    cd_track->prev = last;

    last = cd_track;
    current = cd_track;

    nb_tracks++;

    return cd_track;
}

cd_track_t* CD_Info::get_track(unsigned int track_nb) const {
    cd_track_t *cd_track=NULL;

    cd_track = first;
    while( cd_track!=NULL ) {
	if( cd_track->track_nb==track_nb ) return cd_track;
	cd_track = cd_track->next;
    }
    return NULL;
}

void CD_Info::print() const {
    cd_track_t *current_track;
    mpxp_info<<"================ CD INFO === start ========="<<std::endl;
    mpxp_info<<" artist=["<<artist<<"]"<<std::endl;
    mpxp_info<<" album=["<<album<<"]"<<std::endl;
    mpxp_info<<" genre=["<<genre<<"]"<<std::endl;
    mpxp_info<<" nb_tracks="<<nb_tracks<<std::endl;
    mpxp_info<<" length= "<<min<<":"<<sec<<"."<<msec<<std::endl;
    current_track = first;
    while( current_track!=NULL ) {
	mpxp_v<<"  #"<<current_track->track_nb
	    <<" "<<current_track->min
	    <<":"<<current_track->sec
	    <<"."<<current_track->msec
	    <<" @ "<<current_track->frame_begin<<"\t["
	    <<current_track->name<<"]"<<std::endl;
	current_track = current_track->next;
    }
    mpxp_info<<"================ CD INFO ===  end  ========="<<std::endl;
}

static char* __FASTCALL__ xmcd_parse_dtitle(CD_Info& cd_info,char *line) {
    char *ptr, *album;
    ptr = strstr(line, "DTITLE=");
    if( ptr!=NULL ) {
	ptr += 7;
	album = strstr(ptr, "/");
	if( album==NULL ) return NULL;
	cd_info.album=album+2;
	album--;
	album[0] = '\0';
	cd_info.artist=ptr;
    }
    return ptr;
}

char* __FASTCALL__ xmcd_parse_dgenre(CD_Info& cd_info,char *line) {
    char *ptr;
    ptr = strstr(line, "DGENRE=");
    if( ptr!=NULL ) {
	ptr += 7;
	cd_info.genre=ptr;
    }
    return ptr;
}

static char* __FASTCALL__ xmcd_parse_ttitle(CD_Info& cd_info,char *line) {
    unsigned int track_nb;
    unsigned long sec, off;
    char *ptr;
    ptr = strstr(line, "TTITLE");
    if( ptr!=NULL ) {
	ptr += 6;
	// Here we point to the track number
	track_nb = atoi(ptr);
	ptr = strstr(ptr, "=");
	if( ptr==NULL ) return NULL;
	ptr++;

	sec = cdtoc[track_nb].frame;
	off = cdtoc[track_nb+1].frame-sec+1;

	cd_info.add_track( ptr, track_nb+1, (unsigned int)(off/(60*75)), (unsigned int)((off/75)%60), (unsigned int)(off%75), sec, off );
    }
    return ptr;
}

MPXP_Rc CD_Info::parse_xmcd(const std::string& _xmcd_file) {
    int length, pos = 0;
    char *ptr;
    char *ptr2;
    unsigned int audiolen;
    if( _xmcd_file.empty() ) return MPXP_False;
    char* xmcd_file = mp_strdup(_xmcd_file.c_str());

    length = strlen(xmcd_file);
    ptr = xmcd_file;
    while( ptr!=NULL && pos<length ) {
	// Read a line
	ptr2 = ptr;
	while( ptr2[0]!='\0' && ptr2[0]!='\r' && ptr2[0]!='\n' ) ptr2++;
	if( ptr2[0]=='\0' ) break;
	ptr2[0] = '\0';
	// Ignore comments
	if( ptr[0]!='#' ) {
	    // Search for the album title
	    if( xmcd_parse_dtitle(*this, ptr) );
	    // Search for the genre
	    else if( xmcd_parse_dgenre(*this, ptr) );
	    // Search for a track title
	    else if( xmcd_parse_ttitle(*this, ptr) ){}
	}
	if( ptr2[1]=='\n' ) ptr2++;
	pos = (ptr2+1)-ptr;
	ptr = ptr2+1;
    }

    audiolen = cdtoc[nb_tracks].frame-cdtoc[0].frame;
    min  = (unsigned int)(audiolen/(60*75));
    sec  = (unsigned int)((audiolen/75)%60);
    msec = (unsigned int)(audiolen%75);
    delete xmcd_file;

    return MPXP_Ok;
}

MPXP_Rc CDD_Interface::open_cddb(libinput_t& libinput,const std::string& dev, const std::string& track) {
    char *xmcd_file = NULL;
    MPXP_Rc ret;

    ret = cddb_resolve(libinput,&xmcd_file);
    if( ret==MPXP_False ) {
	CD_Info& cd_info = *new(zeromem) CD_Info;
	if(cd_info.parse_xmcd(xmcd_file)==MPXP_Ok)
	    cd_info.print();
	delete xmcd_file;
	delete &cd_info;
	return ret;
    }
    return open_cdda(dev, track);
}
} // namespace	usr
#endif
