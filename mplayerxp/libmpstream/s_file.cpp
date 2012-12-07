#include "mp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;
/*
    s_file - stream interface for file i/o.
*/
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifndef __USE_GNU
#define __USE_GNU
#endif
#include <unistd.h>
#include <fcntl.h>

#include "mplayerxp.h"
#include "stream.h"
#include "stream_internal.h"
#include "stream_msg.h"

namespace mpxp {
    class File_Stream_Interface : public Stream_Interface {
	public:
	    File_Stream_Interface();
	    virtual ~File_Stream_Interface();

	    virtual MPXP_Rc	open(libinput_t* libinput,const char *filename,unsigned flags);
	    virtual int		read(stream_packet_t * sp);
	    virtual off_t	seek(off_t off);
	    virtual off_t	tell() const;
	    virtual void	close();
	    virtual MPXP_Rc	ctrl(unsigned cmd,any_t* param);
	    virtual Stream::type_e type() const;
	    virtual off_t	size() const;
	    virtual off_t	sector_size() const;
	private:
	    int fd;
	    int was_open;
	    off_t spos;
	    off_t end_pos;
    };

File_Stream_Interface::File_Stream_Interface():fd(0),was_open(0),spos(0) {}
File_Stream_Interface::~File_Stream_Interface() {}

MPXP_Rc File_Stream_Interface::open(libinput_t*libinput,const char *filename,unsigned flags)
{
    UNUSED(flags);
    UNUSED(libinput);
    if(strcmp(filename,"-")==0) fd=0;
    else fd=::open(filename,O_RDONLY);
    if(fd<0) {
	MSG_ERR("[s_file] Cannot open file: '%s'\n",filename);
	return MPXP_False;
    }
    was_open = (fd==0)?0:1;
    end_pos = ::lseek(fd,0,SEEK_END);
    ::lseek(fd,0,SEEK_SET);
    /* decreasing number of packet from 256 to 10 speedups cache2 from 3.27% to 1.26%
       with full speed 1.04% for -nocache */
    /* Note: Please locate sector_size changinf after all read/write operations of open() function */
    spos = 0;
    return MPXP_Ok;
}

Stream::type_e File_Stream_Interface::type() const { return (end_pos==-1)?Stream::Type_Stream:Stream::Type_Seekable; }
off_t	File_Stream_Interface::size() const { return end_pos; }
off_t	File_Stream_Interface::sector_size() const { return STREAM_BUFFER_SIZE; }

#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(x) (x)
#endif

int File_Stream_Interface::read(stream_packet_t*sp)
{
/*
    Should we repeate read() again on these errno: `EAGAIN', `EIO' ???
*/
    sp->type=0;
    sp->len = TEMP_FAILURE_RETRY(::read(fd,sp->buf,sp->len));
    if(sp->len>0) spos += sp->len;
    return sp->len;
}

# define TEMP_FAILURE_RETRY64(expression) \
  (__extension__					\
    ({ long long int __result;				\
       do __result = (long long int) (expression);	\
       while (__result == -1LL && errno == EINTR);	\
       __result; }))

off_t File_Stream_Interface::seek(off_t pos)
{
    spos=TEMP_FAILURE_RETRY64(::lseek(fd,pos,SEEK_SET));
    return spos;
}

off_t File_Stream_Interface::tell() const
{
    return spos;
}

void File_Stream_Interface::close()
{
    if(was_open) ::close(fd);
}

MPXP_Rc File_Stream_Interface::ctrl(unsigned cmd,any_t*args) {
    UNUSED(cmd);
    UNUSED(args);
    return MPXP_Unknown;
}

static Stream_Interface* query_file_interface() { return new(zeromem) File_Stream_Interface; }

extern const stream_interface_info_t file_stream =
{
    "file://",
    "reads multimedia stream from regular file",
    query_file_interface
};

    class Stdin_Stream_Interface : public File_Stream_Interface {
	public:
	    Stdin_Stream_Interface();
	    virtual ~Stdin_Stream_Interface();

	    virtual MPXP_Rc	open(libinput_t* libinput,const char *filename,unsigned flags);
    };

Stdin_Stream_Interface::Stdin_Stream_Interface() {}
Stdin_Stream_Interface::~Stdin_Stream_Interface() {}
MPXP_Rc Stdin_Stream_Interface::open(libinput_t*libinput,const char *filename,unsigned flags) {
    UNUSED(libinput);
    UNUSED(filename);
    return File_Stream_Interface::open(NULL,"-",flags);
}

static Stream_Interface* query_stdin_interface() { return new(zeromem) Stdin_Stream_Interface; }

extern const stream_interface_info_t stdin_stream =
{
    "stdin://",
    "reads multimedia stream from standard input",
    query_stdin_interface
};

}