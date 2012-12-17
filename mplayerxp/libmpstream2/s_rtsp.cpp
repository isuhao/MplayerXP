#include "mp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;

#include <stdlib.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#ifndef HAVE_WINSOCK2
#include <sys/socket.h>
#define closesocket close
#else
#include <winsock2.h>
#endif

#include "stream.h"
#include "stream_internal.h"
#include "help_mp.h"
#include "url.h"
#include "tcp.h"
#include "network.h"
#include "librtsp/rtsp.h"
#include "librtsp/rtsp_session.h"
#include "stream_msg.h"

namespace mpxp {
    class Rtsp_Stream_Interface : public Stream_Interface {
	public:
	    Rtsp_Stream_Interface(libinput_t& libinput);
	    virtual ~Rtsp_Stream_Interface();

	    virtual MPXP_Rc	open(const std::string& filename,unsigned flags);
	    virtual int		read(stream_packet_t * sp);
	    virtual off_t	seek(off_t off);
	    virtual off_t	tell() const;
	    virtual void	close();
	    virtual MPXP_Rc	ctrl(unsigned cmd,any_t* param);
	    virtual Stream::type_e type() const;
	    virtual off_t	size() const;
	    virtual off_t	sector_size() const;
	    virtual std::string mime_type() const;
	private:
	    MPXP_Rc		start ();

	    networking_t*	networking;
	    Tcp			tcp;
    };

Rtsp_Stream_Interface::Rtsp_Stream_Interface(libinput_t& _libinput)
			:Stream_Interface(_libinput),
			tcp(_libinput,-1) {}
Rtsp_Stream_Interface::~Rtsp_Stream_Interface() {}

#define RTSP_DEFAULT_PORT 554

int Rtsp_Stream_Interface::read(stream_packet_t*sp)
{
    Rtsp_Session& rtsp = *static_cast<Rtsp_Session*>(networking->data);
    return rtsp.read (tcp, sp->buf, sp->len);
}

off_t Rtsp_Stream_Interface::seek(off_t newpos) { return newpos; }
off_t Rtsp_Stream_Interface::tell() const { return 0; }

MPXP_Rc Rtsp_Stream_Interface::ctrl(unsigned cmd,any_t*args)
{
    UNUSED(cmd);
    UNUSED(args);
    return MPXP_Unknown;
}

void Rtsp_Stream_Interface::close()
{
    Rtsp_Session* rtsp = static_cast<Rtsp_Session*>(networking->data);
    if (rtsp) rtsp->end ();
    delete networking->url;
    free_networking(networking);
    networking=NULL;
}

MPXP_Rc Rtsp_Stream_Interface::start()
{
    Rtsp_Session *rtsp;
    char *mrl;
    char *file;
    int port;
    int redirected, temp;

    /* counter so we don't get caught in infinite redirections */
    temp = 5;

    do {
	redirected = 0;

	tcp.open(networking->url->hostname,
		port = (networking->url->port ?
			networking->url->port :
			RTSP_DEFAULT_PORT));
	if (!tcp.established() && !networking->url->port)
	    tcp.open(networking->url->hostname,
			port = 7070);
	if (!tcp.established()) return MPXP_False;
	file = networking->url->file;
	if (file[0] == '/') file++;

	mrl = new char [strlen (networking->url->hostname) + strlen (file) + 16];

	sprintf (mrl, "rtsp://%s:%i/%s",networking->url->hostname, port, file);

	rtsp = rtsp_session_start (tcp, &mrl, file,
			networking->url->hostname,
			port, &redirected,
			networking->bandwidth,
			networking->url->username,
			networking->url->password);
	if (redirected == 1) {
	    delete networking->url;
	    networking->url = url_new (mrl);
	    tcp.close();
	}
	delete mrl;
	temp--;
    } while ((redirected != 0) && (temp > 0));

    if (!rtsp) return MPXP_False;

    networking->data = rtsp;

    networking->prebuffer_size = 128*1024;  // 640 KBytes
    networking->buffering = 1;
    networking->status = networking_playing_e;

    return MPXP_Ok;
}

extern int index_mode;
MPXP_Rc Rtsp_Stream_Interface::open(const std::string& filename,unsigned flags)
{
    URL *url;
    UNUSED(flags);
    if(filename.substr(0,7)!="rtsp://") return MPXP_False;

    MSG_V("STREAM_RTSP, URL: %s\n", filename.c_str());
    networking = new_networking();
    if (!networking) return MPXP_False;

    networking->bandwidth = net_conf.bandwidth;
    url = url_new (filename);
    networking->url = check4proxies (url);

    tcp.close();
    index_mode = -1; /* prevent most RTSP streams from locking due to -idx */
    if (start() != MPXP_Ok) {
	free_networking(networking);
	networking = NULL;
	return MPXP_False;
    }

    fixup_network_stream_cache (networking);
    return MPXP_Ok;
}
Stream::type_e Rtsp_Stream_Interface::type() const { return Stream::Type_Stream; }
off_t	Rtsp_Stream_Interface::size() const { return 0; }
off_t	Rtsp_Stream_Interface::sector_size() const { return 1; }
std::string Rtsp_Stream_Interface::mime_type() const { return "application/octet-stream"; }

static Stream_Interface* query_interface(libinput_t& libinput) { return new(zeromem) Rtsp_Stream_Interface(libinput); }

/* "reuse a bit of code from ftplib written by Thomas Pfau", */
extern const stream_interface_info_t rtsp_stream =
{
    "rtsp",
    "reads multimedia stream from Real Time Streaming Protocol (RTSP)",
    query_interface
};
} // namespace mpxp