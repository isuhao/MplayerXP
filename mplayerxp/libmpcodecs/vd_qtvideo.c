#include <stdio.h>
#include <stdlib.h>

#include "mp_config.h"
#include "mplayerxp.h"

#include <dlfcn.h> /* GLIBC specific. Exists under cygwin too! */

#include "vd_internal.h"
#include "codecs_ld.h"

#ifdef WIN32_LOADER
#include "loader/ldt_keeper.h"
#endif
#include "codecs_ld.h"
#include "vd_msg.h"

static const vd_info_t info = {
    "Quicktime Video decoder",
    "qtvideo",
    "A'rpi & Faust3",
    "build-in"
};

static const config_t options[] = {
  { NULL, NULL, 0, 0, 0, 0, NULL}
};

LIBVD_EXTERN(qtvideo)

#include "osdep/bswap.h"

#ifdef MACOSX
#include <QuickTime/ImageCodec.h>
#define dump_ImageDescription(x)
#else
#include "loader/qtx/qtxsdk/components.h"
#endif

//#include "wine/windef.h"
HMODULE   WINAPI LoadLibraryA(LPCSTR);
FARPROC   WINAPI GetProcAddress(HMODULE,LPCSTR);
int       WINAPI FreeLibrary(HMODULE);

//static ComponentDescription desc; // for FindNextComponent()
static ComponentInstance ci=NULL; // codec handle
//static CodecInfo cinfo;	// for ImageCodecGetCodecInfo()
//Component prev=NULL;
//ComponentResult cres; //
static CodecCapabilities codeccap; // for decpar
static CodecDecompressParams decpar; // for ImageCodecPreDecompress()
//static ImageSubCodecDecompressCapabilities icap; // for ImageCodecInitialize()
static Rect OutBufferRect;              //the dimensions of our GWorld

static GWorldPtr OutBufferGWorld = NULL;//a GWorld is some kind of description for a drawing environment
static ImageDescriptionHandle framedescHandle;
//static HINSTANCE qtml_dll;
static HMODULE handler;

static    Component (*FindNextComponent)(Component prev,ComponentDescription* desc);
static    OSErr (*GetComponentInfo)(Component prev,ComponentDescription* desc,Handle h1,Handle h2,Handle h3);
static    long (*CountComponents)(ComponentDescription* desc);
static    OSErr (*InitializeQTML)(long flags);
static    OSErr (*EnterMovies)(void);
static    ComponentInstance (*OpenComponent)(Component c);
static    ComponentResult (*ImageCodecInitialize)(ComponentInstance ci,
				 ImageSubCodecDecompressCapabilities * cap);
static    ComponentResult (*ImageCodecBeginBand)(ComponentInstance      ci,
				 CodecDecompressParams * params,
				 ImageSubCodecDecompressRecord * drp,
				 long                   flags);
static    ComponentResult (*ImageCodecDrawBand)(ComponentInstance      ci,
				 ImageSubCodecDecompressRecord * drp);
static    ComponentResult (*ImageCodecEndBand)(ComponentInstance      ci,
				 ImageSubCodecDecompressRecord * drp,
				 OSErr                  result,
				 long                   flags);
static    ComponentResult (*ImageCodecGetCodecInfo)(ComponentInstance      ci,
				 CodecInfo *            info);
static    ComponentResult (*ImageCodecPreDecompress)(ComponentInstance      ci,
				 CodecDecompressParams * params);
static    ComponentResult (*ImageCodecBandDecompress)(ComponentInstance      ci,
				 CodecDecompressParams * params);
static    PixMapHandle    (*GetGWorldPixMap)(GWorldPtr offscreenGWorld);
static    OSErr           (*QTNewGWorldFromPtr)(GWorldPtr *gw,
				 OSType pixelFormat,
				 const Rect *boundsRect,
				 CTabHandle cTable,
				 /*GDHandle*/any_t* aGDevice, //unused anyway
				 GWorldFlags flags,
				 any_t*baseAddr,
				 long rowBytes);
static    OSErr           (*NewHandleClear)(Size byteCount);

// to set/get/query special features/parameters
static MPXP_Rc control(sh_video_t *sh,int cmd,any_t* arg,...){
    switch(cmd) {
      case VDCTRL_QUERY_FORMAT:
	    if (*((int*)arg) == IMGFMT_YV12 ||
		*((int*)arg) == IMGFMT_I420 ||
		*((int*)arg) == IMGFMT_IYUV ||
		*((int*)arg) == IMGFMT_YVU9 ||
		*((int*)arg) == IMGFMT_YUY2 ||
		*((int*)arg) == IMGFMT_UYVY ||
		*((int*)arg) == IMGFMT_YVYU ||
		*((int*)arg) == IMGFMT_BGR16||
		*((int*)arg) == IMGFMT_BGR24||
		*((int*)arg) == IMGFMT_BGR32||
		*((int*)arg) == IMGFMT_RGB32)
			return MPXP_True;
	    else 	return MPXP_False;
      default: break;
    }
    return MPXP_Unknown;
}

static int codec_inited=0;
// init driver
static MPXP_Rc init(sh_video_t *sh,any_t* libinput){
    long result = 1;
    ComponentResult cres;
    ComponentDescription desc;
    Component prev=NULL;
    CodecInfo cinfo;	// for ImageCodecGetCodecInfo()
    ImageSubCodecDecompressCapabilities icap; // for ImageCodecInitialize()
    if(mp_conf.s_cache_size) {
	MSG_FATAL("Disabling video:\nwin32 quicktime DLLs must be initialized in single-threaded mode! Try -nocache\n");
	return MPXP_False;
    }
#ifdef MACOSX
    EnterMovies();
#else

#ifdef WIN32_LOADER
    Setup_LDT_Keeper();
#endif

    handler = LoadLibraryA("qtmlClient.dll");

    InitializeQTML = (OSErr (*)(long))GetProcAddress(handler, "InitializeQTML");
    EnterMovies = (OSErr (*)(void))GetProcAddress(handler, "EnterMovies");
    FindNextComponent = (Component (*)(Component,ComponentDescription*))GetProcAddress(handler, "FindNextComponent");
    CountComponents = (long (*)(ComponentDescription*))GetProcAddress(handler, "CountComponents");
    GetComponentInfo = (OSErr (*)(Component,ComponentDescription*,Handle,Handle,Handle))GetProcAddress(handler, "GetComponentInfo");
    OpenComponent = (ComponentInstance (*)(Component))GetProcAddress(handler, "OpenComponent");
    ImageCodecInitialize = (ComponentResult (*)(ComponentInstance,ImageSubCodecDecompressCapabilities *))GetProcAddress(handler, "ImageCodecInitialize");
    ImageCodecGetCodecInfo = (ComponentResult (*)(ComponentInstance,CodecInfo *))GetProcAddress(handler, "ImageCodecGetCodecInfo");
    ImageCodecBeginBand = (ComponentResult (*)(ComponentInstance,CodecDecompressParams *,ImageSubCodecDecompressRecord *,long))GetProcAddress(handler, "ImageCodecBeginBand");
    ImageCodecPreDecompress = (ComponentResult (*)(ComponentInstance,CodecDecompressParams *))GetProcAddress(handler, "ImageCodecPreDecompress");
    ImageCodecBandDecompress = (ComponentResult (*)(ComponentInstance,CodecDecompressParams *))GetProcAddress(handler, "ImageCodecBandDecompress");
    GetGWorldPixMap = (PixMapHandle (*)(GWorldPtr))GetProcAddress(handler, "GetGWorldPixMap");
    QTNewGWorldFromPtr = (OSErr(*)(GWorldPtr *,OSType,const Rect *,CTabHandle,any_t*,GWorldFlags,any_t*,long))GetProcAddress(handler, "QTNewGWorldFromPtr");
    NewHandleClear = (OSErr(*)(Size))GetProcAddress(handler, "NewHandleClear");
    //     = GetProcAddress(handler, "");

    if(!InitializeQTML || !EnterMovies || !FindNextComponent || !ImageCodecBandDecompress){
	MSG_ERR("invalid qt DLL!\n");
	return MPXP_False;
    }

    result=InitializeQTML(6+16);
//    result=InitializeQTML(0);
    MSG_V("InitializeQTML returned %i\n",result);
//    result=EnterMovies();
//    MSG_V("EnterMovies->%d\n",result);
#endif /* !MACOSX */

#if 0
    memset(&desc,0,sizeof(desc));
    while((prev=FindNextComponent(prev,&desc))){
	ComponentDescription desc2;
	unsigned char* c1=&desc2.componentType;
	unsigned char* c2=&desc2.componentSubType;
	memset(&desc2,0,sizeof(desc2));
//	MSG_V("juhee %p (%p)\n",prev,&desc);
	GetComponentInfo(prev,&desc2,NULL,NULL,NULL);
	MSG_V("DESC: %c%c%c%c/%c%c%c%c [0x%X/0x%X] 0x%X\n",
	    c1[3],c1[2],c1[1],c1[0],
	    c2[3],c2[2],c2[1],c2[0],
	    desc2.componentType,desc2.componentSubType,
	    desc2.componentFlags);
    }
#endif

    memset(&desc,0,sizeof(desc));
    desc.componentType= (((unsigned char)'i')<<24)|
			(((unsigned char)'m')<<16)|
			(((unsigned char)'d')<<8)|
			(((unsigned char)'c'));
#if 0
    desc.componentSubType= 
		    (((unsigned char)'S'<<24))|
			(((unsigned char)'V')<<16)|
			(((unsigned char)'Q')<<8)|
			(((unsigned char)'3'));
#else
    desc.componentSubType = bswap_32(sh->fourcc);
#endif
    desc.componentManufacturer=0;
    desc.componentFlags=0;
    desc.componentFlagsMask=0;

    MSG_V("Count = %d\n",CountComponents(&desc));
    prev=FindNextComponent(NULL,&desc);
    if(!prev){
	MSG_ERR("Cannot find requested component\n");
	return MPXP_False;
    }
    MSG_V("Found it! ID = 0x%X\n",prev);

    ci=OpenComponent(prev);
    MSG_V("ci=%p\n",ci);

    memset(&icap,0,sizeof(icap));
    cres=ImageCodecInitialize(ci,&icap);
    MSG_V("ImageCodecInitialize->%p  size=%d (%d)\n",cres,icap.recordSize,icap.decompressRecordSize);

    memset(&cinfo,0,sizeof(cinfo));
    cres=ImageCodecGetCodecInfo(ci,&cinfo);
    MSG_V("Flags: compr: 0x%X  decomp: 0x%X format: 0x%X\n",
	cinfo.compressFlags, cinfo.decompressFlags, cinfo.formatFlags);
    MSG_V("Codec name: %.*s\n",((unsigned char*)&cinfo.typeName)[0],
	((unsigned char*)&cinfo.typeName)+1);

    //make a yuy2 gworld
    OutBufferRect.top=0;
    OutBufferRect.left=0;
    OutBufferRect.right=sh->src_w;
    OutBufferRect.bottom=sh->src_h;

    //Fill the imagedescription for our SVQ3 frame
    //we can probably get this from Demuxer
#if 0
    framedescHandle=(ImageDescriptionHandle)NewHandleClear(sizeof(ImageDescription)+200);
    MSG_V("framedescHandle=%p  *p=%p\n",framedescHandle,*framedescHandle);
{ FILE* f=fopen("/root/.wine/fake_windows/IDesc","r");
  if(!f) MSG_ERR("filenot found: IDesc\n");
  fread(*framedescHandle,sizeof(ImageDescription)+200,1,f);
  fclose(f);
}
#else
    if(!sh->ImageDesc) sh->ImageDesc=(sh->bih+1); // hack for SVQ3-in-AVI
    MSG_V("ImageDescription size: %d\n",((ImageDescription*)(sh->ImageDesc))->idSize);
    framedescHandle=(ImageDescriptionHandle)NewHandleClear(((ImageDescription*)(sh->ImageDesc))->idSize);
    memcpy(*framedescHandle,sh->ImageDesc,((ImageDescription*)(sh->ImageDesc))->idSize);
    dump_ImageDescription(*framedescHandle);
#endif
//Find codecscomponent for video decompression
//    result = FindCodec ('SVQ1',anyCodec,&compressor,&decompressor );                 
//    MSG_V("FindCodec SVQ1 returned:%i compressor: 0x%X decompressor: 0x%X\n",result,compressor,decompressor);

    sh->context = kYUVSPixelFormat;
#if 1
    int imgfmt = sh->codec->outfmt[sh->outfmtidx];
    int qt_imgfmt;
    switch(imgfmt) {
	case IMGFMT_YUY2:
	    qt_imgfmt = kYUVSPixelFormat;
	    break;
	case IMGFMT_YVU9:
	    qt_imgfmt = 0x73797639; //kYVU9PixelFormat;
	    break;
	case IMGFMT_YV12:
	    qt_imgfmt = 0x79343230;
	    break;
	case IMGFMT_UYVY:
	    qt_imgfmt = kUYVY422PixelFormat;
	    break;
	case IMGFMT_YVYU:
	    qt_imgfmt = kYVYU422PixelFormat;
	    imgfmt = IMGFMT_YUY2;
	    break;
	case IMGFMT_RGB16:
	    qt_imgfmt = k16LE555PixelFormat;
	    break;
	case IMGFMT_BGR24:
	    qt_imgfmt = k24BGRPixelFormat;
	    break;
	case IMGFMT_BGR32:
	    qt_imgfmt = k32BGRAPixelFormat;
	    break;
	case IMGFMT_RGB32:
	    qt_imgfmt = k32RGBAPixelFormat;
	    break;
	default:
	    MSG_ERR("Unknown requested csp\n");
	    return MPXP_False;
    }
    MSG_V("imgfmt: %s qt_imgfmt: %.4s\n", vo_format_name(imgfmt), &qt_imgfmt);
    sh->context = qt_imgfmt;
    if(!mpcodecs_config_vo(sh,sh->src_w,sh->src_h,NULL,libinput)) return MPXP_False;
#else
    if(!mpcodecs_config_vo(sh,sh->src_w,sh->src_h,NULL,libinput)) return MPXP_False;
#endif
    return MPXP_Ok;
}

// uninit driver
static void uninit(sh_video_t *sh){
#ifdef MACOSX
    ExitMovies();
#endif
}

// decode a frame
static mp_image_t* decode(sh_video_t *sh,const enc_frame_t* frame){
    long result = 1;
    int i;
    mp_image_t* mpi;
    ComponentResult cres;

    if(frame->len<=0) return NULL; // skipped frame

    mpi=mpcodecs_get_image(sh, MP_IMGTYPE_STATIC, MP_IMGFLAG_PRESERVE, 
	sh->src_w, sh->src_h);
    if(mpi->flags&MP_IMGFLAG_DIRECT) mpi->flags|=MP_IMGFLAG_RENDERED;

    decpar.data = (const char*)frame->data;
    decpar.bufferSize = frame->len;
    (**framedescHandle).dataSize=frame->len;

if(!codec_inited){
    result = QTNewGWorldFromPtr(
        &OutBufferGWorld,
//        kYUVSPixelFormat, //pixel format of new GWorld == YUY2
	sh->context,
        &OutBufferRect,   //we should benchmark if yvu9 is faster for svq3, too
        0,
        0,
        0,
        mpi->planes[0],
        mpi->stride[0]);
    MSG_V("NewGWorldFromPtr returned:%d\n",65536-(result&0xffff));
//    if (65536-(result&0xFFFF) != 10000)
//	return NULL;

//    MSG_V("IDesc=%d\n",sizeof(ImageDescription));

    decpar.imageDescription = framedescHandle;
    decpar.startLine=0;
    decpar.stopLine=(**framedescHandle).height;
    decpar.frameNumber = 1; //1
//    decpar.conditionFlags=0xFFD; // first
//    decpar.callerFlags=0x2001; // first
    decpar.matrixFlags = 0;
    decpar.matrixType = 0;
    decpar.matrix = 0;
    decpar.capabilities=&codeccap;
//    decpar.accuracy = 0x1680000; //codecNormalQuality;
    decpar.accuracy = codecNormalQuality;
//    decpar.port = OutBufferGWorld;
//    decpar.preferredOffscreenPixelSize=17207;

//    decpar.sequenceID=mp_malloc(1000);
//    memset(decpar.sequenceID,0,1000);

//    SrcRect.top=17207;
//    SrcRect.left=0;
//    SrcRect.right=0;//image_width;
//    SrcRect.bottom=0;//image_height;

//    decpar.srcRect = SrcRect;
    decpar.srcRect = OutBufferRect;

    decpar.transferMode = srcCopy;
    decpar.dstPixMap = **GetGWorldPixMap( OutBufferGWorld);//destPixmap; 

    cres=ImageCodecPreDecompress(ci,&decpar);
    MSG_V("ImageCodecPreDecompress cres=0x%X\n",cres);

    if(decpar.wantedDestinationPixelTypes)
    { OSType *p=*(decpar.wantedDestinationPixelTypes);
      if(p) while(*p){
          MSG_V("supported csp: 0x%08X %.4s\n",*p,p);
	  ++p;
      }
    }

//    decpar.conditionFlags=0x10FFF; // first
//    decpar.preferredOffscreenPixelSize=17207;

//    decpar.conditionFlags=0x10FFD; // first

//	cres=ImageCodecPreDecompress(ci,&decpar);
//    MSG_V("ImageCodecPreDecompress cres=0x%X\n",cres);


    codec_inited=1;
}

#if 0
    if(decpar.frameNumber==124){
	decpar.frameNumber=1;
	cres=ImageCodecPreDecompress(ci,&decpar);
	MSG_V("ImageCodecPreDecompress cres=0x%X\n",cres);
    }
#endif

    cres=ImageCodecBandDecompress(ci,&decpar);

    ++decpar.frameNumber;

    if(cres&0xFFFF){
	MSG_V("ImageCodecBandDecompress cres=0x%X (-0x%X) %d\n",cres,-cres,cres);
	return NULL;
    }
    
//    for(i=0;i<8;i++)
//	MSG_V("img_base[%d]=%p\n",i,((int*)decpar.dstPixMap.baseAddr)[i]);

if((int)sh->context==0x73797639){	// Sorenson 16-bit YUV -> std YVU9

    short *src0=(short *)((char*)decpar.dstPixMap.baseAddr+0x20);

    for(i=0;i<mpi->h;i++){
	int x;
	unsigned char* dst=mpi->planes[0]+i*mpi->stride[0];
	unsigned short* src=src0+i*((mpi->w+15)&(~15));
	for(x=0;x<mpi->w;x++) dst[x]=src[x];
    }
    src0+=((mpi->w+15)&(~15))*((mpi->h+15)&(~15));
    for(i=0;i<mpi->h/4;i++){
	int x;
	unsigned char* dst=mpi->planes[1]+i*mpi->stride[1];
	unsigned short* src=src0+i*(((mpi->w+63)&(~63))/4);
	for(x=0;x<mpi->w/4;x++) dst[x]=src[x];
	src+=((mpi->w+63)&(~63))/4;
    }
    src0+=(((mpi->w+63)&(~63))/4)*(((mpi->h+63)&(~63))/4);
    for(i=0;i<mpi->h/4;i++){
	int x;
	unsigned char* dst=mpi->planes[2]+i*mpi->stride[2];
	unsigned short* src=src0+i*(((mpi->w+63)&(~63))/4);
	for(x=0;x<mpi->w/4;x++) dst[x]=src[x];
	src+=((mpi->w+63)&(~63))/4;
    }

}
    return mpi;
}
