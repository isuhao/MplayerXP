#ifndef __NETWORK_RP_H_INCLUDED
#define __NETWORK_RP_H_INCLUDED

#include "network_nop.h"
#include "rtp_cache.h"

namespace	usr {
    struct Rtp_Networking : public Nop_Networking {
	public:
	    virtual ~Rtp_Networking();

	    static Networking*	start(Tcp& tcp, network_protocol_t& protocol,int raw_udp);
	    virtual int read( Tcp& fd, char *buffer, int buffer_size);
	    virtual int seek( Tcp& fd, off_t pos);
	private:
	    Rtp_Networking();
	    Rtp_Cache* rtp;
    };
} // namespace	usr
#endif
