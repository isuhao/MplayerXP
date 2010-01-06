/*
   ad.h - audio decoders interface
*/
#ifndef __AD_H
#define __AD_H

typedef struct ad_info_s
{
        /* driver description ("Autodesk FLI/FLC Animation decoder" */
        const char *descr;
        /* driver name ("dshow") */
        const char *driver_name;
        /* interface author/maintainer */
        const char *author;
        /* URL of homepage */
        const char *url;
} ad_info_t;

#define CONTROL_OK 1
#define CONTROL_TRUE 1
#define CONTROL_FALSE 0
#define CONTROL_UNKNOWN -1
#define CONTROL_ERROR -2
#define CONTROL_NA -3

#define ADCTRL_RESYNC_STREAM 0
#define ADCTRL_SKIP_FRAME    1

/* interface of video decoder drivers */
typedef struct ad_functions_s
{
	const ad_info_t *info;
	const config_t*  options;/**< Optional: MPlayerXP's option related */
	int (*preinit)(sh_audio_t *);
        int (*init)(sh_audio_t *sh);
        void (*uninit)(sh_audio_t *sh);
        int (*control)(sh_audio_t *sh,int cmd,void* arg, ...);
	int (*decode_audio)(sh_audio_t *sh_audio,unsigned char *buf,int minlen,int maxlen,float *pts);
} ad_functions_t;

// NULL terminated array of all drivers
extern const ad_functions_t* mpcodecs_ad_drivers[];

#define FIX_APTS(sh_audio,pts,in_size) (sh_audio->i_bps?((float)(pts)+(float)(in_size)/(float)sh_audio->i_bps):((float)(pts)))

#endif
