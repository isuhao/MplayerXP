#include "mp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;
#define __STDC_FORMAT_MACROS
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
#include "url.h"
#include "stream_msg.h"

namespace mpxp {
    class Ftp_Stream_Interface : public Stream_Interface {
	public:
	    Ftp_Stream_Interface(libinput_t& libinput);
	    virtual ~Ftp_Stream_Interface();

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
	    int			readline(char *buf,int max);
	    int			readresp(char* rsp);
	    int			OpenPort();
	    int			OpenData(size_t newpos);
	    int			SendCmd(const std::string& cmd,char* rsp);

	    const char*	user;
	    const char*	pass;
	    const char*	host;
	    int		port;
	    const char*	filename;
	    URL*	url;

	    char*	cput,*cget;
	    Tcp		tcp;
	    int		cavail,cleft;
	    char*	buf;
	    off_t	spos;
	    off_t	file_len;
    };

Ftp_Stream_Interface::Ftp_Stream_Interface(libinput_t& libinput)
		    :Stream_Interface(libinput),
		    tcp(libinput,-1) {}
Ftp_Stream_Interface::~Ftp_Stream_Interface() {
    SendCmd("QUIT",NULL);
    if(buf) delete buf;
}

#define BUFSIZE 8192

#define TELNET_IAC      255             /* interpret as command: */
#define TELNET_IP       244             /* interrupt process--permanently */
#define TELNET_SYNCH    242             /* for telfunc calls */

/*
 * read a line of text
 *
 * return -1 on error or bytecount
 */
int Ftp_Stream_Interface::readline(char *_buf,int max)
{
    int x,retval = 0;
    char *end,*bp=_buf;
    int eof = 0;

    do {
	if (cavail > 0) {
	    x = (max >= cavail) ? cavail : max-1;
	    end = (char*)memccpy(bp,cget,'\n',x);
	    if (end != NULL) x = end - bp;
	    retval += x;
	    bp += x;
	    *bp = '\0';
	    max -= x;
	    cget += x;
	    cavail -= x;
	    if (end != NULL) {
		bp -= 2;
		if (strcmp(bp,"\r\n") == 0) {
		    *bp++ = '\n';
		    *bp++ = '\0';
		    --retval;
		}
		break;
	    }
	}
	if (max == 1) {
	    *_buf = '\0';
	    break;
	}
	if (cput == cget) {
	    cput = cget = _buf;
	    cavail = 0;
	    cleft = BUFSIZE;
	}
	if(eof) {
	    if (retval == 0) retval = -1;
	    break;
	}
	if(!tcp.has_data(15)) {
	    MSG_ERR("[ftp] read timed out\n");
	    retval = -1;
	    break;
	}
	if ((x = tcp.read((uint8_t*)cput,cleft)) == -1) {
	    MSG_ERR("[ftp] read error: %s\n",strerror(errno));
	    retval = -1;
	    break;
	}
	if (x == 0) eof = 1;
	cleft -= x;
	cavail += x;
	cput += x;
    } while (1);
    return retval;
}

/*
 * read a response from the server
 *
 * return 0 if first char doesn't match
 * return 1 if first char matches
 */
int Ftp_Stream_Interface::readresp(char* rsp)
{
    static char response[256];
    char match[5];
    int r;

    if (readline(response,256) == -1) return 0;

    r = atoi(response)/100;
    if(rsp) strcpy(rsp,response);

    MSG_V("[ftp] < %s",response);

    if (response[3] == '-') {
	strncpy(match,response,3);
	match[3] = ' ';
	match[4] = '\0';
	do {
	    if (readline(response,256) == -1) {
		MSG_ERR("[ftp] Control socket read failed\n");
		return 0;
	    }
	    MSG_V("[ftp] < %s",response);
	} while (strncmp(response,match,4));
    }
    return r;
}

int Ftp_Stream_Interface::SendCmd(const std::string& _cmd,char* rsp)
{
    const char* cmd=_cmd.c_str();
    int l = _cmd.length();
    int hascrlf = cmd[l - 2] == '\r' && cmd[l - 1] == '\n';

    if(hascrlf && l == 2) MSG_V("\n");
    else MSG_V("[ftp] > %s",cmd);
    while(l > 0) {
	int s = tcp.write((const uint8_t*)cmd,l);
	if(s <= 0) {
	    MSG_ERR("[ftp] write error: %s\n",strerror(errno));
	    return 0;
	}
	cmd += s;
	l -= s;
    }
    if (hascrlf) return readresp(rsp);
    return SendCmd("\r\n", rsp);
}

int Ftp_Stream_Interface::OpenPort() {
    int resp;
    net_fd_t fd;
    char rsp_txt[256];
    char* par,str[128];
    int num[6];

    resp = SendCmd("PASV",rsp_txt);
    if(resp != 2) {
	MSG_WARN("[ftp] command 'PASV' failed: %s\n",rsp_txt);
	return 0;
    }
    par = strchr(rsp_txt,'(');
    if(!par || !par[0] || !par[1]) {
	MSG_ERR("[ftp] invalid server response: %s ??\n",rsp_txt);
	return 0;
    }
    sscanf(par+1,"%u,%u,%u,%u,%u,%u",&num[0],&num[1],&num[2],&num[3],&num[4],&num[5]);
    snprintf(str,127,"%d.%d.%d.%d",num[0],num[1],num[2],num[3]);
    tcp.open(str,(num[4]<<8)+num[5]);

    if(fd < 0) MSG_ERR("[ftp] failed to create data connection\n");
    return 1;
}

int Ftp_Stream_Interface::OpenData(size_t newpos) {
    int resp;
    char str[256],rsp_txt[256];

    // Open a new connection
    OpenPort();

    if(tcp.established()) return 0;

    if(newpos > 0) {
	snprintf(str,255,"REST %"PRId64, (int64_t)newpos);
	resp = SendCmd(str,rsp_txt);
	if(resp != 3) {
	    MSG_WARN("[ftp] command '%s' failed: %s\n",str,rsp_txt);
	    newpos = 0;
	}
    }
    // Get the file
    snprintf(str,255,"RETR %s",filename);
    resp = SendCmd(str,rsp_txt);

    if(resp != 1) {
	MSG_ERR("[ftp] command '%s' failed: %s\n",str,rsp_txt);
	return 0;
    }
    spos = newpos;
    return 1;
}

int Ftp_Stream_Interface::read(stream_packet_t*sp){
    int r;

    if(!OpenData(spos)) return -1;

    if(!tcp.has_data(15)) {
	MSG_ERR("[ftp] read timed out\n");
	return -1;
    }
    MSG_V("ftp read: %u bytes\n",sp->len);
    r = tcp.read((uint8_t*)sp->buf,sp->len);
    spos+=r;
    return (r <= 0) ? -1 : r;
}

off_t Ftp_Stream_Interface::seek(off_t newpos) {
    int resp;
    char rsp_txt[256];

    if(spos==newpos) return spos;
    MSG_V("ftp seek: %llu bytes\n",newpos);
    if(spos > file_len) return 0;

    // Check to see if the server did not already terminate the transfer
    if(tcp.has_data(0)) {
	if(readresp(rsp_txt) != 2)
	    MSG_WARN("[ftp] Warning the server didn't finished the transfer correctly: %s\n",rsp_txt);
	tcp.close();
    }

    // Close current download
    if(tcp.established()) {
	static const char pre_cmd[]={TELNET_IAC,TELNET_IP,TELNET_IAC,TELNET_SYNCH};
	//int fl;
	// Send send the telnet sequence needed to make the server react

	// send only first byte as OOB due to OOB braindamage in many unices
	tcp.write((uint8_t*)pre_cmd,1,MSG_OOB);
	tcp.write((uint8_t*)(pre_cmd+1),sizeof(pre_cmd)-1);

	// Get the 426 Transfer aborted
	// Or the 226 Transfer complete
	resp = readresp(rsp_txt);
	if(resp != 4 && resp != 2) {
	    MSG_ERR("[ftp] Server didn't abort correctly: %s\n",rsp_txt);
	    return 0;
	}
	// Send the ABOR command
	// Ignore the return code as sometimes it fail with "nothing to abort"
	SendCmd("ABOR",rsp_txt);
	// close the fd
	tcp.close();
    }
    if(OpenData(newpos)) spos=newpos;
    return spos;
}

off_t Ftp_Stream_Interface::tell() const
{
    return spos;
}


void Ftp_Stream_Interface::close() {
    if(tcp.established()) tcp.close();
}

MPXP_Rc Ftp_Stream_Interface::open(const std::string& _filename,unsigned flags)
{
    int resp;
    char str[256],rsp_txt[256];
    std::string uname;

    UNUSED(flags);
    uname=std::string("ftp://")+_filename;
    if(!(url=url_new(uname))) goto bad_url;
//  url = check4proxies (rurl);
    if(!(url->hostname && url->file)) {
	bad_url:
	MSG_ERR("[ftp] Bad url\n");
	return MPXP_False;
    }
    user=url->username?url->username:"anonymous";
    pass=url->password?url->password:"no@spam";
    host=url->hostname;
    port=url->port?url->port:21;
    filename=url->file;
    MSG_V("FTP: Opening ~%s :%s @%s :%i %s\n",user,pass,host,port,filename);

    // Open the control connection
    tcp.open(host,port);

    if(!tcp.established()) {
	delete url;
	return MPXP_False;
    }
    // We got a connection, let's start serious things
    buf = new char [BUFSIZE];
    if (readresp(NULL) == 0) {
	close();
	delete url;
	return MPXP_False;
    }
    // Login
    snprintf(str,255,"USER %s",user);
    resp = SendCmd(str,rsp_txt);
    // password needed
    if(resp == 3) {
	snprintf(str,255,"PASS %s",pass);
	resp = SendCmd(str,rsp_txt);
	if(resp != 2) {
	    MSG_ERR("[ftp] command '%s' failed: %s\n",str,rsp_txt);
	    close();
	    delete url;
	    return MPXP_False;
	}
    } else if(resp != 2) {
	MSG_ERR("[ftp] command '%s' failed: %s\n",str,rsp_txt);
	close();
	delete url;
	return MPXP_False;
    }

    // Set the transfer type
    resp = SendCmd("TYPE I",rsp_txt);
    if(resp != 2) {
	MSG_ERR("[ftp] command 'TYPE I' failed: %s\n",rsp_txt);
	close();
	delete url;
	return MPXP_False;
    }

    // Get System of FTP
    resp = SendCmd("SYST",rsp_txt);
    if(resp != 2) {
	MSG_ERR("[ftp] command 'SYST' failed: %s\n",rsp_txt);
	close();
	delete url;
	return MPXP_False;
    }
    MSG_INFO("[ftp] System: %s\n",rsp_txt);
    resp = SendCmd("STAT",rsp_txt);
    if(resp != 2) {
	MSG_ERR("[ftp] command 'STAT' failed: %s\n",rsp_txt);
	close();
	delete url;
	return MPXP_False;
    }

    file_len=0;
    // Get the filesize
    snprintf(str,255,"SIZE %s",filename);
    resp = SendCmd(str,rsp_txt);
    if(resp != 2) {
	MSG_WARN("[ftp] command '%s' failed: %s\n",str,rsp_txt);
    } else {
	int dummy;
	sscanf(rsp_txt,"%d %"PRId64,&dummy,&file_len);
    }

    spos=0;
    // The data connection is really opened only at the first
    // read/seek. This must be done when the cache is used
    // because the connection would stay open in the main process,
    // preventing correct abort with many servers.

    delete url;
    return MPXP_Ok;
}
Stream::type_e Ftp_Stream_Interface::type() const { return file_len?Stream::Type_Seekable:Stream::Type_Stream; }
off_t	Ftp_Stream_Interface::size() const { return file_len; }
off_t	Ftp_Stream_Interface::sector_size() const { return BUFSIZE; }
std::string Ftp_Stream_Interface::mime_type() const { return "application/octet-stream"; }

MPXP_Rc Ftp_Stream_Interface::ctrl(unsigned cmd,any_t*args) {
    UNUSED(cmd);
    UNUSED(args);
    return MPXP_Unknown;
}

static Stream_Interface* query_interface(libinput_t& libinput) { return new(zeromem) Ftp_Stream_Interface(libinput); }

/* "reuse a bit of code from ftplib written by Thomas Pfau", */
extern const stream_interface_info_t ftp_stream =
{
    "ftp://",
    "reads multimedia stream from File Transfer Protocol (FTP)",
    query_interface,
};
} // namespace mpxp
