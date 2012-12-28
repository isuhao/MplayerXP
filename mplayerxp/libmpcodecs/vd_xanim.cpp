#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;
/*
  xacodec.c -- XAnim Video Codec DLL support

  (C) 2001 Alex Beregszaszi <alex@naxine.org>
       and Arpad Gereoffy <arpi@thot.banki.hu>
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* strerror */
#ifdef __FreeBSD__
#include <unistd.h>
#endif
#include <dlfcn.h> /* ld_sym, dlopen, dlclose */
#include <stdarg.h> /* va_alist, va_start, va_end */
#include <errno.h> /* strerror, errno */

#include "osdep/bswap.h"

#include "libmpconf/codec-cfg.h"

#include "libvo2/img_format.h"
#include "osdep/timer.h"
#include "osdep/fastmemcpy.h"
#include "vd_internal.h"
#include "codecs_ld.h"

static const vd_info_t info = {
    "XAnim codecs",
    "xanim",
    "A'rpi & Alex <Xanim (http://xanim.va.pubnix.com/)>",
    "build-in"
};

static const mpxp_option_t options[] = {
  { NULL, NULL, 0, 0, 0, 0, NULL}
};

LIBVD_EXTERN(xanim)

static const video_probe_t probes[] = {
    { "xanim", "vid_3ivX.xa", FOURCC_TAG('3','I','V','1'), VCodecStatus_Working, {IMGFMT_YV12, IMGFMT_I420}, {VideoFlag_None, VideoFlag_None } },
    { "xanim", "vid_cvid.xa", FOURCC_TAG('C','V','I','D'), VCodecStatus_Problems,{IMGFMT_YV12, IMGFMT_I420}, {VideoFlag_None, VideoFlag_None } },
    { "xanim", "vid_h261.xa", FOURCC_TAG('H','2','6','1'), VCodecStatus_Working, {IMGFMT_YV12, IMGFMT_I420}, {VideoFlag_None, VideoFlag_None } },
    { "xanim", "vid_h263.xa", FOURCC_TAG('H','2','6','3'), VCodecStatus_Working, {IMGFMT_YV12, IMGFMT_I420}, {VideoFlag_None, VideoFlag_None } },
    { "xanim", "vid_h263.xa", FOURCC_TAG('V','I','V','1'), VCodecStatus_Working, {IMGFMT_YV12, IMGFMT_I420}, {VideoFlag_None, VideoFlag_None } },
    { "xanim", "vid_h263.xa", FOURCC_TAG('V','I','V','O'), VCodecStatus_Working, {IMGFMT_YV12, IMGFMT_I420}, {VideoFlag_None, VideoFlag_None } },
    { "xanim", "vid_iv32.xa", FOURCC_TAG('I','V','3','1'), VCodecStatus_Working, {IMGFMT_YV12, IMGFMT_I420}, {VideoFlag_None, VideoFlag_None } },
    { "xanim", "vid_iv32.xa", FOURCC_TAG('I','V','3','2'), VCodecStatus_Working, {IMGFMT_YV12, IMGFMT_I420}, {VideoFlag_None, VideoFlag_None } },
    { "xanim", "vid_iv41.xa", FOURCC_TAG('I','V','4','1'), VCodecStatus_Working, {IMGFMT_YV12, IMGFMT_I420}, {VideoFlag_None, VideoFlag_None } },
    { "xanim", "vid_iv50.xa", FOURCC_TAG('I','V','5','0'), VCodecStatus_Working, {IMGFMT_YV12, IMGFMT_I420}, {VideoFlag_None, VideoFlag_None } },
    { NULL, NULL, 0x0, VCodecStatus_NotWorking, {0x0}, { VideoFlag_None } }
};

static const video_probe_t* __FASTCALL__ probe(uint32_t fourcc) {
    unsigned i;
    for(i=0;probes[i].driver;i++)
	if(fourcc==probes[i].fourcc)
	    return &probes[i];
    return NULL;
}

/*************************** START OF XA CODEC BINARY INTERFACE ****************/
typedef struct {
    unsigned int	what;
    unsigned int	id;
    int			(*iq_func)();	/* init/query function */
    unsigned int	(*dec_func)();  /* opt decode function */
} XAVID_FUNC_HDR;

enum {
    XAVID_WHAT_NO_MORE	=0x0000,
    XAVID_AVI_QUERY	=0x0001,
    XAVID_QT_QUERY	=0x0002,
    XAVID_DEC_FUNC	=0x0100,

    XAVID_API_REV	=0x0003
};

typedef struct {
    unsigned int	api_rev;
    char		*desc;
    char		*rev;
    char		*copyright;
    char		*mod_author;
    char		*authors;
    unsigned int	num_funcs;
    XAVID_FUNC_HDR	*funcs;
} XAVID_MOD_HDR;

/* XA CODEC .. */
typedef struct {
    any_t*		anim_hdr;
    unsigned long	compression;
    unsigned long	x, y;
    unsigned long	depth;
    any_t*		extra;
    unsigned long	xapi_rev;
    unsigned long	(*decoder)();
    char*		description;
    unsigned long	avi_ctab_flag;
    unsigned long	(*avi_read_ext)();
} XA_CODEC_HDR;

enum {
    CODEC_SUPPORTED=1,
    CODEC_UNKNOWN=0,
    CODEC_UNSUPPORTED=-1
};

/* fuckin colormap structures for xanim */
typedef struct {
    unsigned short	red;
    unsigned short	green;
    unsigned short	blue;
    unsigned short	gray;
} ColorReg;

typedef struct XA_ACTION_STRUCT {
    int				type;
    int				cmap_rev;
    unsigned char*		data;
    struct XA_ACTION_STRUCT*	next;
    struct XA_CHDR_STRUCT*	chdr;
    ColorReg*			h_cmap;
    unsigned int*		map;
    struct XA_ACTION_STRUCT*	next_same_chdr;
} XA_ACTION;

typedef struct XA_CHDR_STRUCT {
    unsigned int		rev;
    ColorReg*			cmap;
    unsigned int		csize, coff;
    unsigned int*		map;
    unsigned int		msize, moff;
    struct XA_CHDR_STRUCT*	next;
    XA_ACTION*			acts;
    struct XA_CHDR_STRUCT*	new_chdr;
} XA_CHDR;

typedef struct {
    unsigned int	cmd;
    unsigned int	skip_flag;
    unsigned int	imagex, imagey;	/* image buffer size */
    unsigned int	imaged;		/* image depth */
    XA_CHDR*		chdr;		/* color map header */
    unsigned int	map_flag;
    unsigned int*	map;
    unsigned int	xs, ys;
    unsigned int	xe, ye;
    unsigned int	special;
    any_t*		extra;
} XA_DEC_INFO;

typedef struct {
    unsigned int	file_num;
    unsigned int	anim_type;
    unsigned int	imagex;
    unsigned int	imagey;
    unsigned int	imagec;
    unsigned int	imaged;
} XA_ANIM_HDR;

// Added by A'rpi
typedef struct {
    unsigned int	out_fmt;
    unsigned		bpp;
    unsigned		width,height;
    unsigned char*	planes[3];
    unsigned		stride[3];
    unsigned char*	mem;
} xacodec_image_t;

//int xacodec_init_video(sh_video_t *vidinfo, int out_format);
//xacodec_image_t* xacodec_decode_frame(uint8_t *frame, int frame_size, int skip_flag);
//int xacodec_exit();
#if 0
typedef char xaBYTE;
typedef short xaSHORT;
typedef int xaLONG;

typedef unsigned char xaUBYTE;
typedef unsigned short xaUSHORT;
typedef unsigned int xaULONG;
#endif

enum {
    xaFALSE=0,
    xaTRUE
};

static const int XA_CLOSE_FUNCS=5;

struct xacodec_driver_t {
    XA_DEC_INFO*	decinfo;
    any_t*		file_handler;
    long (*iq_func)(XA_CODEC_HDR *codec_hdr);
    unsigned int (*dec_func)(unsigned char *image, unsigned char *delta,
	unsigned int dsize, XA_DEC_INFO *dec_info);
    any_t*		close_func[XA_CLOSE_FUNCS];
    xacodec_image_t	image;
};

struct xa_private_t : public Opaque {
    xa_private_t();
    virtual ~xa_private_t();

    sh_video_t* sh;
    video_decoder_t* parent;
    xacodec_driver_t* xacodec_driver;
    int xa_close_func;
};
xa_private_t::xa_private_t() {}
xa_private_t::~xa_private_t() {}

/* Needed by XAnim DLLs */
void XA_Print(const char *fmt, ...)
{
    va_list vallist;
    char buf[1024];

    va_start(vallist, fmt);
    vsnprintf(buf, 1024, fmt, vallist);
    MSG_DBG2("[xacodec] %s\n", buf);
    va_end(vallist);

    return;
}

/* 0 is no debug (needed by 3ivX) */
long xa_debug = 0;

int xacodec_exit(xa_private_t&);
void TheEnd1(xa_private_t& priv,const char *err_mess)
{
    XA_Print("error: %s - exiting\n", err_mess);
    xacodec_exit(priv);

    return;
}

void XA_Add_Func_To_Free_Chain(xa_private_t& priv,XA_ANIM_HDR *anim_hdr, void (*function)())
{
//    XA_Print("XA_Add_Func_To_Free_Chain('anim_hdr: %08x', 'function: %08x')",
//	    anim_hdr, function);
    priv.xacodec_driver->close_func[priv.xa_close_func] = (any_t*)function;
    if (priv.xa_close_func+1 < XA_CLOSE_FUNCS)
	priv.xa_close_func++;

    return;
}
/* end of crap */

/* load, init and query */
int xacodec_init(const char *filename, xacodec_driver_t *codec_driver)
{
    any_t*(*what_the)();
    const char *error;
    XAVID_MOD_HDR *mod_hdr;
    XAVID_FUNC_HDR *func;
    unsigned int i;

    codec_driver->file_handler = ::dlopen(filename, RTLD_NOW|RTLD_GLOBAL);
    if (!codec_driver->file_handler) {
	error = ::dlerror();
	if (error)
	    MSG_FATAL( "xacodec: failed to dlopen %s while %s\n", filename, error);
	else
	    MSG_FATAL( "xacodec: failed to dlopen %s\n", filename);
	return 0;
    }

    what_the = (any_t* (*)())ld_sym(codec_driver->file_handler, "What_The");
    if ((error = ::dlerror()) != NULL) {
	MSG_FATAL( "xacodec: failed to init %s while %s\n", filename, error);
	dlclose(codec_driver->file_handler);
	return 0;
    }

    mod_hdr = reinterpret_cast<XAVID_MOD_HDR *>(what_the());
    if (!mod_hdr) {
	MSG_FATAL( "xacodec: initializer function failed in %s\n", filename);
	dlclose(codec_driver->file_handler);
	return 0;
    }

    MSG_INFO( "=== XAnim Codec ===\n");
    MSG_INFO( " Filename: %s (API revision: %x)\n", filename, mod_hdr->api_rev);
    MSG_INFO( " Codec: %s. Rev: %s\n", mod_hdr->desc, mod_hdr->rev);
    if (mod_hdr->copyright)
	MSG_INFO( " %s\n", mod_hdr->copyright);
    if (mod_hdr->mod_author)
	MSG_INFO( " Module Author(s): %s\n", mod_hdr->mod_author);
    if (mod_hdr->authors)
	MSG_INFO( " Codec Author(s): %s\n", mod_hdr->authors);

    if (mod_hdr->api_rev > XAVID_API_REV) {
	MSG_FATAL( "xacodec: not supported api revision (%d) in %s\n",
	    mod_hdr->api_rev, filename);
	dlclose(codec_driver->file_handler);
	return 0;
    }

    func = mod_hdr->funcs;
    if (!func) {
	MSG_FATAL( "xacodec: function table error in %s\n", filename);
	dlclose(codec_driver->file_handler);
	return 0;
    }

    MSG_DBG2( "Exported functions by codec: [functable: 0x%08x entries: %d]\n",
	mod_hdr->funcs, mod_hdr->num_funcs);
    for (i = 0; i < mod_hdr->num_funcs; i++) {
	MSG_DBG2( " %d: %d %d [iq:0x%08x d:0x%08x]\n",
		i, func[i].what, func[i].id, func[i].iq_func, func[i].dec_func);
	if (func[i].what & XAVID_AVI_QUERY) {
	    MSG_DBG2( " 0x%08x: avi init/query func (id: %d)\n",
		func[i].iq_func, func[i].id);
	    codec_driver->iq_func = (long (*)(XA_CODEC_HDR*))func[i].iq_func;
	}
	if (func[i].what & XAVID_QT_QUERY) {
	    MSG_DBG2( " 0x%08x: qt init/query func (id: %d)\n",
		func[i].iq_func, func[i].id);
	    codec_driver->iq_func = (long (*)(XA_CODEC_HDR*))func[i].iq_func;
	}
	if (func[i].what & XAVID_DEC_FUNC) {
	    MSG_DBG2( " 0x%08x: decoder func (init/query: 0x%08x) (id: %d)\n",
		func[i].dec_func, func[i].iq_func, func[i].id);
	    codec_driver->dec_func = (unsigned (*)(unsigned char*,unsigned char *,unsigned int, XA_DEC_INFO *))func[i].dec_func;
	}
    }
    return 1;
}

int xacodec_query(xacodec_driver_t *codec_driver, XA_CODEC_HDR *codec_hdr)
{
    long codec_ret;

#if 0
    /* the brute one */
    if (codec_driver->dec_func) {
	codec_hdr->decoder = codec_driver->dec_func;
	MSG_DBG2( "We got decoder's address at init! %p\n", codec_hdr->decoder);
	return 1;
    }
#endif
    codec_ret = codec_driver->iq_func(codec_hdr);
    switch(codec_ret) {
	case CODEC_SUPPORTED:
	    codec_driver->dec_func = (unsigned (*)(unsigned char*,unsigned char *,unsigned int, XA_DEC_INFO *))codec_hdr->decoder;
	    MSG_DBG2( "Codec is supported: found decoder for %s at 0x%08x\n",
		codec_hdr->description, codec_hdr->decoder);
	    return 1;
	case CODEC_UNSUPPORTED:
	    MSG_FATAL( "Codec (%s) is unsupported by driver\n",
		codec_hdr->description);
	    return 0;
	case CODEC_UNKNOWN:
	default:
	    MSG_FATAL( "Codec (%s) is unknown by driver\n",
		codec_hdr->description);
	    return 0;
    }
}

const char *xacodec_def_path = "/usr/lib/xanim/mods";

static int xacodec_init_video(xa_private_t& priv, int out_format)
{
    char dll[1024];
    XA_CODEC_HDR codec_hdr;
    int i;
    sh_video_t* sh_video = priv.sh;

    priv.xacodec_driver = (xacodec_driver_t*)mp_realloc(priv.xacodec_driver, sizeof(struct xacodec_driver_t));
    if (priv.xacodec_driver == NULL) {
	MSG_FATAL( "xacodec: memory allocation error: %s\n",
	    strerror(errno));
	return 0;
    }

    priv.xacodec_driver->iq_func = NULL;
    priv.xacodec_driver->dec_func = NULL;

    for (i=0; i < XA_CLOSE_FUNCS; i++)
	priv.xacodec_driver->close_func[i] = NULL;

    if (getenv("XANIM_MOD_DIR"))
	xacodec_def_path = ::getenv("XANIM_MOD_DIR");

    snprintf(dll, 1024, "%s/%s", xacodec_def_path, sh_video->codec->dll_name);
    if (xacodec_init(dll, priv.xacodec_driver) == 0)
	return 0;

    codec_hdr.xapi_rev = XAVID_API_REV;
    codec_hdr.anim_hdr = mp_malloc(4096);
    codec_hdr.description = sh_video->codec->s_info;
    codec_hdr.compression = bswap_32(sh_video->bih->biCompression);
    codec_hdr.decoder = NULL;
    codec_hdr.x = sh_video->bih->biWidth; /* ->src_w */
    codec_hdr.y = sh_video->bih->biHeight; /* ->src_h */
    /* extra fields to store palette */
    codec_hdr.avi_ctab_flag = 0;
    codec_hdr.avi_read_ext = NULL;
    codec_hdr.extra = NULL;

    switch(out_format) {
/*	case IMGFMT_RGB8:
	    codec_hdr.depth = 8;
	    break;
	case IMGFMT_RGB15:
	    codec_hdr.depth = 15;
	    break;
	case IMGFMT_RGB16:
	    codec_hdr.depth = 16;
	    break;
	case IMGFMT_RGB24:
	    codec_hdr.depth = 24;
	    break;
	case IMGFMT_RGB32:
	    codec_hdr.depth = 32;
	    break;
	case IMGFMT_BGR8:
	    codec_hdr.depth = 8;
	    break;
	case IMGFMT_BGR15:
	    codec_hdr.depth = 15;
	    break;
	case IMGFMT_BGR16:
	    codec_hdr.depth = 16;
	    break;
	case IMGFMT_BGR24:
	    codec_hdr.depth = 24;
	    break;
	case IMGFMT_BGR32:
	    codec_hdr.depth = 32;
	    break;*/
	case IMGFMT_IYUV:
	case IMGFMT_I420:
	case IMGFMT_YV12:
	    codec_hdr.depth = 12;
	    break;
	default:
	    MSG_FATAL( "xacodec: not supported image out format (%s)\n",
		vo_format_name(out_format));
	    return 0;
    }
    MSG_INFO( "xacodec: querying for input %dx%d %dbit [fourcc: %4x] (%s)...\n",
	codec_hdr.x, codec_hdr.y, codec_hdr.depth, codec_hdr.compression, codec_hdr.description);

    if (xacodec_query(priv.xacodec_driver, &codec_hdr) == 0)
	return 0;

//    delete codec_hdr.anim_hdr;

    priv.xacodec_driver->decinfo = new(zeromem) XA_DEC_INFO;
    if (priv.xacodec_driver->decinfo == NULL) {
	MSG_FATAL( "xacodec: memory allocation error: %s\n",
	    strerror(errno));
	return 0;
    }
    priv.xacodec_driver->decinfo->cmd = 0;
    priv.xacodec_driver->decinfo->skip_flag = 0;
    priv.xacodec_driver->decinfo->imagex = priv.xacodec_driver->decinfo->xe = codec_hdr.x;
    priv.xacodec_driver->decinfo->imagey = priv.xacodec_driver->decinfo->ye = codec_hdr.y;
    priv.xacodec_driver->decinfo->imaged = codec_hdr.depth;
    priv.xacodec_driver->decinfo->chdr = NULL;
    priv.xacodec_driver->decinfo->map_flag = 0; /* xaFALSE */
    priv.xacodec_driver->decinfo->map = NULL;
    priv.xacodec_driver->decinfo->xs = priv.xacodec_driver->decinfo->ys = 0;
    priv.xacodec_driver->decinfo->special = 0;
    priv.xacodec_driver->decinfo->extra = codec_hdr.extra;
    MSG_DBG2( "decinfo->extra, filled by codec: 0x%08x [%s]\n",
	&priv.xacodec_driver->decinfo->extra, priv.xacodec_driver->decinfo->extra);

    priv.xacodec_driver->image.out_fmt = out_format;
    priv.xacodec_driver->image.bpp = codec_hdr.depth;
    priv.xacodec_driver->image.width = codec_hdr.x;
    priv.xacodec_driver->image.height = codec_hdr.y;
    priv.xacodec_driver->image.mem = new unsigned char [codec_hdr.y * codec_hdr.x * ((codec_hdr.depth+7)/8)];

    if (priv.xacodec_driver->image.mem == NULL) {
	MSG_FATAL( "xacodec: memory allocation error: %s\n",
	    strerror(errno));
	return 0;
    }

    return 1;
}

enum {
    ACT_DLTA_NORM	=0x00000000,
    ACT_DLTA_BODY	=0x00000001,
    ACT_DLTA_XOR	=0x00000002,
    ACT_DLTA_NOP	=0x00000004,
    ACT_DLTA_MAPD	=0x00000008,
    ACT_DLTA_DROP	=0x00000010,
    ACT_DLTA_BAD	=0x80000000
};

//    unsigned int (*dec_func)(unsigned char *image, unsigned char *delta,
//	unsigned int dsize, XA_DEC_INFO *dec_info);

xacodec_image_t* xacodec_decode_frame(xa_private_t& priv,uint8_t *frame, int frame_size, int skip_flag)
{
    unsigned int ret;
    xacodec_image_t *image=&priv.xacodec_driver->image;

// ugyis kiirja a vegen h dropped vagy nem..
//    if (skip_flag > 0)
//	MSG_DBG2( "frame will be dropped..\n");

    priv.xacodec_driver->decinfo->skip_flag = skip_flag;

    image->planes[0]=image->mem;
    image->stride[0]=image->width;
    image->stride[1]=image->stride[2]=image->width/2;
    switch(image->out_fmt){
    case IMGFMT_YV12:
	image->planes[2]=image->planes[0]+image->width*image->height;
	image->planes[1]=image->planes[2]+image->width*image->height/4;
	break;
    case IMGFMT_I420:
    case IMGFMT_IYUV:
	image->planes[1]=image->planes[0]+image->width*image->height;
	image->planes[2]=image->planes[1]+image->width*image->height/4;
	break;
    }

    ret = priv.xacodec_driver->dec_func((uint8_t*)&priv.xacodec_driver->image, frame, frame_size, priv.xacodec_driver->decinfo);


    if (ret == ACT_DLTA_NORM) {
//	MSG_DBG2( "norm\n");
	return &priv.xacodec_driver->image;
    }

    if (ret & ACT_DLTA_MAPD)
	MSG_DBG2( "mapd\n");
/*
    if (!(ret & ACT_DLT_MAPD))
	priv.xacodec_driver->decinfo->map_flag = 0;
    else
    {
	priv.xacodec_driver->decinfo->map_flag = 1;
	priv.xacodec_driver->decinfo->map = ...
    }
*/

    if (ret & ACT_DLTA_XOR) {
	MSG_DBG2( "xor\n");
	return &priv.xacodec_driver->image;
    }

    /* nothing changed */
    if (ret & ACT_DLTA_NOP) {
	MSG_DBG2( "nop\n");
	return NULL;
    }

    /* frame dropped (also display latest frame) */
    if (ret & ACT_DLTA_DROP) {
	MSG_DBG2( "drop\n");
	return NULL;
    }

    if (ret & ACT_DLTA_BAD) {
	MSG_DBG2( "bad\n");
	return NULL;
    }

    /* used for double buffer */
    if (ret & ACT_DLTA_BODY) {
	MSG_DBG2( "body\n");
	return NULL;
    }

    return NULL;
}

int xacodec_exit(xa_private_t& priv)
{
    int i;
    void (*close_func)();
    if(!priv.xacodec_driver) return TRUE;
    for (i=0; i < XA_CLOSE_FUNCS; i++)
	if (priv.xacodec_driver->close_func[i]) {
	    close_func = reinterpret_cast<void(*)()>(priv.xacodec_driver->close_func[i]);
	    close_func();
	}
    dlclose(priv.xacodec_driver->file_handler);
    if (priv.xacodec_driver->decinfo != NULL)
	delete priv.xacodec_driver->decinfo;
    delete priv.xacodec_driver;
    return TRUE;
}


/* *** XANIM Conversions *** */
/* like loader/win32.c - mini XANIM library */

unsigned long XA_Time_Read() {
    return GetTimer(); //(GetRelativeTime());
}

void XA_dummy() {
    XA_Print("dummy() called");
}

void XA_Gen_YUV_Tabs(XA_ANIM_HDR *anim_hdr)
{
    XA_Print("XA_Gen_YUV_Tabs('anim_hdr: %08x')", anim_hdr);

//    XA_Print("anim type: %d - img[x: %d, y: %d, c: %d, d: %d]",
//	anim_hdr->anim_type, anim_hdr->imagex, anim_hdr->imagey,
//	anim_hdr->imagec, anim_hdr->imaged);
    return;
}

void JPG_Setup_Samp_Limit_Table(XA_ANIM_HDR *anim_hdr)
{
    XA_Print("JPG_Setup_Samp_Limit_Table('anim_hdr: %08x')", anim_hdr);
//    xa_byte_limit = jpg_samp_limit + (MAXJSAMPLE + 1);
    return;
}

void JPG_Alloc_MCU_Bufs(XA_ANIM_HDR *anim_hdr, unsigned int width,
	unsigned int height, unsigned int full_flag)
{
    XA_Print("JPG_Alloc_MCU_Bufs('anim_hdr: %08x', 'width: %d', 'height: %d', 'full_flag: %d')",
	    anim_hdr, width, height, full_flag);
    return;
}

/* ---------------  4x4 pixel YUV block fillers [CVID] ----------------- */

typedef struct
{
    unsigned char r0, g0, b0;
    unsigned char r1, g1, b1;
    unsigned char r2, g2, b2;
    unsigned char r3, g3, b3;
    unsigned int clr0_0, clr0_1, clr0_2, clr0_3;
    unsigned int clr1_0, clr1_1, clr1_2, clr1_3;
    unsigned int clr2_0, clr2_1, clr2_2, clr2_3;
    unsigned int clr3_0, clr3_1, clr3_2, clr3_3;
} XA_2x2_Color;

#define SET_4_YUV_PIXELS(image,x,y,cmap2x2) \
    image->planes[0][((x)+0)+((y)+0)*image->stride[0]]=cmap2x2->clr0_0;\
    image->planes[0][((x)+1)+((y)+0)*image->stride[0]]=cmap2x2->clr0_1;\
    image->planes[0][((x)+0)+((y)+1)*image->stride[0]]=cmap2x2->clr0_2;\
    image->planes[0][((x)+1)+((y)+1)*image->stride[0]]=cmap2x2->clr0_3;\
    image->planes[1][((x)>>1)+((y)>>1)*image->stride[1]]=cmap2x2->clr1_0;\
    image->planes[2][((x)>>1)+((y)>>1)*image->stride[2]]=cmap2x2->clr1_1;

void XA_2x2_OUT_1BLK_Convert(unsigned char *image_p, unsigned int x, unsigned int y,
    unsigned int imagex, XA_2x2_Color *cmap2x2)
{
    xacodec_image_t *image=(xacodec_image_t*)image_p;

#if 0
    SET_4_YUV_PIXELS(image,x,y,cmap2x2)
#else
    SET_4_YUV_PIXELS(image,x,y,cmap2x2)
    SET_4_YUV_PIXELS(image,x+2,y,cmap2x2)
    SET_4_YUV_PIXELS(image,x,y+2,cmap2x2)
    SET_4_YUV_PIXELS(image,x+2,y+2,cmap2x2)
#endif
    return;
}

void XA_2x2_OUT_4BLKS_Convert(unsigned char *image_p, unsigned int x, unsigned int y,
    unsigned int imagex, XA_2x2_Color *cm0, XA_2x2_Color *cm1, XA_2x2_Color *cm2,
    XA_2x2_Color *cm3)
{
    xacodec_image_t *image=(xacodec_image_t*)image_p;

    SET_4_YUV_PIXELS(image,x,y,cm0)
    SET_4_YUV_PIXELS(image,x+2,y,cm1)
    SET_4_YUV_PIXELS(image,x,y+2,cm2)
    SET_4_YUV_PIXELS(image,x+2,y+2,cm3)
    return;
}

any_t*YUV2x2_Blk_Func(unsigned int image_type, int blks, unsigned int dith_flag)
{
    MSG_DBG3( "YUV2x2_Blk_Func(image_type=%d, blks=%d, dith_flag=%d)\n",
	image_type, blks, dith_flag);
    switch(blks){
    case 1:
	return (any_t*) XA_2x2_OUT_1BLK_Convert;
    case 4:
	return (any_t*) XA_2x2_OUT_4BLKS_Convert;
    }

    MSG_WARN("Unimplemented: YUV2x2_Blk_Func(image_type=%d  blks=%d  dith=%d)\n",image_type,blks,dith_flag);
    return (any_t*) XA_dummy;
}

//  Take Four Y's and UV and put them into a 2x2 Color structure.

void XA_YUV_2x2_clr(XA_2x2_Color *cmap2x2, unsigned int Y0, unsigned int Y1,
    unsigned int Y2, unsigned int Y3, unsigned int U, unsigned int V,
    unsigned int map_flag, unsigned int *map, XA_CHDR *chdr)
{

  MSG_DBG3("XA_YUV_2x2_clr(%p [%d,%d,%d,%d][%d][%d] %d %p %p)\n",
	  cmap2x2,Y0,Y1,Y2,Y3,U,V,map_flag,map,chdr);

  cmap2x2->clr0_0=Y0;
  cmap2x2->clr0_1=Y1;
  cmap2x2->clr0_2=Y2;
  cmap2x2->clr0_3=Y3;
  cmap2x2->clr1_0=U;
  cmap2x2->clr1_1=V;
  return;
}

any_t*YUV2x2_Map_Func(unsigned int image_type, unsigned int dith_type)
{
    MSG_DBG3( "YUV2x2_Map_Func('image_type: %d', 'dith_type: %d')",
	    image_type, dith_type);
    return (any_t*)XA_YUV_2x2_clr;
}

/* -------------------- whole YUV frame converters ------------------------- */

typedef struct
{
    unsigned char *Ybuf;
    unsigned char *Ubuf;
    unsigned char *Vbuf;
    unsigned char *the_buf;
    unsigned int the_buf_size;
    unsigned short y_w, y_h;
    unsigned short uv_w, uv_h;
} YUVBufs;

typedef struct
{
    unsigned long Uskip_mask;
    long	*YUV_Y_tab;
    long	*YUV_UB_tab;
    long	*YUV_VR_tab;
    long	*YUV_UG_tab;
    long	*YUV_VG_tab;
} YUVTabs;

YUVBufs jpg_YUVBufs;
YUVTabs def_yuv_tabs;

/* -------------- YUV 4x4 1x1 1x1  [Indeo 3,4,5] ------------------ */

void XA_YUV1611_Convert(unsigned char *image_p, unsigned int imagex, unsigned int imagey,
    unsigned int i_x, unsigned int i_y, YUVBufs *yuv, YUVTabs *yuv_tabs,
    unsigned int map_flag, unsigned int *map, XA_CHDR *chdr)
{
    xacodec_image_t *image=(xacodec_image_t*)image_p;
    unsigned int y;
    int uvstride;

    MSG_DBG3( "YUVTabs:  %d %p %p %p %p %p\n",yuv_tabs->Uskip_mask,
	yuv_tabs->YUV_Y_tab,
	yuv_tabs->YUV_UB_tab,
	yuv_tabs->YUV_VR_tab,
	yuv_tabs->YUV_UG_tab,
	yuv_tabs->YUV_VG_tab );

    MSG_DBG3( "XA_YUV1611_Convert('image: %08x', 'imagex: %d', 'imagey: %d', 'i_x: %d', 'i_y: %d', 'yuv_bufs: %08x', 'yuv_tabs: %08x', 'map_flag: %d', 'map: %08x', 'chdr: %08x')",
	image, imagex, imagey, i_x, i_y, yuv, yuv_tabs, map_flag, map, chdr);

    MSG_DBG3( "YUV: %p %p %p %X (%d) %dx%d %dx%d\n",
	yuv->Ybuf,yuv->Ubuf,yuv->Vbuf,yuv->the_buf,yuv->the_buf_size,
	yuv->y_w,yuv->y_h,yuv->uv_w,yuv->uv_h);

    // copy Y plane:
    if(yuv_tabs->YUV_Y_tab){     // dirty hack to detect iv32:
	for(y=0;y<imagey*imagex;y++)
	    image->planes[0][y]=yuv->Ybuf[y]<<1;
    } else
	memcpy(image->planes[0],yuv->Ybuf,imagex*imagey);

    // scale U,V planes by 2:
    imagex>>=2;
    imagey>>=2;

    uvstride=(yuv->uv_w)?yuv->uv_w:imagex;

    for(y=0;y<imagey;y++){
	unsigned char *su=yuv->Ubuf+uvstride*y;
	unsigned char *sv=yuv->Vbuf+uvstride*y;
	unsigned int strideu=image->stride[1];
	unsigned int stridev=image->stride[2];
	unsigned char *du=image->planes[1]+2*y*strideu;
	unsigned char *dv=image->planes[2]+2*y*stridev;
	unsigned int x;
	if(yuv_tabs->YUV_Y_tab){     // dirty hack to detect iv32:
	    for(x=0;x<imagex;x++){
		du[2*x]=du[2*x+1]=du[2*x+strideu]=du[2*x+strideu+1]=su[x]*2;
		dv[2*x]=dv[2*x+1]=dv[2*x+stridev]=dv[2*x+stridev+1]=sv[x]*2;
	    }
	} else {
	    for(x=0;x<imagex;x++){
		du[2*x]=du[2*x+1]=du[2*x+strideu]=du[2*x+strideu+1]=su[x];
		dv[2*x]=dv[2*x+1]=dv[2*x+stridev]=dv[2*x+stridev+1]=sv[x];
	    }
	}
    }
    return;
}

any_t*XA_YUV1611_Func(unsigned int image_type)
{
    MSG_DBG3( "XA_YUV1611_Func('image_type: %d')", image_type);
    return (any_t*)XA_YUV1611_Convert;
}

/* -------------- YUV 4x1 1x1 1x1 (4:1:1 ?) [CYUV] ------------------ */

void XA_YUV411111_Convert(unsigned char *image, unsigned int imagex, unsigned int imagey,
    unsigned int i_x, unsigned int i_y, YUVBufs *yuv_bufs, YUVTabs *yuv_tabs,
    unsigned int map_flag, unsigned int *map, XA_CHDR *chdr)
{
    MSG_DBG3( "XA_YUV411111_Convert('image: %d', 'imagex: %d', 'imagey: %d', 'i_x: %d', 'i_y: %d', 'yuv_bufs: %08x', 'yuv_tabs: %08x', 'map_flag: %d', 'map: %08x', 'chdr: %08x')",
	    image, imagex, imagey, i_x, i_y, yuv_bufs, yuv_tabs, map_flag, map, chdr);
    return;
}

any_t*XA_YUV411111_Func(unsigned int image_type)
{
    MSG_DBG3( "XA_YUV411111_Func('image_type: %d')", image_type);
    return (any_t*)XA_YUV411111_Convert;
}

/* --------------- YUV 2x2 1x1 1x1 (4:2:0 aka YV12) [3ivX,H263] ------------ */

void XA_YUV221111_Convert(unsigned char *image_p, unsigned int imagex, unsigned int imagey,
    unsigned int i_x, unsigned int i_y, YUVBufs *yuv, YUVTabs *yuv_tabs, unsigned int map_flag,
    unsigned int *map, XA_CHDR *chdr)
{
    xacodec_image_t *image=(xacodec_image_t*)image_p;

    MSG_DBG3( "XA_YUV221111_Convert(%p  %dx%d %d;%d [%dx%d]  %p %p %d %p %p)\n",
	image,imagex,imagey,i_x,i_y, image->width,image->height,
	yuv,yuv_tabs,map_flag,map,chdr);

    MSG_DBG3( "YUV: %p %p %p %X (%X) %Xx%X %Xx%X\n",
	yuv->Ybuf,yuv->Ubuf,yuv->Vbuf,yuv->the_buf,yuv->the_buf_size,
	yuv->y_w,yuv->y_h,yuv->uv_w,yuv->uv_h);

#warning "FIXME! Decoder doesn't supports Vivo/2.00 :("

if(i_x==(unsigned)image->width && i_y==(unsigned)image->height){
    image->planes[0]=yuv->Ybuf;
    if(image->out_fmt==IMGFMT_YV12){
	image->planes[1]=yuv->Ubuf;
	image->planes[2]=yuv->Vbuf;
    } else {
	image->planes[1]=yuv->Vbuf;
	image->planes[2]=yuv->Ubuf;
    }
    image->stride[0]=i_x; // yuv->y_w
    image->stride[1]=image->stride[2]=i_x/2; // yuv->uv_w
} else {
    unsigned int y;
    for(y=0;y<i_y;y++)
	memcpy(image->planes[0]+y*image->stride[0],yuv->Ybuf+y*i_x,i_x);
    i_x>>=1; i_y>>=1;
    for(y=0;y<i_y;y++){
	memcpy(image->planes[1]+y*image->stride[1],yuv->Ubuf+y*i_x,i_x);
	memcpy(image->planes[2]+y*image->stride[2],yuv->Vbuf+y*i_x,i_x);
    }
}
    return;
}

any_t*XA_YUV221111_Func(unsigned int image_type)
{
    MSG_DBG3( "XA_YUV221111_Func('image_type: %d')\n",image_type);
    return (any_t*)XA_YUV221111_Convert;
}

/* *** EOF XANIM *** */

/*************************** END OF XA CODEC BINARY INTERFACE ******************/

// to set/get/query special features/parameters
static MPXP_Rc control_vd(Opaque& ctx,int cmd,any_t* arg,...){
    UNUSED(ctx);
    switch(cmd) {
      case VDCTRL_QUERY_FORMAT:
	    if (*((int*)arg) == IMGFMT_YV12 ||
		*((int*)arg) == IMGFMT_I420 ||
		*((int*)arg) == IMGFMT_IYUV)
			return MPXP_True;
	    else 	return MPXP_False;
      default: break;
    }
    return MPXP_Unknown;
}

static Opaque* preinit(const video_probe_t& probe,sh_video_t *sh,put_slice_info_t& psi){
    UNUSED(probe);
    UNUSED(psi);
    xa_private_t* priv = new(zeromem) xa_private_t;
    priv->sh=sh;
    return priv;
}

// init driver
static MPXP_Rc init(Opaque& ctx,video_decoder_t& opaque){
    xa_private_t& priv=static_cast<xa_private_t&>(ctx);
    sh_video_t* sh=priv.sh;
    priv.parent=&opaque;
    if(xacodec_init_video(priv,sh->codec->outfmt[sh->outfmtidx]))
	return mpcodecs_config_vf(opaque,sh->src_w,sh->src_h);
    return MPXP_False;
}

// uninit driver
static void uninit(Opaque& ctx){
    xa_private_t& priv=static_cast<xa_private_t&>(ctx);
    xacodec_exit(priv);
}

// decode a frame
static mp_image_t* decode(Opaque& ctx,const enc_frame_t& frame){
    xa_private_t& priv=static_cast<xa_private_t&>(ctx);
    sh_video_t* sh = priv.sh;
    mp_image_t* mpi;
    xacodec_image_t* image;

    if(frame.len<=0) return NULL; // skipped frame

    image=xacodec_decode_frame(priv,reinterpret_cast<uint8_t*>(frame.data),frame.len,(frame.flags&3)?1:0);
    if(!image) return NULL;

    mpi=mpcodecs_get_image(*priv.parent, MP_IMGTYPE_EXPORT, MP_IMGFLAG_PRESERVE,
	sh->src_w, sh->src_h);
    if(!mpi) return NULL;

    mpi->planes[0]=image->planes[0];
    mpi->planes[1]=image->planes[1];
    mpi->planes[2]=image->planes[2];
    mpi->stride[0]=image->stride[0];
    mpi->stride[1]=image->stride[1];
    mpi->stride[2]=image->stride[2];

    return mpi;
}

