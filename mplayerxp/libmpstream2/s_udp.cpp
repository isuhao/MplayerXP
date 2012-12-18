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
#include "tcp.h"
#include "network_nop.h"
#include "udp.h"
#include "url.h"
#include "stream_msg.h"

namespace mpxp {
    class Udp_Stream_Interface : public Stream_Interface {
	public:
	    Udp_Stream_Interface(libinput_t& libinput);
	    virtual ~Udp_Stream_Interface();

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
	    MPXP_Rc		start (const URL&,unsigned);

	    Networking*		networking;
	    Udp			udp;
	    Tcp			tcp;
    };

Udp_Stream_Interface::Udp_Stream_Interface(libinput_t& libinput)
			:Stream_Interface(libinput),
			udp(-1),
			tcp(libinput,-1) {}
Udp_Stream_Interface::~Udp_Stream_Interface() {}

int Udp_Stream_Interface::read(stream_packet_t*sp)
{
  return networking->read(tcp,sp->buf,sp->len);
}

off_t Udp_Stream_Interface::seek(off_t newpos) { return newpos; }
off_t Udp_Stream_Interface::tell() const { return 0; }

MPXP_Rc Udp_Stream_Interface::ctrl(unsigned cmd,any_t*args)
{
    UNUSED(cmd);
    UNUSED(args);
    return MPXP_Unknown;
}

void Udp_Stream_Interface::close()
{
    delete networking;
    networking=NULL;
}

MPXP_Rc Udp_Stream_Interface::start (const URL& url,unsigned bandwidth)
{
    if (!udp.established()) {
	udp.open(networking->url);
	if (!udp.established()) return MPXP_False;
    }
    tcp=udp.socket();
    network_protocol_t net_protocol;
    net_protocol.url=url;
    net_protocol.mime="application/octet-stream";
    Nop_Networking* rv = new(zeromem) Nop_Networking;
    rv->start(tcp,net_protocol);
    rv->bandwidth = bandwidth;
    return MPXP_Ok;
}

MPXP_Rc Udp_Stream_Interface::open(const std::string& filename,unsigned flags)
{
    URL url;
    UNUSED(flags);
    MSG_V("STREAM_UDP, URL: %s\n", filename.c_str());

    url.redirect(filename);
    url.check4proxies ();
    if (url.port() == 0) {
	MSG_ERR("You must enter a port number for UDP streams!\n");
	return MPXP_False;
    }
    if (start(url,net_conf.bandwidth) != MPXP_Ok) {
	MSG_ERR("udp_networking_start failed\n");
	return MPXP_False;
    }
    networking->fixup_cache ();
    return MPXP_Ok;
}
Stream::type_e Udp_Stream_Interface::type() const { return Stream::Type_Stream; }
off_t	Udp_Stream_Interface::size() const { return 0; }
off_t	Udp_Stream_Interface::sector_size() const { return 1; }
std::string Udp_Stream_Interface::mime_type() const { return "application/octet-stream"; }

static Stream_Interface* query_interface(libinput_t& libinput) { return new(zeromem) Udp_Stream_Interface(libinput); }

/* "reuse a bit of code from ftplib written by Thomas Pfau", */
extern const stream_interface_info_t udp_stream =
{
    "udp://",
    "reads multimedia stream directly from User Datagram Protocol (UDP)",
    query_interface
};
} // namespace mpxp

