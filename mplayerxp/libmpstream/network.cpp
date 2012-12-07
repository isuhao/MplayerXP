#include "mp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;
/*
 * Network layer for MPlayer
 * by Bertrand BAUDET <bertrand_baudet@yahoo.com>
 * (C) 2001, MPlayer team.
 */

//#define DUMP2FILE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

#include <errno.h>
#include <ctype.h>

#include "mplayerxp.h"
#include "asf_streaming.h"
#ifndef HAVE_WINSOCK2
#define closesocket close
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "stream.h"
#include "libmpdemux/demuxer.h"
#include "libmpconf/cfgparser.h"
#include "libmpdemux/mpdemux.h"
#include "help_mp.h"

#include "tcp.h"
#include "network.h"
#include "http.h"
#include "cookies.h"
#include "url.h"
#include "udp.h"
#include "libmpdemux/asf.h"
#include "pnm.h"
#ifndef STREAMING_LIVE_DOT_COM
#include "rtp.h"
#endif
#include "version.h"
#include "stream_msg.h"

/* Variables for the command line option -user, -passwd & -bandwidth */
char*	network_username=NULL;
char*	network_password=NULL;
int	network_bandwidth=0;
int	network_cookies_enabled = 0;
char*	network_useragent=NULL;

/* IPv6 options */
int	network_prefer_ipv4 = 1;
int	network_ipv4_only_proxy = 0;

static const struct {
    const char *mime_type;
    int demuxer_type;
} mime_type_table[] = {
    // MP3 networking, some MP3 networking server answer with audio/mpeg
    { "audio/mpeg", Demuxer::Type_AUDIO },
    // MPEG networking
    { "video/mpeg", Demuxer::Type_UNKNOWN },
    { "video/x-mpeg", Demuxer::Type_UNKNOWN },
    { "video/x-mpeg2", Demuxer::Type_UNKNOWN },
    // AVI ??? => video/x-msvideo
    { "video/x-msvideo", Demuxer::Type_AVI },
    // MOV => video/quicktime
    { "video/quicktime", Demuxer::Type_MOV },
    // ASF
    { "audio/x-ms-wax", Demuxer::Type_ASF },
    { "audio/x-ms-wma", Demuxer::Type_ASF },
    { "video/x-ms-asf", Demuxer::Type_ASF },
    { "video/x-ms-afs", Demuxer::Type_ASF },
    { "video/x-ms-wvx", Demuxer::Type_ASF },
    { "video/x-ms-wmv", Demuxer::Type_ASF },
    { "video/x-ms-wma", Demuxer::Type_ASF },
    // Playlists
    { "video/x-ms-wmx", Demuxer::Type_PLAYLIST },
    { "audio/x-scpls", Demuxer::Type_PLAYLIST },
    { "audio/x-mpegurl", Demuxer::Type_PLAYLIST },
    { "audio/x-pls", Demuxer::Type_PLAYLIST },
    // Real Media
    { "audio/x-pn-realaudio", Demuxer::Type_REAL },
    // OGG Streaming
    { "application/x-ogg", Demuxer::Type_OGG },
    // NullSoft Streaming Video
    { "video/nsv", Demuxer::Type_NSV},
    { "misc/ultravox", Demuxer::Type_NSV}
};

networking_t* new_networking(libinput_t*libinput) {
    networking_t *networking = new(zeromem) networking_t;
    if( networking==NULL ) {
	MSG_FATAL(MSGTR_OutOfMemory);
	return NULL;
    }
    networking->libinput=libinput;
    return networking;
}

void free_networking( networking_t *networking ) {
    if( networking==NULL ) return;
    if( networking->url ) url_free( networking->url );
    if( networking->buffer ) delete networking->buffer ;
    if( networking->data ) delete networking->data ;
    delete networking;
}

URL_t*
check4proxies( URL_t *url ) {
	URL_t *url_out = NULL;
	if( url==NULL ) return NULL;
	url_out = url_new( url->url );
	if( !strcasecmp(url->protocol, "http_proxy") ) {
		MSG_V("Using HTTP proxy: http://%s:%d\n", url->hostname, url->port );
		return url_out;
	}
	// Check if the http_proxy environment variable is set.
	if( !strcasecmp(url->protocol, "http") ) {
		char *proxy;
		proxy = getenv("http_proxy");
		if( proxy!=NULL ) {
			// We got a proxy, build the URL to use it
			int len;
			char *new_url;
			URL_t *tmp_url;
			URL_t *proxy_url = url_new( proxy );

			if( proxy_url==NULL ) {
				MSG_WARN("Invalid proxy setting...Trying without proxy.\n");
				return url_out;
			}

#ifdef HAVE_AF_INET6
			if (network_ipv4_only_proxy && (gethostbyname(url->hostname)==NULL)) {
				MSG_WARN(
					"Could not find resolve remote hostname for AF_INET. Trying without proxy.\n");
				return url_out;
			}
#endif

			MSG_V("Using HTTP proxy: %s\n", proxy_url->url );
			len = strlen( proxy_url->hostname ) + strlen( url->url ) + 20;	// 20 = http_proxy:// + port
			new_url = new char [len+1];
			if( new_url==NULL ) {
				MSG_FATAL(MSGTR_OutOfMemory);
				return url_out;
			}
			sprintf(new_url, "http_proxy://%s:%d/%s", proxy_url->hostname, proxy_url->port, url->url );
			tmp_url = url_new( new_url );
			if( tmp_url==NULL ) {
				return url_out;
			}
			url_free( url_out );
			url_out = tmp_url;
			delete new_url ;
			url_free( proxy_url );
		}
	}
	return url_out;
}

Tcp* http_send_request(libinput_t* libinput, URL_t *url, off_t pos ) {
	HTTP_header_t *http_hdr;
	URL_t *server_url;
	char str[256];
	Tcp* tcp=NULL;
	int ret;
	int proxy = 0;		// Boolean

	http_hdr = http_new_header();

	if( !strcasecmp(url->protocol, "http_proxy") ) {
		proxy = 1;
		server_url = url_new( (url->file)+1 );
		http_set_uri( http_hdr, server_url->url );
	} else {
		server_url = url;
		http_set_uri( http_hdr, server_url->file );
	}
	if (server_url->port && server_url->port != 80)
	    snprintf(str, 256, "Host: %s:%d", server_url->hostname, server_url->port );
	else
	    snprintf(str, 256, "Host: %s", server_url->hostname );
	http_set_field( http_hdr, str);
	if (network_useragent)
	{
	    snprintf(str, 256, "User-Agent: %s", network_useragent);
	    http_set_field(http_hdr, str);
	}
	else
	    http_set_field( http_hdr, "User-Agent: MPlayerXP/"VERSION);

	http_set_field(http_hdr, "Icy-MetaData: 1");

	if(pos>0) {
	// Extend http_send_request with possibility to do partial content retrieval
	    snprintf(str, 256, "Range: bytes=%d-", (int)pos);
	    http_set_field(http_hdr, str);
	}

	if (network_cookies_enabled) cookies_set( http_hdr, server_url->hostname, server_url->url );

	http_set_field( http_hdr, "Connection: closed");
	http_add_basic_authentication( http_hdr, url->username, url->password );
	if( http_build_request( http_hdr )==NULL ) {
		goto err_out;
	}

	if( proxy ) {
		if( url->port==0 ) url->port = 8080;			// Default port for the proxy server
		tcp = new Tcp(libinput, url->hostname, url->port, Tcp::IP4);
		url_free( server_url );
		server_url = NULL;
	} else {
		if( server_url->port==0 ) server_url->port = 80;	// Default port for the web server
		tcp = new Tcp(libinput, server_url->hostname, server_url->port, Tcp::IP4);
	}
	if(!tcp->established()) goto err_out;
	MSG_DBG2("Request: [%s]\n", http_hdr->buffer );

	ret = tcp->write((uint8_t*)(http_hdr->buffer), http_hdr->buffer_size);
	if( ret!=(int)http_hdr->buffer_size ) {
		MSG_ERR("Error while sending HTTP request: didn't sent all the request\n");
		goto err_out;
	}

	http_free( http_hdr );

	return tcp;
err_out:
	if (tcp) delete tcp;
	http_free(http_hdr);
	if (proxy && server_url)
		url_free(server_url);
	return NULL;
}

HTTP_header_t* http_read_response( Tcp& tcp ) {
	HTTP_header_t *http_hdr;
	char response[BUFFER_SIZE];
	int i;

	http_hdr = http_new_header();
	if( http_hdr==NULL ) {
		return NULL;
	}

	do {
		i = tcp.read((uint8_t*)response, BUFFER_SIZE);
		if( i<0 ) {
			MSG_ERR("Read failed\n");
			http_free( http_hdr );
			return NULL;
		}
		if( i==0 ) {
			MSG_ERR("http_read_response read 0 -ie- EOF\n");
			http_free( http_hdr );
			return NULL;
		}
		http_response_append( http_hdr, response, i );
	} while( !http_is_header_entire( http_hdr ) );
	http_response_parse( http_hdr );
	return http_hdr;
}

int
http_authenticate(HTTP_header_t *http_hdr, URL_t *url, int *auth_retry) {
	char *aut;

	if( *auth_retry==1 ) {
		MSG_ERR(MSGTR_ConnAuthFailed);
		return -1;
	}
	if( *auth_retry>0 ) {
		if( url->username ) {
			delete url->username ;
			url->username = NULL;
		}
		if( url->password ) {
			delete url->password ;
			url->password = NULL;
		}
	}

	aut = http_get_field(http_hdr, "WWW-Authenticate");
	if( aut!=NULL ) {
		char *aut_space;
		aut_space = strstr(aut, "realm=");
		if( aut_space!=NULL ) aut_space += 6;
		MSG_INFO("Authentication required for %s\n", aut_space);
	} else {
		MSG_INFO("Authentication required\n");
	}
	if( network_username ) {
		url->username = mp_strdup(network_username);
		if( url->username==NULL ) {
			MSG_FATAL(MSGTR_OutOfMemory);
			return -1;
		}
	} else {
		MSG_ERR(MSGTR_ConnAuthFailed);
		return -1;
	}
	if( network_password ) {
		url->password = mp_strdup(network_password);
		if( url->password==NULL ) {
			MSG_FATAL(MSGTR_OutOfMemory);
			return -1;
		}
	} else {
		MSG_INFO("No password provided, trying blank password\n");
	}
	(*auth_retry)++;
	return 0;
}

off_t http_seek(Tcp& tcp, networking_t *networking, off_t pos ) {
    HTTP_header_t *http_hdr = NULL;
    Tcp* other_tcp;

    tcp.close();
    other_tcp = http_send_request(networking->libinput, networking->url, pos );
    if(! other_tcp->established() ) return 0;
    tcp=*other_tcp;
    delete other_tcp;

    http_hdr = http_read_response(tcp);

    if( http_hdr==NULL ) return 0;

    switch( http_hdr->status_code ) {
	case 200:
	case 206: // OK
	    MSG_V("Content-Type: [%s]\n", http_get_field(http_hdr, "Content-Type") );
	    MSG_V("Content-Length: [%s]\n", http_get_field(http_hdr, "Content-Length") );
	    if( http_hdr->body_size>0 ) {
		if( networking_bufferize( networking, http_hdr->body, http_hdr->body_size )<0 ) {
		    http_free( http_hdr );
		    return 0;
		}
	    }
	    break;
	default:
	    MSG_ERR("Server return %d: %s\n", http_hdr->status_code, http_hdr->reason_phrase );
	    tcp.close();
    }

    if( http_hdr ) {
	http_free( http_hdr );
	networking->data = NULL;
    }

    return pos;
}

// By using the protocol, the extension of the file or the content-type
// we might be able to guess the networking type.
static MPXP_Rc autodetectProtocol(networking_t *networking, Tcp& tcp) {
    HTTP_header_t *http_hdr=NULL;
    unsigned int i;
    int redirect;
    int auth_retry=0;
    MPXP_Rc seekable=MPXP_False;
    char *extension;
    char *content_type;
    char *next_url;

    URL_t *url = networking->url;

    do {
	next_url = NULL;
	extension = NULL;
	content_type = NULL;
	redirect = 0;

	if( url==NULL ) {
	    goto err_out;
	}

#ifndef STREAMING_LIVE_DOT_COM
	// Old, hacked RTP support, which works for MPEG Program Streams
	//   RTP streams only:
	// Checking for RTP
	if( !strcasecmp(url->protocol, "rtp") ) {
	    if( url->port==0 ) {
		MSG_ERR("You must enter a port number for RTP streams!\n");
		goto err_out;
	    }
	    return MPXP_Ok;
	}
#endif
	// HTTP based protocol
	if( !strcasecmp(url->protocol, "http") || !strcasecmp(url->protocol, "http_proxy") ) {
	    tcp = *http_send_request(networking->libinput, url, 0 );
	    if(!tcp.established()) goto err_out;

	    http_hdr = http_read_response(tcp);
	    if( http_hdr==NULL ) goto err_out;
	    if( mp_conf.verbose ) http_debug_hdr( http_hdr );
	    networking->data = (any_t*)http_hdr;

	    // Check if we can make partial content requests and thus seek in http-streams
	    if( http_hdr!=NULL && http_hdr->status_code==200 ) {
		    char *accept_ranges;
		    if( (accept_ranges = http_get_field(http_hdr,"Accept-Ranges")) != NULL )
			seekable = strncmp(accept_ranges,"bytes",5)==0?MPXP_Ok:MPXP_False;
	    }
	    // Check if the response is an ICY status_code reason_phrase
	    if( !strcasecmp(http_hdr->protocol, "ICY") ) {
		switch( http_hdr->status_code ) {
		    case 200: { // OK
			char *field_data = NULL;
			// note: I skip icy-notice1 and 2, as they contain html <BR>
			// and are IMHO useless info ::atmos
			if( (field_data = http_get_field(http_hdr, "icy-name")) != NULL )
			    MSG_INFO("Name   : %s\n", field_data); field_data = NULL;
			if( (field_data = http_get_field(http_hdr, "icy-genre")) != NULL )
			    MSG_INFO("Genre  : %s\n", field_data); field_data = NULL;
			if( (field_data = http_get_field(http_hdr, "icy-url")) != NULL )
			    MSG_INFO("Website: %s\n", field_data); field_data = NULL;
			// XXX: does this really mean public server? ::atmos
			if( (field_data = http_get_field(http_hdr, "icy-pub")) != NULL )
			    MSG_INFO("Public : %s\n", atoi(field_data)?"yes":"no"); field_data = NULL;
			if( (field_data = http_get_field(http_hdr, "icy-br")) != NULL )
			    MSG_INFO("Bitrate: %skbit/s\n", field_data); field_data = NULL;
			return MPXP_Ok;
		    }
		    case 400: // Server Full
			MSG_ERR("Error: ICY-Server is full, skipping!\n");
			goto err_out;
		    case 401: // Service Unavailable
			MSG_ERR("Error: ICY-Server return service unavailable, skipping!\n");
			goto err_out;
		    case 403: // Service Forbidden
			MSG_ERR("Error: ICY-Server return 'Service Forbidden'\n");
			goto err_out;
		    case 404: // Resource Not Found
			MSG_ERR("Error: ICY-Server couldn't find requested stream, skipping!\n");
			goto err_out;
		    default:
			MSG_ERR("Error: unhandled ICY-Errorcode, contact MPlayer developers!\n");
			goto err_out;
		}
	    }

	    // Assume standard http if not ICY
	    switch( http_hdr->status_code ) {
		case 200: // OK
		    // Look if we can use the Content-Type
		    content_type = http_get_field( http_hdr, "Content-Type" );
		    if( content_type!=NULL ) {
			const char *content_length = NULL;
			MSG_V("Content-Type: [%s]\n", content_type );
			if( (content_length = http_get_field(http_hdr, "Content-Length")) != NULL)
			    MSG_V("Content-Length: [%s]\n", http_get_field(http_hdr, "Content-Length"));
			// Check in the mime type table for a demuxer type
			for( i=0 ; i<(sizeof(mime_type_table)/sizeof(mime_type_table[0])) ; i++ ) {
			    if( !strcasecmp( content_type, mime_type_table[i].mime_type ) ) {
				return seekable;
			    }
			}
		    }
		    // Not found in the mime type table, don't fail,
		    // we should try raw HTTP
		    return MPXP_Ok;
		// Redirect
		    case 301: // Permanently
		    case 302: // Temporarily
			// TODO: RFC 2616, recommand to detect infinite redirection loops
			next_url = http_get_field( http_hdr, "Location" );
			if( next_url!=NULL ) {
			    networking->url = url = url_redirect( &url, next_url );
			    if (!strcasecmp(url->protocol, "mms")) goto err_out;
			    if (strcasecmp(url->protocol, "http")) {
				MSG_WARN("Unsupported http %d redirect to %s protocol\n", http_hdr->status_code, url->protocol);
				goto err_out;
			    }
			    redirect = 1;
			}
			break;
		    case 401: // Authentication required
			if( http_authenticate(http_hdr, url, &auth_retry)<0 ) goto err_out;
			redirect = 1;
			break;
		    default:
			MSG_ERR("Server returned %d: %s\n", http_hdr->status_code, http_hdr->reason_phrase );
			goto err_out;
	    }
	} else {
	    MSG_ERR("Unknown protocol '%s'\n", url->protocol );
	    goto err_out;
	}
    } while( redirect );
err_out:
    http_free( http_hdr );
    http_hdr = NULL;

    return MPXP_False;
}

int
networking_bufferize( networking_t *networking,unsigned char *buffer, int size) {
//printf("networking_bufferize\n");
    networking->buffer = new char [size];
    if( networking->buffer==NULL ) {
	MSG_FATAL(MSGTR_OutOfMemory);
	return -1;
    }
    memcpy( networking->buffer, buffer, size );
    networking->buffer_size = size;
    return size;
}

int
nop_networking_read(Tcp& tcp, char *buffer, int size, networking_t *stream_ctrl ) {
    int len=0;
//printf("nop_networking_read\n");
    if( stream_ctrl->buffer_size!=0 ) {
	int buffer_len = stream_ctrl->buffer_size-stream_ctrl->buffer_pos;
//printf("%d bytes in buffer\n", stream_ctrl->buffer_size);
	len = (size<buffer_len)?size:buffer_len;
	memcpy( buffer, (stream_ctrl->buffer)+(stream_ctrl->buffer_pos), len );
	stream_ctrl->buffer_pos += len;
//printf("buffer_pos = %d\n", stream_ctrl->buffer_pos );
	if( stream_ctrl->buffer_pos>=stream_ctrl->buffer_size ) {
	    delete stream_ctrl->buffer ;
	    stream_ctrl->buffer = NULL;
	    stream_ctrl->buffer_size = 0;
	    stream_ctrl->buffer_pos = 0;
//printf("buffer cleaned\n");
	}
//printf("read %d bytes from buffer\n", len );
    }
    if( len<size ) {
	int ret;
	ret = tcp.read((uint8_t*)(buffer+len), size-len);
	if( ret<0 ) {
	    MSG_ERR("nop_networking_read error : %s\n",strerror(errno));
	}
	len += ret;
//printf("read %d bytes from network\n", len );
    }
    return len;
}

int
nop_networking_seek(Tcp& tcp, off_t pos, networking_t *n ) {
    UNUSED(tcp);
    UNUSED(pos);
    UNUSED(n);
    return -1;
}

MPXP_Rc nop_networking_start(Tcp& tcp,networking_t* networking ) {
    HTTP_header_t *http_hdr = NULL;
    char *next_url=NULL;
    URL_t *rd_url=NULL;
    MPXP_Rc ret;

    if( !tcp.established() ) {
	tcp = *http_send_request(networking->libinput, networking->url,0);
	if( !tcp.established() ) return MPXP_False;
	http_hdr = http_read_response(tcp);
	if( http_hdr==NULL ) return MPXP_False;

	switch( http_hdr->status_code ) {
	    case 200: // OK
		MSG_V("Content-Type: [%s]\n", http_get_field(http_hdr, "Content-Type") );
		MSG_V("Content-Length: [%s]\n", http_get_field(http_hdr, "Content-Length") );
		if( http_hdr->body_size>0 ) {
		    if( networking_bufferize( networking, http_hdr->body, http_hdr->body_size )<0 ) {
			http_free( http_hdr );
			return MPXP_False;
		    }
		}
		break;
	    // Redirect
	    case 301: // Permanently
	    case 302: // Temporarily
		ret=MPXP_False;
		next_url = http_get_field( http_hdr, "Location" );

		if (next_url != NULL)
		    rd_url=url_new(next_url);

		if (next_url != NULL && rd_url != NULL) {
		    MSG_STATUS("Redirected: Using this url instead %s\n",next_url);
		    networking->url=check4proxies(rd_url);
		    ret=nop_networking_start(tcp,networking); //recursively get networking started
		} else {
		    MSG_ERR("Redirection failed\n");
		    tcp.close();
		}
		return ret;
		break;
	    case 401: //Authorization required
	    case 403: //Forbidden
	    case 404: //Not found
	    case 500: //Server Error
	    default:
		MSG_ERR("Server return %d: %s\n", http_hdr->status_code, http_hdr->reason_phrase );
		tcp.close();
		return MPXP_False;
		break;
	}
    } else {
	http_hdr = (HTTP_header_t*)networking->data;
	if( http_hdr->body_size>0 ) {
	    if( networking_bufferize( networking, http_hdr->body, http_hdr->body_size )<0 ) {
		http_free( http_hdr );
		networking->data = NULL;
		return MPXP_False;
	    }
	}
    }

    if( http_hdr ) {
	http_free( http_hdr );
	networking->data = NULL;
    }

    networking->networking_read = nop_networking_read;
    networking->networking_seek = nop_networking_seek;
    networking->prebuffer_size = 64*1024;	// KBytes
    networking->buffering = 1;
    networking->status = networking_playing_e;
    return MPXP_Ok;
}

void fixup_network_stream_cache(networking_t *networking) {
  if(networking->buffering) {
    if(mp_conf.s_cache_size<0) {
      // cache option not set, will use our computed value.
      // buffer in KBytes, *5 because the prefill is 20% of the buffer.
      mp_conf.s_cache_size = (networking->prebuffer_size/1024)*5;
      if( mp_conf.s_cache_size<64 ) mp_conf.s_cache_size = 64;	// 16KBytes min buffer
    }
    MSG_INFO("[network] cache size set to: %i\n", mp_conf.s_cache_size);
  }
}

int
pnm_networking_read(Tcp& tcp, char *buffer, int size, networking_t *stream_ctrl ) {
    UNUSED(tcp);
    return pnm_read(reinterpret_cast<pnm_t*>(stream_ctrl->data), buffer, size);
}

MPXP_Rc pnm_networking_start(Tcp& tcp,networking_t *networking ) {
    pnm_t *pnm;

    tcp.open(networking->libinput, networking->url->hostname,
	    networking->url->port ? networking->url->port : 7070);
    if(!tcp.established()) return MPXP_False;

    pnm = pnm_connect(tcp,networking->url->file);
    if(!pnm) return MPXP_NA;

    networking->data=pnm;

    networking->networking_read = pnm_networking_read;
    networking->prebuffer_size = 8*1024;  // 8 KBytes
    networking->buffering = 1;
    networking->status = networking_playing_e;
    return MPXP_Ok;
}

#ifdef HAVE_RTSP_SESSION_H
int
realrtsp_networking_read( int fd, char *buffer, int size, networking_t *stream_ctrl ) {
    return rtsp_session_read(stream_ctrl->data, buffer, size);
}

MPXP_Rc realrtsp_networking_start( net_fd_t* fd, networking_t *stream ) {
    rtsp_session_t *rtsp;
    char *mrl;
    char *file;
    int port;
    int redirected, temp;
    if( stream==NULL ) return MPXP_False;

    temp = 5; // counter so we don't get caught in infinite redirections (you never know)

    do {
	redirected = 0;
	port = networking->url->port ? networking->url->port : 554;
	*fd = tcp_connect2Server( networking->url->hostname, port, 1);
	if(*fd<0 && !networking->url->port)
		*fd = tcp_connect2Server( networking->url->hostname,port = 7070, 1 );
	if(*fd<0) return MPXP_False;

	file = networking->url->file;
	if (file[0] == '/') file++;
	mrl = mp_malloc(sizeof(char)*(strlen(networking->url->hostname)+strlen(file)+16));
	sprintf(mrl,"rtsp://%s:%i/%s",networking->url->hostname,port,file);
	rtsp = rtsp_session_start(fd,&mrl, file,
			networking->url->hostname, port, &redirected);

	if ( redirected == 1 ) {
	    url_free(networking->url);
	    networking->url = url_new(mrl);
	    closesocket(fd);
	}
	delete mrl;
	temp--;

    } while( (redirected != 0) && (temp > 0) );

    if(!rtsp) return MPXP_False;

    networking->data=rtsp;

    networking->networking_read = realrtsp_networking_read;
    networking->prebuffer_size = 128*1024;  // 8 KBytes
    networking->buffering = 1;
    networking->status = networking_playing_e;
    return MPXP_Ok;
}
#endif // HAVE_RTSP_SESSION_H


#ifndef STREAMING_LIVE_DOT_COM

static int
rtp_networking_read(Tcp& tcp, char *buffer, int size, networking_t *networking ) {
    UNUSED(networking);
    return read_rtp_from_server(tcp, buffer, size );
}

static MPXP_Rc rtp_networking_start(Tcp& tcp,networking_t* networking, int raw_udp ) {

    if( !tcp.established() ) {
	Udp* udp(new(zeromem) Udp(networking->url));
	tcp = udp->socket();
	if( !tcp.established()) return MPXP_False;
    }

    if(raw_udp)
	networking->networking_read = nop_networking_read;
    else
	networking->networking_read = rtp_networking_read;
    networking->networking_read = rtp_networking_read;
    networking->networking_seek = nop_networking_seek;
    networking->prebuffer_size = 64*1024;	// KBytes
    networking->buffering = 0;
    networking->status = networking_playing_e;
    return MPXP_Ok;
}
#endif

MPXP_Rc networking_start(Tcp& tcp,networking_t* networking, URL_t *url) {
    MPXP_Rc rc;

    networking->url = check4proxies( url );

    rc = autodetectProtocol( networking, tcp);

    if( rc!=MPXP_Ok ) return MPXP_False;
    rc = MPXP_False;

    // Get the bandwidth available
    networking->bandwidth = network_bandwidth;

    // For RTP streams, we usually don't know the stream type until we open it.
    if( !strcasecmp( networking->url->protocol, "rtp")) {
	if(tcp.established()) tcp.close();
	rc = rtp_networking_start(tcp, networking, 0);
    } else if( !strcasecmp( networking->url->protocol, "pnm")) {
	tcp.close();
	rc = pnm_networking_start(tcp, networking);
	if (rc == MPXP_False) {
	    MSG_INFO("Can't connect with pnm, retrying with http.\n");
	    return MPXP_False;
	}
    }
#ifdef HAVE_RTSP_SESSION_H
    else if( !strcasecmp( networking->url->protocol, "rtsp")) {
	if ((rc = realrtsp_networking_start( tcp, networking )) < 0) {
	    MSG_INFO("Not a Realmedia rtsp url. Trying standard rtsp protocol.\n");
#ifdef STREAMING_LIVE_DOT_COM
	    rc = rtsp_networking_start( tcp, networking );
	    if(rc==MPXP_FAlse ) MSG_ERR("rtsp_networking_start failed\n");
	    return rc;
#else
	    MSG_ERR("RTSP support requires the \"LIVE.COM Streaming Media\" libraries!\n");
	    return MPXP_False;
#endif
	}
    }
#endif
    else if(!strcasecmp( networking->url->protocol, "udp")) {
	tcp.close();
	rc = rtp_networking_start(tcp, networking, 1);
	if(rc==MPXP_False) {
	    MSG_ERR("rtp_networking_start(udp) failed\n");
	    return MPXP_False;
	}
    } else {
	// Send the appropriate HTTP request
	// Need to filter the network stream.
	// ASF raw stream is encapsulated.
	// It can also be a playlist (redirector)
	// so we need to pass demuxer_type too
	rc = asf_networking_start(tcp,networking);
	if( rc==MPXP_False ) {
	    //sometimes a file is just on a webserver and it is not streamed.
	    //try loading them default method as last resort for http protocol
	    if ( !strcasecmp(networking->url->protocol, "http") ) {
		MSG_STATUS("Trying default networking for http protocol\n ");
		//reset stream
		tcp.close();
		rc=nop_networking_start(tcp,networking);
	    }
	    if (rc==MPXP_False) {
		MSG_ERR("asf_networking_start failed\n");
		MSG_STATUS("Check if this is a playlist which requires -playlist option\nExample: mplayer -playlist <url>\n");
	    }
	}
    }
    if( rc==MPXP_False ) ;
    else if( networking->buffering ) {
	if(mp_conf.s_cache_size<0) {
	    // cache option not set, will use our computed value.
	    // buffer in KBytes, *5 because the prefill is 20% of the buffer.
	    mp_conf.s_cache_size = (networking->prebuffer_size/1024)*5;
	    if( mp_conf.s_cache_size<64 ) mp_conf.s_cache_size = 64;	// 16KBytes min buffer
	}
	MSG_INFO("Cache size set to %d KBytes\n", mp_conf.s_cache_size);
    }
    return rc;
}

int
networking_stop( networking_t *networking) {
    networking->status = networking_stopped_e;
    return 0;
}
