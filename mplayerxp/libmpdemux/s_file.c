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

#include "../mplayer.h"
#include "stream.h"

typedef struct file_priv_s
{
    int was_open;
    off_t spos;
}file_priv_t;


static int __FASTCALL__ file_open(stream_t *stream,const char *filename,unsigned flags)
{
    UNUSED(flags);
    if(!(stream->priv = malloc(sizeof(file_priv_t)))) return 0;
    if(strcmp(filename,"-")==0) stream->fd=0;
    else stream->fd=open(filename,O_RDONLY);
    if(stream->fd<0) { free(stream->priv); return 0; }
    ((file_priv_t*)stream->priv)->was_open = stream->fd==0?0:1;
    stream->end_pos = lseek(stream->fd,0,SEEK_END);
    lseek(stream->fd,0,SEEK_SET);
    if(stream->end_pos == -1)	stream->type = STREAMTYPE_STREAM;
    else			stream->type = STREAMTYPE_SEEKABLE;
    /* decreasing number of packet from 256 to 10 speedups cache2 from 3.27% to 1.26%
       with full speed 1.04% for -nocache */
    /* Note: Please locate sector_size changinf after all read/write operations of open() function */
    stream->sector_size=stream_cache_size?stream_cache_size*1024/10:STREAM_BUFFER_SIZE;
    ((file_priv_t*)stream->priv)->spos = 0;
    return 1;
}

static int __FASTCALL__ stdin_open(stream_t *stream,const char *filename,unsigned flags) {
    UNUSED(filename);
    return file_open(stream,"-",flags);
}

#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(x) (x)
#endif

static int __FASTCALL__ file_read(stream_t*stream,stream_packet_t*sp)
{
/*
    Should we repeate read() again on these errno: `EAGAIN', `EIO' ???
*/
    file_priv_t*p=stream->priv;
    sp->type=0;
    sp->len = TEMP_FAILURE_RETRY(read(stream->fd,sp->buf,sp->len));
    if(sp->len>0) p->spos += sp->len;
    else	  stream->_Errno=errno;
    return sp->len;
}

# define TEMP_FAILURE_RETRY64(expression) \
  (__extension__					\
    ({ long long int __result;				\
       do __result = (long long int) (expression);	\
       while (__result == -1LL && errno == EINTR);	\
       __result; }))

static off_t __FASTCALL__ file_seek(stream_t*stream,off_t pos)
{
    file_priv_t*p=stream->priv;
    p->spos=TEMP_FAILURE_RETRY64(lseek(stream->fd,pos,SEEK_SET));
    if(p->spos<0) stream->_Errno=errno;
    return p->spos;
}

static off_t __FASTCALL__ file_tell(stream_t*stream)
{
    file_priv_t*p=stream->priv;
    return p->spos;
}

static void __FASTCALL__ file_close(stream_t *stream)
{
    int was_open = ((file_priv_t*)stream->priv)->was_open;
    if(was_open) close(stream->fd);
    free(stream->priv);
}

static int __FASTCALL__ file_ctrl(stream_t *s,unsigned cmd,void *args) {
    UNUSED(s);
    UNUSED(cmd);
    UNUSED(args);
    return SCTRL_UNKNOWN;
}

const stream_driver_t stdin_stream =
{
    "stdin://",
    "reads multimedia stream from standard input",
    stdin_open,
    file_read,
    file_seek,
    file_tell,
    file_close,
    file_ctrl
};

const stream_driver_t file_stream =
{
    "file://",
    "reads multimedia stream from regular file",
    file_open,
    file_read,
    file_seek,
    file_tell,
    file_close,
    file_ctrl
};
