typedef struct vd_info_s
{
        /* codec long name ("Autodesk FLI/FLC Animation decoder" */
        const char *codec_name;
        /* driver name ("dshow") */
        const char *driver_name;
        /* interface author/maintainer */
        const char *maintainer;
        /* codec author ("Aaron Holtzman <aholtzma@ess.engr.uvic.ca>") */
        const char *author;
        /* any additional comments */
        const char *comment;
} vd_info_t;

/* interface of video decoder drivers */
typedef struct vd_functions_s
{
	const vd_info_t *info;
	const config_t*  options;/**< Optional: MPlayerXP's option related */
        int (*init)(sh_video_t *sh);
        void (*uninit)(sh_video_t *sh);
        int (*control)(sh_video_t *sh,int cmd,void* arg, ...);
        mp_image_t* (*decode)(sh_video_t *sh,void* data,int len,int flags);
} vd_functions_t;

// NULL terminated array of all drivers
extern const vd_functions_t* mpcodecs_vd_drivers[];

#define CONTROL_OK 1
#define CONTROL_TRUE 1
#define CONTROL_FALSE 0
#define CONTROL_UNKNOWN -1
#define CONTROL_ERROR -2
#define CONTROL_NA -3

#define VDCTRL_QUERY_FORMAT 3 /* test for availabilty of a format */
#define VDCTRL_QUERY_MAX_PP_LEVEL 4 /* test for postprocessing support (max level) */
#define VDCTRL_SET_PP_LEVEL 5 /* set postprocessing level */
#define VDCTRL_SET_EQUALIZER 6 /* set color options (brightness,contrast etc) */
#define VDCTRL_RESYNC_STREAM 7 /* resync video stream if needed */

// callbacks:
int mpcodecs_config_vo(sh_video_t *sh, int w, int h, void *tune);
mp_image_t* mpcodecs_get_image(sh_video_t *sh, int mp_imgtype, int mp_imgflag,int w, int h);
void mpcodecs_draw_slice(sh_video_t *sh, mp_image_t*);

