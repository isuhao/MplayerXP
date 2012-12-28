#ifndef AVIFILE_DMO_VIDEODECODER_H
#define AVIFILE_DMO_VIDEODECODER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _DMO_VideoDecoder DMO_VideoDecoder;

int DMO_VideoDecoder_GetCapabilities(DMO_VideoDecoder *self);

DMO_VideoDecoder * DMO_VideoDecoder_Open(char* dllname, GUID* guid, BITMAPINFOHEADER * format, int flip, int maxauto);

void DMO_VideoDecoder_Destroy(DMO_VideoDecoder *self);

void DMO_VideoDecoder_StartInternal(DMO_VideoDecoder *self);

void DMO_VideoDecoder_StopInternal(DMO_VideoDecoder *self);

int DMO_VideoDecoder_DecodeInternal(DMO_VideoDecoder *self, const any_t* src, int size, int is_keyframe, char* pImage);

/*
 * bits == 0   - leave unchanged
 */
//int SetDestFmt(DMO_VideoDecoder * self, int bits = 24, fourcc_t csp = 0);
int DMO_VideoDecoder_SetDestFmt(DMO_VideoDecoder *self, int bits, unsigned int csp);
int DMO_VideoDecoder_SetDirection(DMO_VideoDecoder *self, int d);

#ifdef __cplusplus
}
#endif
#endif /* AVIFILE_DMO_VIDEODECODER_H */