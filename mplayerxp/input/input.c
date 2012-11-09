#include "../mp_config.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <ctype.h>

#include "input.h"
#include "mouse.h"
#ifdef MP_DEBUG
#include <assert.h>
#endif
#include "libmpdemux/stream.h"
#include "osdep/getch2.h"
#include "osdep/keycodes.h"
#include "osdep/get_path.h"
#include "osdep/timer.h"
#include "osdep/mplib.h"
#include "libmpconf/cfgparser.h"

#include "joystick.h"

#ifdef HAVE_LIRC
#include "lirc.h"
#endif

#ifdef HAVE_LIRCC
#include <lirc/lircc.h>
#endif

#include "in_msg.h"

/// This array defines all know commands.
/// The first field is an id used to recognize the command without too many strcmp
/// The second is abviously the command name
/// The third is the minimum number of argument this command need
/// Then come the definition of each argument, terminated with and arg of type -1
/// A command can take maximum MP_CMD_MAX_ARGS-1 arguments (-1 because of
/// the terminal one) wich is actually 9

/// For the args, the first field is the type (actually int, float or string), the second
/// is the default value wich is used for optional arguments

static const mp_cmd_t mp_cmds[] = {
  { MP_CMD_SEEK, "seek", 1, { {MP_CMD_ARG_INT,{0}}, {MP_CMD_ARG_INT,{0}}, {-1,{0}} } },
  { MP_CMD_SPEED_INCR, "speed_incr", 1, { {MP_CMD_ARG_FLOAT,{0}}, {-1,{0}} } },
  { MP_CMD_SPEED_MULT, "speed_mult", 1, { {MP_CMD_ARG_FLOAT,{0}}, {-1,{0}} } },
  { MP_CMD_SPEED_SET, "speed_set", 1, { {MP_CMD_ARG_FLOAT,{0}}, {-1,{0}} } },
  { MP_CMD_SWITCH_AUDIO, "cycle_audio", 0, { {-1,{0}} } },
  { MP_CMD_SWITCH_VIDEO, "cycle_video", 0, { {-1,{0}} } },
  { MP_CMD_SWITCH_SUB, "cycle_subtitles", 0, { {-1,{0}} } },
  { MP_CMD_QUIT, "quit", 0, { {-1,{0}} } },
  { MP_CMD_SOFT_QUIT, "soft_quit", 0, { {-1,{0}} } },
  { MP_CMD_PAUSE, "pause", 0, { {-1,{0}} } },
  { MP_CMD_FRAME_STEP, "frame_step", 0, { {-1,{0}} } },
  { MP_CMD_PLAY_TREE_STEP, "pt_step",1, { { MP_CMD_ARG_INT ,{0}}, { MP_CMD_ARG_INT ,{0}}, {-1,{0}} } },
  { MP_CMD_PLAY_TREE_UP_STEP, "pt_up_step",1,  { { MP_CMD_ARG_INT,{0} }, { MP_CMD_ARG_INT ,{0}}, {-1,{0}} } },
  { MP_CMD_PLAY_ALT_SRC_STEP, "alt_src_step",1, { { MP_CMD_ARG_INT,{0} }, {-1,{0}} } },
  { MP_CMD_OSD, "osd",0, { {MP_CMD_ARG_INT,{-1}}, {-1,{0}} } },
  { MP_CMD_VOLUME, "volume", 1, { { MP_CMD_ARG_INT,{0} }, {-1,{0}} } },
  { MP_CMD_MUTE, "mute", 0, { {-1,{0}} } },
  { MP_CMD_CONTRAST, "contrast",1,  { {MP_CMD_ARG_INT,{0}}, {MP_CMD_ARG_INT,{0}}, {-1,{0}} } },
  { MP_CMD_BRIGHTNESS, "brightness",1,  { {MP_CMD_ARG_INT,{0}}, {MP_CMD_ARG_INT,{0}}, {-1,{0}} }  },
  { MP_CMD_HUE, "hue",1,  { {MP_CMD_ARG_INT,{0}}, {MP_CMD_ARG_INT,{0}}, {-1,{0}} } },
  { MP_CMD_SATURATION, "saturation",1,  { {MP_CMD_ARG_INT,{0}}, {MP_CMD_ARG_INT,{0}}, {-1,{0}} }  },
  { MP_CMD_FRAMEDROPPING, "frame_drop",0, { { MP_CMD_ARG_INT,{-1} }, {-1,{0}} } },
  { MP_CMD_SUB_POS, "sub_pos", 1, { {MP_CMD_ARG_INT,{0}}, {MP_CMD_ARG_INT,{0}}, {-1,{0}} } },
#ifdef USE_TV
  { MP_CMD_TV_STEP_CHANNEL, "tv_step_channel", 1,  { { MP_CMD_ARG_INT ,{0}}, {-1,{0}} }},
  { MP_CMD_TV_STEP_NORM, "tv_step_norm",0, { {-1,{0}} }  },
  { MP_CMD_TV_STEP_CHANNEL_LIST, "tv_step_chanlist", 0, { {-1,{0}} }  },
#endif
  { MP_CMD_VO_FULLSCREEN, "vo_fullscreen", 0, { {-1,{0}} } },
  { MP_CMD_VO_SCREENSHOT, "vo_screenshot", 0, { {-1,{0}} } },
  { MP_CMD_PANSCAN, "panscan",1,  { {MP_CMD_ARG_FLOAT,{0}}, {MP_CMD_ARG_INT,{0}}, {-1,{0}} } },

#ifdef USE_DVDNAV
  { MP_CMD_DVDNAV, "dvdnav", 1, { {MP_CMD_ARG_INT,{0}}, {-1,{0}} } },
  { MP_CMD_DVDNAV_EVENT, "dvdnav_event", 1, { { MP_CMD_ARG_VOID, {0}}, {-1, {0}} } },
#endif
  { MP_CMD_MENU, "menu",1,  { {MP_CMD_ARG_STRING, {0}}, {-1,{0}} } },
  { MP_CMD_SET_MENU, "set_menu",1,  { {MP_CMD_ARG_STRING, {0}},  {MP_CMD_ARG_STRING, {0}}, {-1,{0}} } },
  { MP_CMD_CHELP, "help", 0, { {-1,{0}} } },
  { MP_CMD_CEXIT, "exit", 0, { {-1,{0}} } },
  { MP_CMD_CHIDE, "hide", 0, { {MP_CMD_ARG_INT,{3000}}, {-1,{0}} } },

  { 0, NULL, 0, {} }
};

/// The names of the key for input.conf
/// If you add some new keys, you also need to add them here

static const mp_key_name_t key_names[] = {
  { ' ', "SPACE" },
  { KEY_ENTER, "ENTER" },
  { KEY_TAB, "TAB" },
  { KEY_CTRL, "CTRL" },
  { KEY_BACKSPACE, "BS" },
  { KEY_DELETE, "DEL" },
  { KEY_INSERT, "INS" },
  { KEY_HOME, "HOME" },
  { KEY_END, "END" },
  { KEY_PAGE_UP, "PGUP" },
  { KEY_PAGE_DOWN, "PGDWN" },
  { KEY_ESC, "ESC" },
  { KEY_RIGHT, "RIGHT" },
  { KEY_LEFT, "LEFT" },
  { KEY_DOWN, "DOWN" },
  { KEY_UP, "UP" },
  { KEY_F+1, "F1" },
  { KEY_F+2, "F2" },
  { KEY_F+3, "F3" },
  { KEY_F+4, "F4" },
  { KEY_F+5, "F5" },
  { KEY_F+6, "F6" },
  { KEY_F+7, "F7" },
  { KEY_F+8, "F8" },
  { KEY_F+9, "F9" },
  { KEY_F+10, "F10" },
  { KEY_KP0, "KP0" },
  { KEY_KP1, "KP1" },
  { KEY_KP2, "KP2" },
  { KEY_KP3, "KP3" },
  { KEY_KP4, "KP4" },
  { KEY_KP5, "KP5" },
  { KEY_KP6, "KP6" },
  { KEY_KP7, "KP7" },
  { KEY_KP8, "KP8" },
  { KEY_KP9, "KP9" },
  { KEY_KPDEL, "KP_DEL" },
  { KEY_KPDEC, "KP_DEL" },
  { KEY_KPINS, "KP0" },
  { KEY_KPENTER, "KP_ENTER" },
  { MOUSE_BTN0, "MOUSE_BTN0" },
  { MOUSE_BTN1, "MOUSE_BTN1" },
  { MOUSE_BTN2, "MOUSE_BTN2" },
  { MOUSE_BTN3, "MOUSE_BTN3" },
  { MOUSE_BTN4, "MOUSE_BTN4" },
  { MOUSE_BTN5, "MOUSE_BTN5" },
  { MOUSE_BTN6, "MOUSE_BTN6" },
  { MOUSE_BTN7, "MOUSE_BTN7" },
  { MOUSE_BTN8, "MOUSE_BTN8" },
  { MOUSE_BTN9, "MOUSE_BTN9" },
  { MOUSE_BTN0_DBL, "MOUSE_BTN0_DBL" },
  { MOUSE_BTN1_DBL, "MOUSE_BTN1_DBL" },
  { MOUSE_BTN2_DBL, "MOUSE_BTN2_DBL" },
  { MOUSE_BTN3_DBL, "MOUSE_BTN3_DBL" },
  { MOUSE_BTN4_DBL, "MOUSE_BTN4_DBL" },
  { MOUSE_BTN5_DBL, "MOUSE_BTN5_DBL" },
  { MOUSE_BTN6_DBL, "MOUSE_BTN6_DBL" },
  { MOUSE_BTN7_DBL, "MOUSE_BTN7_DBL" },
  { MOUSE_BTN8_DBL, "MOUSE_BTN8_DBL" },
  { MOUSE_BTN9_DBL, "MOUSE_BTN9_DBL" },
  { JOY_AXIS1_MINUS, "JOY_UP" },
  { JOY_AXIS1_PLUS, "JOY_DOWN" },
  { JOY_AXIS0_MINUS, "JOY_LEFT" },
  { JOY_AXIS0_PLUS, "JOY_RIGHT" },

  { JOY_AXIS0_PLUS, "JOY_AXIS0_PLUS" },
  { JOY_AXIS0_MINUS, "JOY_AXIS0_MINUS" },
  { JOY_AXIS1_PLUS, "JOY_AXIS1_PLUS" },
  { JOY_AXIS1_MINUS, "JOY_AXIS1_MINUS" },
  { JOY_AXIS2_PLUS, "JOY_AXIS2_PLUS" },
  { JOY_AXIS2_MINUS, "JOY_AXIS2_MINUS" },
  { JOY_AXIS3_PLUS, "JOY_AXIS3_PLUS" },
  { JOY_AXIS3_MINUS, "JOY_AXIS3_MINUS" },
  { JOY_AXIS4_PLUS, "JOY_AXIS4_PLUS" },
  { JOY_AXIS4_MINUS, "JOY_AXIS4_MINUS" },
  { JOY_AXIS5_PLUS, "JOY_AXIS5_PLUS" },
  { JOY_AXIS5_MINUS, "JOY_AXIS5_MINUS" },
  { JOY_AXIS6_PLUS, "JOY_AXIS6_PLUS" },
  { JOY_AXIS6_MINUS, "JOY_AXIS6_MINUS" },
  { JOY_AXIS7_PLUS, "JOY_AXIS7_PLUS" },
  { JOY_AXIS7_MINUS, "JOY_AXIS7_MINUS" },
  { JOY_AXIS8_PLUS, "JOY_AXIS8_PLUS" },
  { JOY_AXIS8_MINUS, "JOY_AXIS8_MINUS" },
  { JOY_AXIS9_PLUS, "JOY_AXIS9_PLUS" },
  { JOY_AXIS9_MINUS, "JOY_AXIS9_MINUS" },

  { JOY_BTN0, "JOY_BTN0" },
  { JOY_BTN1, "JOY_BTN1" },
  { JOY_BTN2, "JOY_BTN2" },
  { JOY_BTN3, "JOY_BTN3" },
  { JOY_BTN4, "JOY_BTN4" },
  { JOY_BTN5, "JOY_BTN5" },
  { JOY_BTN6, "JOY_BTN6" },
  { JOY_BTN7, "JOY_BTN7" },
  { JOY_BTN8, "JOY_BTN8" },
  { JOY_BTN9, "JOY_BTN9" },

  { KEY_XF86_STANDBY, "XF86_STANDBY" },
  { KEY_XF86_POWER, "XF86_POWER" },
  { KEY_XF86_PAUSE, "XF86_PAUSE" },
  { KEY_XF86_STOP, "XF86_STOP" },
  { KEY_XF86_PREV, "XF86_PREV" },
  { KEY_XF86_NEXT, "XF86_NEXT" },
  { KEY_XF86_VOLUME_UP, "XF86_VOLUME_UP" },
  { KEY_XF86_VOLUME_DN, "XF86_VOLUME_DN" },
  { KEY_XF86_MUTE, "XF86_MUTE" },
  { KEY_XF86_EJECT, "XF86_EJECT" },
  { KEY_XF86_MENU, "XF86_MENU" },
  { KEY_XF86_PLAY, "XF86_PLAY" },
  { KEY_XF86_FORWARD, "XF86_FORWARD" },
  { KEY_XF86_REWIND, "XF86_REWIND" },
  { KEY_XF86_BRIGHTNESS, "XF86_BRIGHTNESS" },
  { KEY_XF86_CONTRAST, "XF86_CONTRAST" },
  { KEY_XF86_SATURATION, "XF86_SATURATION" },
  { KEY_XF86_SCREENSAVE, "XF86_SCREENSAVE" },
  { KEY_XF86_REFRESH, "XF86_REFRESH" },

  { 0, NULL }
};

// This is the default binding. The content of input.conf override these ones.
// The first args is a null terminated array of key codes.
// The second is the command

static const mp_cmd_bind_t def_cmd_binds[] = {

  { {  MOUSE_BTN3, 0 }, "seek 10" },
  { {  MOUSE_BTN4, 0 }, "seek -10" },
  { {  MOUSE_BTN5, 0 }, "volume 1" },
  { {  MOUSE_BTN6, 0 }, "volume -1" },

#ifdef USE_DVDNAV
  { { KEY_KP8, 0 }, "dvdnav 1" },   // up
  { { KEY_KP2, 0 }, "dvdnav 2" },   // down
  { { KEY_KP4, 0 }, "dvdnav 3" },   // left
  { { KEY_KP6, 0 }, "dvdnav 4" },   // right
  { { KEY_KP5, 0 }, "dvdnav 5" },   // menu
  { { KEY_KP0, 0 }, "dvdnav 6" },   // select
  { { KEY_KPINS, 0 }, "dvdnav 6" },   // select
#endif
  { { KEY_F+3, 0 }, "menu menu" },
  { { KEY_XF86_MENU, 0 }, "menu menu" },
  { { KEY_F+2, 0 }, "set_menu" },
  { { KEY_F+1, 0 }, "menu help" },
  { { KEY_F+10, 0 }, "menu exit" },
  { { KEY_F+8, 0 }, "menu hide" },

  { { KEY_RIGHT, 0 }, "seek 10" },
  { {  KEY_LEFT, 0 }, "seek -10" },
  { {  KEY_UP, 0 }, "seek 60" },
  { {  KEY_DOWN, 0 }, "seek -60" },
  { {  KEY_PAGE_UP, 0 }, "seek 600" },
  { { KEY_PAGE_DOWN, 0 }, "seek -600" },
  { { '[', 0 }, "speed_mult 0.9091" },
  { { ']', 0 }, "speed_mult 1.1" },
  { { '{', 0 }, "speed_mult 0.5" },
  { { '}', 0 }, "speed_mult 2.0" },
  { { KEY_BACKSPACE, 0 }, "speed_set 1.0" },
  { { 'q', 0 }, "soft_quit" },
  { { KEY_ESC, 0 }, "quit" },
  { { 'p', 0 }, "pause" },
  { { ' ', 0 }, "pause" },
  { { '.', 0 }, "frame_step" },
  { { KEY_HOME, 0 }, "pt_up_step 1" },
  { { KEY_END, 0 }, "pt_up_step -1" },
  { { '>', 0 }, "pt_step 1" },
  { { KEY_ENTER, 0 }, "pt_step 1 1" },
  { { '<', 0 }, "pt_step -1" },
  { { KEY_INS, 0 }, "alt_src_step 1" },
  { { KEY_DEL, 0 }, "alt_src_step -1" },
  { { 'o', 0 }, "osd" },
  { { '9', 0 }, "volume -1" },
  { { '/', 0 }, "volume -1" },
  { { '0', 0 }, "volume 1" },
  { { '*', 0 }, "volume 1" },
  { { 'm', 0 }, "mute" },
  { { '1', 0 }, "contrast -1" },
  { { '2', 0 }, "contrast 1" },
  { { '3', 0 }, "brightness -1" },
  { { '4', 0 }, "brightness 1" },
  { { '5', 0 }, "hue -1" },
  { { '6', 0 }, "hue 1" },
  { { '7', 0 }, "saturation -1" },
  { { '8', 0 }, "saturation 1" },
  { { 'd', 0 }, "frame_drop" },
  { { 'r', 0 }, "sub_pos -1" },
  { { 't', 0 }, "sub_pos +1" },
#ifdef USE_TV
  { { 'h', 0 }, "tv_step_channel 1" },
  { { 'k', 0 }, "tv_step_channel -1" },
  { { 'n', 0 }, "tv_step_norm" },
  { { 'u', 0 }, "tv_step_chanlist" },
#endif
#ifdef HAVE_JOYSTICK
  { { JOY_AXIS0_PLUS, 0 }, "seek 10" },
  { { JOY_AXIS0_MINUS, 0 }, "seek -10" },
  { { JOY_AXIS1_MINUS, 0 }, "seek 60" },
  { { JOY_AXIS1_PLUS, 0 }, "seek -60" },
  { { JOY_BTN0, 0 }, "pause" },
  { { JOY_BTN1, 0 }, "osd" },
  { { JOY_BTN2, 0 }, "volume 1"},
  { { JOY_BTN3, 0 }, "volume -1"},
#endif
  { { 'f', 0 }, "vo_fullscreen" },
  { { 's', 0 }, "vo_screenshot" },
  { { 'w', 0 }, "panscan -0.1" },
  { { 'e', 0 }, "panscan +0.1" },

  { { 'a', 0 }, "cycle_audio" },
  { { 'v', 0 }, "cycle_video" },
  { { 'c', 0 }, "cycle_subtitles" },

  { { KEY_XF86_PLAY, 0 }, "pause" },
  { { KEY_XF86_PAUSE, 0 }, "pause" },
  { { KEY_XF86_STANDBY, 0 }, "pause" },
  { { KEY_XF86_STOP, 0 }, "soft_quit" },
  { { KEY_XF86_POWER, 0 }, "quit" },
  { { KEY_XF86_EJECT, 0 }, "quit" },
  { { KEY_XF86_PREV, 0 }, "pt_step -1" },
  { { KEY_XF86_NEXT, 0 }, "pt_step 1" },
  { { KEY_XF86_REWIND, 0 }, "seek -60" },
  { { KEY_XF86_FORWARD, 0 }, "seek 60" },
  { { KEY_XF86_VOLUME_UP, 0 }, "volume 1"},
  { { KEY_XF86_VOLUME_DN, 0 }, "volume -1"},
  { { KEY_XF86_MUTE, 0 }, "mute" },
  { { KEY_XF86_SCREENSAVE, 0 }, "vo_screenshot" },
  { { KEY_XF86_BRIGHTNESS, 0 }, "brightness 1" },
  { { KEY_XF86_CONTRAST, 0 }, "contrast 1" },
  { { KEY_XF86_SATURATION, 0 }, "saturation 1" },
  { { KEY_XF86_REFRESH, 0 }, "pause" },

  { { 0 }, NULL }
};


#ifndef MP_MAX_KEY_FD
#define MP_MAX_KEY_FD 10
#endif

#ifndef MP_MAX_CMD_FD
#define MP_MAX_CMD_FD 10
#endif

#define MP_FD_EOF (1<<0)
#define MP_FD_DROP (1<<1)
#define MP_FD_DEAD (1<<2)
#define MP_FD_GOT_CMD (1<<3)
#define MP_FD_NO_SELECT (1<<4)

#define CMD_QUEUE_SIZE 100

typedef struct mp_input_fd {
    int fd;
    any_t* read_func;
    mp_close_func_t close_func;
    int flags;
    // This fields are for the cmd fds
    char* buffer;
    int pos,size;
} mp_input_fd_t;

typedef struct mp_cmd_filter_st mp_cmd_filter_t;

struct mp_cmd_filter_st {
    mp_input_cmd_filter filter;
    any_t* ctx;
    mp_cmd_filter_t* next;
};

// These are the user defined binds
static mp_cmd_bind_t* cmd_binds = NULL;
static mp_cmd_filter_t* cmd_filters = NULL;

// Callback to allow the menu filter to grab the incoming keys
void (*mp_input_key_cb)(int code) = NULL;

static mp_input_fd_t key_fds[MP_MAX_KEY_FD];
static unsigned int num_key_fd = 0;
static mp_input_fd_t cmd_fds[MP_MAX_CMD_FD];
static unsigned int num_cmd_fd = 0;
static mp_cmd_t* cmd_queue[CMD_QUEUE_SIZE];
static unsigned int cmd_queue_length = 0,cmd_queue_start = 0, cmd_queue_end = 0;

// this is the key currently down
static int key_down[MP_MAX_KEY_DOWN];
static unsigned int num_key_down = 0, last_key_down = 0;

// Autorepeat stuff
static short ar_state = -1;
static mp_cmd_t* ar_cmd = NULL;
static unsigned int ar_delay = 100, ar_rate = 8, last_ar = 0;

static int use_joystick = 1, use_lirc = 1, use_lircc = 1;
static char* config_file = "input.conf";

static const char* js_dev = "/dev/input/js0";

static const char* in_file = NULL;
static int in_file_fd = -1;

static int mp_input_print_key_list(const config_t* cfg);
static int mp_input_print_cmd_list(const config_t* cfg);

static const config_t joystick_conf[] = {
  { "on", &use_joystick,  CONF_TYPE_FLAG, CONF_GLOBAL, 0, 1, "enables using of joystick" },
  { "off", &use_joystick,  CONF_TYPE_FLAG, CONF_GLOBAL, 1, 0, "disables using of joystick" },
  { "dev", &js_dev, CONF_TYPE_STRING, CONF_GLOBAL, 0, 0, "specifies the joystick device" },
  { NULL, NULL, 0, 0, 0, 0, NULL}
};

extern char *lirc_configfile;
// Our command line options
static const config_t input_conf[] = {
  { "conf", &config_file, CONF_TYPE_STRING, CONF_GLOBAL, 0, 0, "specifies alternative input.conf" },
  { "ar-delay", &ar_delay, CONF_TYPE_INT, CONF_GLOBAL, 0, 0, "autorepeate a key delay in milliseconds (0 to disable)" },
  { "ar-rate", &ar_rate, CONF_TYPE_INT, CONF_GLOBAL, 0, 0, "number of key-presses per second generating on autorepeat" },
  { "keylist", mp_input_print_key_list, CONF_TYPE_FUNC, CONF_GLOBAL, 0, 0, "prints all keys that can be bound to commands" },
  { "cmdlist", mp_input_print_cmd_list, CONF_TYPE_FUNC, CONF_GLOBAL, 0, 0, "prints all commands that can be bound to keys" },
  { "file", &in_file, CONF_TYPE_STRING, CONF_GLOBAL, 0, 0, "specifes file with commands (useful for FIFO)" },
#ifdef HAVE_LIRC
  { "lircconf", &lirc_configfile, CONF_TYPE_STRING, CONF_GLOBAL, 0, 0, "specifies a config.file for LIRC"}, 
#endif
  { "lirc", &use_lirc, CONF_TYPE_FLAG, CONF_GLOBAL, 0, 1, "enables using of lirc" },
  { "nolirc", &use_lirc, CONF_TYPE_FLAG, CONF_GLOBAL, 1, 0, "disables using of lirc" },
  { "lircc", &use_lircc, CONF_TYPE_FLAG, CONF_GLOBAL, 0, 1, "enables using of lirc daemon" },
  { "nolircc", &use_lircc, CONF_TYPE_FLAG, CONF_GLOBAL, 1, 0, "disables using of lirc daemon" },
  { "joystick", &joystick_conf, CONF_TYPE_SUBCONFIG, 0, 0, 0, "Joystick related options" },
  { NULL, NULL, 0, 0, 0, 0, NULL}
};

static const config_t mp_input_opts[] = {
  { "input", &input_conf, CONF_TYPE_SUBCONFIG, 0, 0, 0, "Input specific options"},
  { NULL, NULL, 0, 0, 0, 0, NULL}
};

static int mp_input_default_key_func(int fd);
static int mp_input_default_cmd_func(int fd,char* buf, int l);
static char* mp_input_get_key_name(int key);

int mp_input_add_cmd_fd(int fd, int sel, mp_cmd_func_t read_func, mp_close_func_t close_func) {
    if(num_cmd_fd == MP_MAX_CMD_FD) {
	MSG_ERR("Too much command fd, unable to register fd %d\n",fd);
	return 0;
    }

    memset(&cmd_fds[num_cmd_fd],0,sizeof(mp_input_fd_t));
    cmd_fds[num_cmd_fd].fd = fd;
    cmd_fds[num_cmd_fd].read_func = read_func ? read_func : mp_input_default_cmd_func;
    cmd_fds[num_cmd_fd].close_func = close_func;
    if(!sel) cmd_fds[num_cmd_fd].flags = MP_FD_NO_SELECT;
    num_cmd_fd++;

    return 1;
}

void mp_input_rm_cmd_fd(int fd) {
    unsigned int i;

    for(i = 0; i < num_cmd_fd; i++) {
	if(cmd_fds[i].fd == fd) break;
    }
    if(i == num_cmd_fd) return;
    if(cmd_fds[i].close_func) cmd_fds[i].close_func(cmd_fds[i].fd);
    if(cmd_fds[i].buffer)     mp_free(cmd_fds[i].buffer);
    if(i + 1 < num_cmd_fd)    memmove(&cmd_fds[i],&cmd_fds[i+1],(num_cmd_fd - i - 1)*sizeof(mp_input_fd_t));
    num_cmd_fd--;
}

void mp_input_rm_key_fd(int fd) {
    unsigned int i;

    for(i = 0; i < num_key_fd; i++) {
	if(key_fds[i].fd == fd) break;
    }
    if(i == num_key_fd) return;
    if(key_fds[i].close_func) key_fds[i].close_func(key_fds[i].fd);
    if(i + 1 < num_key_fd)    memmove(&key_fds[i],&key_fds[i+1],(num_key_fd - i - 1)*sizeof(mp_input_fd_t));
    num_key_fd--;
}

int mp_input_add_key_fd(int fd, int sel, mp_key_func_t read_func, mp_close_func_t close_func) {
    if(num_key_fd == MP_MAX_KEY_FD) {
	MSG_ERR("Too much key fd, unable to register fd %d\n",fd);
	return 0;
    }

    memset(&key_fds[num_key_fd],0,sizeof(mp_input_fd_t));
    key_fds[num_key_fd].fd = fd;
    key_fds[num_key_fd].read_func = read_func ? read_func : mp_input_default_key_func;
    key_fds[num_key_fd].close_func = close_func;
    if(!sel) key_fds[num_key_fd].flags |= MP_FD_NO_SELECT;
    num_key_fd++;

    return 1;
}



mp_cmd_t* mp_input_parse_cmd(char* str) {
    int i,l;
    char *ptr,*e;
    mp_cmd_t *cmd;
    const mp_cmd_t *cmd_def;

#ifdef MP_DEBUG
    assert(str != NULL);
#endif

    for(ptr = str ; ptr[0] != '\0'  && ptr[0] != '\t' && ptr[0] != ' ' ; ptr++)
    /* NOTHING */;
    if(ptr[0] != '\0') l = ptr-str;
    else  l = strlen(str);

    if(l == 0) return NULL;

    for(i=0; mp_cmds[i].name != NULL; i++) {
	if(strncasecmp(mp_cmds[i].name,str,l) == 0) break;
    }

    if(mp_cmds[i].name == NULL) return NULL;

    cmd_def = &mp_cmds[i];

    cmd = (mp_cmd_t*)mp_malloc(sizeof(mp_cmd_t));
    cmd->id = cmd_def->id;
    cmd->name = mp_strdup(cmd_def->name);

    ptr = str;

    for(i=0; ptr && i < MP_CMD_MAX_ARGS; i++) {
	ptr = strchr(ptr,' ');
	if(!ptr) break;
	while(ptr[0] == ' ' || ptr[0] == '\t') ptr++;
	if(ptr[0] == '\0') break;
	cmd->args[i].type = cmd_def->args[i].type;
	switch(cmd_def->args[i].type) {
	    case MP_CMD_ARG_INT:
		errno = 0;
		cmd->args[i].v.i = atoi(ptr);
		if(errno != 0) {
		    MSG_ERR("Command %s : argument %d isn't an integer\n",cmd_def->name,i+1);
		    ptr = NULL;
		}
	    break;
	    case MP_CMD_ARG_FLOAT:
		errno = 0;
		/* <olo@altkom.com.pl> Use portable C locale for parsing floats: */
#ifdef USE_SETLOCALE
		setlocale(LC_NUMERIC, "C");
#endif
		cmd->args[i].v.f = atof(ptr);
#ifdef USE_SETLOCALE
		setlocale(LC_NUMERIC, "");
#endif
		if(errno != 0) {
		    MSG_ERR("Command %s : argument %d isn't a float\n",cmd_def->name,i+1);
		    ptr = NULL;
		}
		break;
	    case MP_CMD_ARG_STRING: {
		char term;
		char* ptr2 = ptr, *start;
		if(ptr[0] == '\'' || ptr[0] == '"') {
		    term = ptr[0];
		    ptr2++;
		} else
		term = ' ';
		start = ptr2;
		while(1) {
		    e = strchr(ptr2,term);
		    if(!e) break;
		    if(e <= ptr2 || *(e - 1) != '\\') break;
		    ptr2 = e + 1;
		}
		if(term != ' ' && (!e || e[0] == '\0')) {
		    MSG_ERR("Command %s : argument %d is unterminated\n",cmd_def->name,i+1);
		    ptr = NULL;
		    break;
		} else if(!e) e = ptr+strlen(ptr);
		l = e-start;
		cmd->args[i].v.s = (char*)mp_malloc((l+1)*sizeof(char));
		strncpy(cmd->args[i].v.s,start,l);
		cmd->args[i].v.s[l] = '\0';
		ptr2 = start;
		for(e = strchr(ptr2,'\\') ; e ; e = strchr(ptr2,'\\')) {
		    memmove(e,e+1,strlen(e));
		    ptr2 = e + 1;
		}
	    } break;
	    case -1: ptr = NULL;
	    default: MSG_ERR("Unknown argument %d\n",i);
	}
    }
    cmd->nargs = i;
    if(cmd_def->nargs > cmd->nargs) {
	MSG_ERR("Got command '%s' but\n",str);
	MSG_ERR("command %s require at least %d arguments, we found only %d so far\n",cmd_def->name,cmd_def->nargs,cmd->nargs);
	mp_cmd_free(cmd);
	return NULL;
    }
    for( ; i < MP_CMD_MAX_ARGS && cmd_def->args[i].type != -1 ; i++) {
	memcpy(&cmd->args[i],&cmd_def->args[i],sizeof(mp_cmd_arg_t));
	if(cmd_def->args[i].type == MP_CMD_ARG_STRING && cmd_def->args[i].v.s != NULL)
	    cmd->args[i].v.s = mp_strdup(cmd_def->args[i].v.s);
    }
    if(i < MP_CMD_MAX_ARGS) cmd->args[i].type = -1;
    return cmd;
}

static int mp_input_default_key_func(int fd) {
    int r,code=0;
    unsigned int l;
    l = 0;
    while(l < sizeof(int)) {
	r = read(fd,(&code)+l,sizeof(int)-l);
	if(r <= 0) break;
	l +=r;
    }
    return code;
}

#define MP_CMD_MAX_SIZE 256
static int mp_input_read_cmd(mp_input_fd_t* mp_fd, char** ret) {
    char* end;
    (*ret) = NULL;

    // Allocate the buffer if it dont exist
    if(!mp_fd->buffer) {
	mp_fd->buffer = (char*)mp_malloc(MP_CMD_MAX_SIZE*sizeof(char));
	mp_fd->pos = 0;
	mp_fd->size = MP_CMD_MAX_SIZE;
    }

    // Get some data if needed/possible
    while( !(mp_fd->flags & MP_FD_GOT_CMD) && !(mp_fd->flags & MP_FD_EOF) && (mp_fd->size - mp_fd->pos > 1) ) {
	int r = ((mp_cmd_func_t)mp_fd->read_func)(mp_fd->fd,mp_fd->buffer+mp_fd->pos,mp_fd->size - 1 - mp_fd->pos);
	// Error ?
	if(r < 0) {
	    switch(r) {
		case MP_INPUT_ERROR:
		case MP_INPUT_DEAD:
		    MSG_ERR("Error while reading cmd fd %d : %s\n",mp_fd->fd,strerror(errno));
		case MP_INPUT_NOTHING: return r;
	    }
	    // EOF ?
	} else if(r == 0) {
	    mp_fd->flags |= MP_FD_EOF;
	    break;
	}
	mp_fd->pos += r;
	break;
    }
    // Reset the got_cmd flag
    mp_fd->flags &= ~MP_FD_GOT_CMD;
    while(1) {
	int l = 0;
	// Find the cmd end
	mp_fd->buffer[mp_fd->pos] = '\0';
	end = strchr(mp_fd->buffer,'\n');
	// No cmd end ?
	if(!end) {
	    // If buffer is full we must drop all until the next \n
	    if(mp_fd->size - mp_fd->pos <= 1) {
		MSG_ERR("Cmd buffer of fd %d is full : dropping content\n",mp_fd->fd);
		mp_fd->pos = 0;
		mp_fd->flags |= MP_FD_DROP;
	    }
	    break;
	}
	// We alredy have a cmd : set the got_cmd flag
	else if((*ret)) {
	    mp_fd->flags |= MP_FD_GOT_CMD;
	    break;
	}
	l = end - mp_fd->buffer;
	// Not dropping : put the cmd in ret
	if( ! (mp_fd->flags & MP_FD_DROP)) {
	    (*ret) = (char*)mp_malloc((l+1)*sizeof(char));
	    strncpy((*ret),mp_fd->buffer,l);
	    (*ret)[l] = '\0';
	} else { // Remove the dropping flag
	    mp_fd->flags &= ~MP_FD_DROP;
	}
	if( mp_fd->pos - (l+1) > 0) memmove(mp_fd->buffer,end+1,mp_fd->pos-(l+1));
	mp_fd->pos -= l+1;
    }
    if(*ret) return 1;
    else     return MP_INPUT_NOTHING;
}

static int mp_input_default_cmd_func(int fd,char* buf, int l) {
    while(1) {
	int r = read(fd,buf,l);
	// Error ?
	if(r < 0) {
	    if(errno == EINTR) continue;
	    else if(errno == EAGAIN) return MP_INPUT_NOTHING;
	    return MP_INPUT_ERROR;
	    // EOF ?
	}
	return r;
    }
}

void mp_input_add_cmd_filter(mp_input_cmd_filter func, any_t* ctx) {
    mp_cmd_filter_t* filter = mp_malloc(sizeof(mp_cmd_filter_t))/*, *prev*/;

    filter->filter = func;
    filter->ctx = ctx;
    filter->next = cmd_filters;
    cmd_filters = filter;
}


static char* mp_input_find_bind_for_key(const mp_cmd_bind_t* binds, int n,int* keys) {
    int j;

    for(j = 0; binds[j].cmd != NULL; j++) {
	if(n > 0) {
	    int found = 1,s;
	    for(s = 0; s < n && binds[j].input[s] != 0; s++) {
		if(binds[j].input[s] != keys[s]) {
		    found = 0;
		    break;
		}
	    }
	    if(found && binds[j].input[s] == 0 && s == n) break;
	    else continue;
	} else if(n == 1){
	    if(binds[j].input[0] == keys[0] && binds[j].input[1] == 0) break;
	}
    }
    return binds[j].cmd;
}

static mp_cmd_t* mp_input_get_cmd_from_keys(int n,int* keys, int paused) {
    char* cmd = NULL;
    mp_cmd_t* ret;

    if(cmd_binds)   cmd = mp_input_find_bind_for_key(cmd_binds,n,keys);
    if(cmd == NULL) cmd = mp_input_find_bind_for_key(def_cmd_binds,n,keys);
    if(cmd == NULL) {
	MSG_WARN("No bind found for key %s",mp_input_get_key_name(keys[0]));
	if(n > 1) {
	    int s;
	    for(s=1; s < n; s++) MSG_WARN("-%s",mp_input_get_key_name(keys[s]));
	}
	MSG_WARN("                         \n");
	return NULL;
    }
    ret =  mp_input_parse_cmd(cmd);
    if(!ret) {
	MSG_ERR("Invalid command for binded key %s",mp_input_get_key_name(key_down[0]));
	if(  num_key_down > 1) {
	    unsigned int s;
	    for(s=1; s < num_key_down; s++) MSG_ERR("-%s",mp_input_get_key_name(key_down[s]));
	}
	MSG_ERR(" : %s             \n",cmd);
    }
    return ret;
}

int mp_input_read_key_code(int tim) {
#ifndef HAVE_NO_POSIX_SELECT
    fd_set fds;
    struct timeval tv,*time_val;
#endif
    int i,n=0,max_fd = 0;
    static int last_loop = 0;

    if(num_key_fd == 0) return MP_INPUT_NOTHING;

#ifndef HAVE_NO_POSIX_SELECT
    FD_ZERO(&fds);
#endif
    // Remove fd marked as dead and build the fd_set
    // n == number of fd's to be select() checked
    for(i = 0; (unsigned int)i < num_key_fd; i++) {
	if( (key_fds[i].flags & MP_FD_DEAD) ) {
	    mp_input_rm_key_fd(key_fds[i].fd);
	    i--;
	    continue;
	} else if(key_fds[i].flags & MP_FD_NO_SELECT) continue;
	if(key_fds[i].fd > max_fd) max_fd = key_fds[i].fd;
#ifndef HAVE_NO_POSIX_SELECT
	FD_SET(key_fds[i].fd,&fds);
#endif
	n++;
    }

    if(num_key_fd == 0) return MP_INPUT_NOTHING;
#ifndef HAVE_NO_POSIX_SELECT
    // if we have fd's without MP_FD_NO_SELECT flag, call select():
    if(n>0){
	if(tim >= 0 ) {
	    tv.tv_sec=tim/1000;
	    tv.tv_usec = (tim%1000)*1000;
	    time_val = &tv;
	} else time_val = NULL;
	while(1) {
	    if(select(max_fd+1,&fds,NULL,NULL,time_val) < 0) {
		if(errno == EINTR) continue;
		MSG_ERR("Select error : %s\n",strerror(errno));
	    }
	    break;
	}
    }
#endif
    for(i = last_loop + 1 ; i != last_loop ; i++) {
	int code = -1;
	// This is to check all fd in turn
	if((unsigned int)i >= num_key_fd) {
	    i = -1;
	    last_loop++;
	    last_loop %= (num_key_fd+1);
	    continue;
	}
#ifndef HAVE_NO_POSIX_SELECT
	// No input from this fd
	if(! (key_fds[i].flags & MP_FD_NO_SELECT) && ! FD_ISSET(key_fds[i].fd,&fds))
	    continue;
#endif
	if(key_fds[i].fd == 0) { // stdin is handled by getch2
	    code = getch2(tim);
	    if(code < 0) code = MP_INPUT_NOTHING;
	} else code = ((mp_key_func_t)key_fds[i].read_func)(key_fds[i].fd);
	if(code >= 0) return code;

	if(code == MP_INPUT_ERROR) MSG_ERR("Error on key input fd %d\n",key_fds[i].fd);
	else if(code == MP_INPUT_DEAD) {
	    MSG_ERR("Dead key input on fd %d\n",key_fds[i].fd);
	    key_fds[i].flags |= MP_FD_DEAD;
	}
    }
    return MP_INPUT_NOTHING;
}

static mp_cmd_t* mp_input_read_keys(int tim,int paused) {
    int code = mp_input_read_key_code(tim);
    unsigned int j;
    mp_cmd_t* ret;

    if(mp_input_key_cb) {
	for( ; code >= 0 ;   code = mp_input_read_key_code(0) ) {
	    if(code & MP_KEY_DOWN) continue;
	    code &= ~(MP_KEY_DOWN|MP_NO_REPEAT_KEY);
	    mp_input_key_cb(code);
	}
	return NULL;
    }

    for( ; code >= 0 ;   code = mp_input_read_key_code(0) ) {
	// key pushed
	if(code & MP_KEY_DOWN) {
	    if(num_key_down > MP_MAX_KEY_DOWN) {
		MSG_ERR("Too much key down at the same time\n");
		continue;
	    }
	    code &= ~MP_KEY_DOWN;
	    // Check if we don't already have this key as pushed
	    for(j = 0; j < num_key_down; j++) {
		if(key_down[j] == code)
		break;
	    }
	    if(j != num_key_down) continue;
	    key_down[num_key_down] = code;
	    num_key_down++;
	    last_key_down = GetTimer();
	    ar_state = 0;
	    continue;
	}
	// key released
	// Check if the key is in the down key, driver which can't send push event
	// send only release event
	for(j = 0; j < num_key_down; j++) {
	    if(key_down[j] == code) break;
	}
	if(j == num_key_down) { // key was not in the down keys : add it
	    if(num_key_down > MP_MAX_KEY_DOWN) {
		MSG_ERR("Too much key down at the same time\n");
		continue;
	    }
	    key_down[num_key_down] = code;
	    num_key_down++;
	    last_key_down = 1;
	}
	// We ignore key from last combination
	ret = last_key_down ? mp_input_get_cmd_from_keys(num_key_down,key_down,paused) : NULL;
	// Remove the key
	if(j+1 < num_key_down) memmove(&key_down[j],&key_down[j+1],(num_key_down-(j+1))*sizeof(int));
	num_key_down--;
	last_key_down = 0;
	ar_state = -1;
	if(ar_cmd) {
	    mp_cmd_free(ar_cmd);
	    ar_cmd = NULL;
	}
	if(ret) return ret;
    }

    // No input : autorepeat ?
    if(ar_rate > 0 && ar_state >=0 && num_key_down > 0 && ! (key_down[num_key_down-1] & MP_NO_REPEAT_KEY)) {
	unsigned int t = GetTimer();
	// First time : wait delay
	if(ar_state == 0 && (t - last_key_down) >= ar_delay*1000) {
	    ar_cmd = mp_input_get_cmd_from_keys(num_key_down,key_down,paused);
	    if(!ar_cmd) {
		ar_state = -1;
		return NULL;
	    }
	    ar_state = 1;
	    last_ar = t;
	    return mp_cmd_clone(ar_cmd);
	    // Then send rate / sec event
	} else if(ar_state == 1 && (t -last_ar) >= 1000000/ar_rate) {
	    last_ar = t;
	    return mp_cmd_clone(ar_cmd);
	}
    }
    return NULL;
}

static mp_cmd_t* mp_input_read_cmds(int tim) {
#ifndef HAVE_NO_POSIX_SELECT
    fd_set fds;
    struct timeval tv,*time_val;
#endif
    int i,n = 0,max_fd = 0,got_cmd = 0;
    mp_cmd_t* ret;
    static int last_loop = 0;

    if(num_cmd_fd == 0) return NULL;

#ifndef HAVE_NO_POSIX_SELECT
    FD_ZERO(&fds);
#endif
    for(i = 0; (unsigned int)i < num_cmd_fd ; i++) {
	if( (cmd_fds[i].flags & MP_FD_DEAD) || (cmd_fds[i].flags & MP_FD_EOF) ) {
	    mp_input_rm_cmd_fd(cmd_fds[i].fd);
	    i--;
	    continue;
	} else if(cmd_fds[i].flags & MP_FD_NO_SELECT) continue;
	if(cmd_fds[i].flags & MP_FD_GOT_CMD) got_cmd = 1;
	if(cmd_fds[i].fd > max_fd) max_fd = cmd_fds[i].fd;
#ifndef HAVE_NO_POSIX_SELECT
	FD_SET(cmd_fds[i].fd,&fds);
#endif
	n++;
    }
    if(num_cmd_fd == 0) return NULL;
#ifndef HAVE_NO_POSIX_SELECT
    if(tim >= 0) {
	tv.tv_sec=tim/1000;
	tv.tv_usec = (tim%1000)*1000;
	time_val = &tv;
    } else time_val = NULL;
    while(n > 0) {
	if((i = select(max_fd+1,&fds,NULL,NULL,time_val)) <= 0) {
	    if(i < 0) {
		if(errno == EINTR) continue;
		MSG_ERR("Select error : %s\n",strerror(errno));
	    }
	    if(!got_cmd) return NULL;
	}
	break;
    }
#endif
    for(i = last_loop + 1; i !=  last_loop ; i++) {
	int r = 0;
	char* cmd;
	if((unsigned int)i >= num_cmd_fd) {
	    i = -1;
	    last_loop++;
	    last_loop %= (num_cmd_fd+1);
	    continue;
	}
#ifndef HAVE_NO_POSIX_SELECT
	if( ! (cmd_fds[i].flags & MP_FD_NO_SELECT) && ! FD_ISSET(cmd_fds[i].fd,&fds) && ! (cmd_fds[i].flags & MP_FD_GOT_CMD) )
	    continue;
#endif
	r = mp_input_read_cmd(&cmd_fds[i],&cmd);
	if(r < 0) {
	    if(r == MP_INPUT_ERROR) MSG_ERR("Error on cmd fd %d\n",cmd_fds[i].fd);
	    else if(r == MP_INPUT_DEAD) cmd_fds[i].flags |= MP_FD_DEAD;
	    continue;
	}
	ret = mp_input_parse_cmd(cmd);
	mp_free(cmd);
	if(!ret) continue;
	last_loop = i;
	return ret;
    }
    last_loop = 0;
    return NULL;
}

int mp_input_queue_cmd(mp_cmd_t* cmd) {
    if(cmd_queue_length  >= CMD_QUEUE_SIZE) return 0;
    cmd_queue[cmd_queue_end] = cmd;
    cmd_queue_end = (cmd_queue_end + 1) % CMD_QUEUE_SIZE;
    cmd_queue_length++;
    return 1;
}

static mp_cmd_t* mp_input_get_queued_cmd(int peek_only) {
    mp_cmd_t* ret;

    if(cmd_queue_length == 0) return NULL;

    ret = cmd_queue[cmd_queue_start];

    if (!peek_only) {
	cmd_queue_length--;
	cmd_queue_start = (cmd_queue_start + 1) % CMD_QUEUE_SIZE;
    }
     return ret;
}

/**
 * \param peek_only when set, the returned command stays in the queue.
 * Do not mp_free the returned cmd whe you set this!
 */
mp_cmd_t* mp_input_get_cmd(int tim, int paused, int peek_only) {
    mp_cmd_t* ret = NULL;
    mp_cmd_filter_t* cf;
    int from_queue;

    while(1) {
	from_queue = 1;
	ret = mp_input_get_queued_cmd(peek_only);
	if(ret) break;
	from_queue = 0;
	ret = mp_input_read_keys(tim,paused);
	if(ret) break;
	ret = mp_input_read_cmds(tim);
	break;
    }

    if(!ret) return NULL;

    for(cf = cmd_filters ; cf ; cf = cf->next) {
	if(cf->filter(ret,paused,cf->ctx)) return NULL;
    }

    if (!from_queue && peek_only) mp_input_queue_cmd(ret);

    return ret;
}

void mp_cmd_free(mp_cmd_t* cmd) {
    int i;
//#ifdef MP_DEBUG
//  assert(cmd != NULL);
//#endif
    if ( !cmd ) return;

    if(cmd->name) mp_free(cmd->name);

    for(i=0; i < MP_CMD_MAX_ARGS && cmd->args[i].type != -1; i++) {
	if(cmd->args[i].type == MP_CMD_ARG_STRING && cmd->args[i].v.s != NULL)
	    mp_free(cmd->args[i].v.s);
    }
    mp_free(cmd);
}

mp_cmd_t* mp_cmd_clone(mp_cmd_t* cmd) {
    mp_cmd_t* ret;
    int i;
#ifdef MP_DEBUG
    assert(cmd != NULL);
#endif

    ret = (mp_cmd_t*)mp_malloc(sizeof(mp_cmd_t));
    memcpy(ret,cmd,sizeof(mp_cmd_t));
    if(cmd->name) ret->name = mp_strdup(cmd->name);
    for(i = 0;  i < MP_CMD_MAX_ARGS && cmd->args[i].type != -1; i++) {
	if(cmd->args[i].type == MP_CMD_ARG_STRING && cmd->args[i].v.s != NULL)
	    ret->args[i].v.s = mp_strdup(cmd->args[i].v.s);
    }
    return ret;
}

static char key_str[12];

static char* mp_input_get_key_name(int key) {
    int i;

    for(i = 0; key_names[i].name != NULL; i++) {
	if(key_names[i].key == key) return key_names[i].name;
    }

    if(isascii(key)) {
	snprintf(key_str,12,"%c",(char)key);
	return key_str;
    }

    // Print the hex key code
    snprintf(key_str,12,"%#-8x",key);
    return key_str;
}

static int mp_input_get_key_from_name(char* name) {
    int i,ret = 0,len = strlen(name);
    if(len == 1) { // Direct key code
	ret = (unsigned char)name[0];
	return ret;
    } else if(len > 2 && strncasecmp("0x",name,2) == 0) return strtol(name,NULL,16);

    for(i = 0; key_names[i].name != NULL; i++) {
	if(strcasecmp(key_names[i].name,name) == 0)
	    return key_names[i].key;
    }
    return -1;
}

static int mp_input_get_input_from_name(char* name,int* keys) {
    char *end,*ptr;
    int n=0;

    ptr = name;
    n = 0;
    for(end = strchr(ptr,'-') ; ptr != NULL ; end = strchr(ptr,'-')) {
	if(end && end[1] != '\0') {
	    if(end[1] == '-') end = &end[1];
	    end[0] = '\0';
	}
	keys[n] = mp_input_get_key_from_name(ptr);
	if(keys[n] < 0) return 0;
	n++;
	if(end && end[1] != '\0' && n < MP_MAX_KEY_DOWN) ptr = &end[1];
	else break;
    }
    keys[n] = 0;
    return 1;
}

void mp_input_bind_keys(int keys[MP_MAX_KEY_DOWN+1], char* cmd) {
    int i = 0,j;
    mp_cmd_bind_t* _bind = NULL;

#ifdef MP_DEBUG
    assert(keys != NULL);
    assert(cmd != NULL);
#endif

    if(cmd_binds) {
	for(i = 0; cmd_binds[i].cmd != NULL ; i++) {
	    for(j = 0 ; cmd_binds[i].input[j] == keys[j]  && keys[j] != 0 ; j++) /* NOTHING */;
	    if(keys[j] == 0 && cmd_binds[i].input[j] == 0 ) {
		_bind = &cmd_binds[i];
		break;
	    }
	}
    }

    if(!_bind) {
	cmd_binds = (mp_cmd_bind_t*)mp_realloc(cmd_binds,(i+2)*sizeof(mp_cmd_bind_t));
	memset(&cmd_binds[i],0,2*sizeof(mp_cmd_bind_t));
	_bind = &cmd_binds[i];
    }
    if(_bind->cmd) mp_free(_bind->cmd);
    _bind->cmd = mp_strdup(cmd);
    memcpy(_bind->input,keys,(MP_MAX_KEY_DOWN+1)*sizeof(int));
}

static void mp_input_free_binds(mp_cmd_bind_t* binds) {
    int i;
    if(!binds) return;
    for(i = 0; binds[i].cmd != NULL; i++) mp_free(binds[i].cmd);
    mp_free(binds);
}

#define BS_MAX 256
#define SPACE_CHAR " \n\r\t"

static int mp_input_parse_config(char *file) {
    int fd;
    int bs = 0,r,eof = 0,comments = 0;
    char *iter,*end;
    char buffer[BS_MAX];
    int n_binds = 0, keys[MP_MAX_KEY_DOWN+1] = { 0 };
    mp_cmd_bind_t* binds = NULL;

    fd = open(file,O_RDONLY);

    if(fd < 0) {
	MSG_ERR("Can't open input config file %s : %s\n",file,strerror(errno));
	return 0;
    }

    MSG_V("Parsing input config file %s\n",file);

    while(1) {
	if(! eof && bs < BS_MAX-1) {
	    if(bs > 0) bs--;
	    r = read(fd,buffer+bs,BS_MAX-1-bs);
	    if(r < 0) {
		if(errno == EINTR) continue;
		MSG_ERR("Error while reading input config file %s : %s\n",file,strerror(errno));
		mp_input_free_binds(binds);
		close(fd);
		return 0;
	    } else if(r == 0) {
		eof = 1;
	    } else {
		bs += r+1;
		buffer[bs-1] = '\0';
	    }
	}
	// Empty buffer : return
	if(bs <= 1) {
	    MSG_INFO("Input config file %s parsed : %d binds\n",file,n_binds);
	    if(binds) cmd_binds = binds;
	    close(fd);
	    return 1;
	}
	iter = buffer;
	if(comments) {
	    for( ; iter[0] != '\0' && iter[0] != '\n' ; iter++)/* NOTHING */;
	    if(iter[0] == '\0') { // Buffer was full of comment
		bs = 0;
		continue;
	    }
	    iter++;
	    r = strlen(iter);
	    if(r) memmove(buffer,iter,r+1);
	    bs = r+1;
	    if(iter[0] != '#') comments = 0;
	    continue;
	}
	// Find the wanted key
	if(keys[0] == 0) {
	    // Jump beginning space
	    for(  ; iter[0] != '\0' && strchr(SPACE_CHAR,iter[0]) != NULL ; iter++)/* NOTHING */;
	    if(iter[0] == '\0') { // Buffer was full of space char
		bs = 0;
		continue;
	    }
	    if(iter[0] == '#') { // Comments
		comments = 1;
		continue;
	    }
	    // Find the end of the key code name
	    for(end = iter; end[0] != '\0' && strchr(SPACE_CHAR,end[0]) == NULL ; end++)/*NOTHING */;
	    if(end[0] == '\0') { // Key name don't fit in the buffer
		if(buffer == iter) {
		    if(eof && (buffer-iter) == bs) MSG_ERR("Unfinished binding %s\n",iter);
		    else MSG_ERR("Buffer is too small for this key name : %s\n",iter);
		    mp_input_free_binds(binds);
		    return 0;
		}
		memmove(buffer,iter,end-iter);
		bs = end-iter;
		continue;
	    }
	    char name[end-iter+1];
	    strncpy(name,iter,end-iter);
	    name[end-iter] = '\0';
	    if(! mp_input_get_input_from_name(name,keys)) {
		MSG_ERR("Unknown key '%s'\n",name);
		mp_input_free_binds(binds);
		close(fd);
		return 0;
	    }
	    if( bs > (end-buffer)) memmove(buffer,end,bs - (end-buffer));
	    bs -= end-buffer;
	    continue;
	} else { // Get the command
	    while(iter[0] == ' ' || iter[0] == '\t') iter++;
	    // Found new line
	    if(iter[0] == '\n' || iter[0] == '\r') {
		int i;
		MSG_ERR("No command found for key %s" ,mp_input_get_key_name(keys[0]));
		for(i = 1; keys[i] != 0 ; i++) MSG_ERR("-%s",mp_input_get_key_name(keys[i]));
		MSG_ERR("\n");
		keys[0] = 0;
		if(iter > buffer) {
		    memmove(buffer,iter,bs- (iter-buffer));
		    bs -= (iter-buffer);
		}
		continue;
	    }
	    for(end = iter ; end[0] != '\n' && end[0] != '\r' && end[0] != '\0' ; end++)/* NOTHING */;
	    if(end[0] == '\0' && ! (eof && ((end+1) - buffer) == bs)) {
		if(iter == buffer) {
		    MSG_ERR("Buffer is too small for command %s\n",buffer);
		    mp_input_free_binds(binds);
		    close(fd);
		    return 0;
		}
		memmove(buffer,iter,end - iter);
		bs = end - iter;
		continue;
	    }
	    char cmd[end-iter+1];
	    strncpy(cmd,iter,end-iter);
	    cmd[end-iter] = '\0';
	    //printf("Set bind %d => %s\n",keys[0],cmd);
	    mp_input_bind_keys(keys,cmd);
	    n_binds++;
	    keys[0] = 0;
	    end++;
	    if(bs > (end-buffer)) memmove(buffer,end,bs-(end-buffer));
	    bs -= (end-buffer);
	    buffer[bs-1] = '\0';
	    continue;
	}
    }
    MSG_ERR("What are we doing here ?\n");
    close(fd);
    return 0;
}

void mp_input_init(void) {
    char* file;

    file = config_file[0] != '/' ? get_path(config_file) : config_file;
    if(!file) return;

    if(! mp_input_parse_config(file)) {
	// Try global conf dir
	file = CONFDIR "/input.conf";
	if(! mp_input_parse_config(file)) MSG_WARN("Falling back on default (hardcoded) input config\n");
    }
#ifdef HAVE_JOYSTICK
    if(use_joystick) {
	int fd = mp_input_joystick_init(js_dev);
	if(fd < 0) MSG_ERR("Can't init input joystick with using: %s\n",js_dev);
	else       mp_input_add_key_fd(fd,1,mp_input_joystick_read,(mp_close_func_t)close);
    }
#endif

#ifdef HAVE_LIRC
    if(use_lirc) {
	int fd = mp_input_lirc_init();
	if(fd > 0) mp_input_add_cmd_fd(fd,0,mp_input_lirc_read,mp_input_lirc_close);
    }
#endif

#ifdef HAVE_LIRCC
    if(use_lircc) {
	int fd = lircc_init("mplayer", NULL);
	if(fd >= 0) mp_input_add_cmd_fd(fd,1,NULL,(mp_close_func_t)lircc_cleanup);
    }
#endif
    if(in_file) {
	struct stat st;
	if(stat(in_file,&st)) MSG_ERR("Can't stat %s: %s\n",in_file,strerror(errno));
	else {
	    in_file_fd = open(in_file,S_ISFIFO(st.st_mode) ? O_RDWR : O_RDONLY);
	    if(in_file_fd >= 0) mp_input_add_cmd_fd(in_file_fd,1,NULL,(mp_close_func_t)close);
	    else MSG_ERR("Can't open %s: %s\n",in_file,strerror(errno));
	}
    }
}

void mp_input_uninit(void) {
    unsigned int i;

    for(i=0; i < num_key_fd; i++) {
	if(key_fds[i].close_func) key_fds[i].close_func(key_fds[i].fd);
    }

    for(i=0; i < num_cmd_fd; i++) {
	if(cmd_fds[i].close_func) cmd_fds[i].close_func(cmd_fds[i].fd);
    }

    if(cmd_binds) {
	unsigned i=0;
	for(i=0;;i++) {
	    if(cmd_binds[i].cmd != NULL) mp_free(cmd_binds[i].cmd);
	    else break;
	}
	mp_free(cmd_binds);
	cmd_binds=NULL;
    }
}

void mp_input_register_options(m_config_t* cfg) {
    m_config_register_options(cfg,mp_input_opts);
}

void mp_input_print_keys(void) {
    unsigned i;
    MSG_INFO("List of available KEYS:\n");
    for(i= 0; key_names[i].name != NULL ; i++) MSG_INFO("%s\n",key_names[i].name);
}

static int mp_input_print_key_list(const config_t* cfg) {
    mp_input_print_keys();
    exit(0);
}

void mp_input_print_binds(void) {
    unsigned i,j;
    MSG_INFO("List of available key bindings:\n");
    for(i=0; def_cmd_binds[i].cmd != NULL ; i++) {
	for(j=0;def_cmd_binds[i].input[j] != 0;j++) {
	    MSG_INFO("  %-20s",mp_input_get_key_name(def_cmd_binds[i].input[j]));
	}
	MSG_INFO(" %s\n",def_cmd_binds[i].cmd);
    }
}

void mp_input_print_cmds(void) {
    const mp_cmd_t *cmd;
    int i,j;
    char* type;

    MSG_INFO("List of available input commands:\n");
    for(i = 0; (cmd = &mp_cmds[i])->name != NULL ; i++) {
	MSG_INFO("  %-20.20s",cmd->name);
	for(j= 0 ; j < MP_CMD_MAX_ARGS && cmd->args[j].type != -1 ; j++) {
	    switch(cmd->args[j].type) {
		case MP_CMD_ARG_INT:
		    type = "Integer";
		    break;
		case MP_CMD_ARG_FLOAT:
		    type = "Float";
		    break;
		case MP_CMD_ARG_STRING:
		    type = "String";
		    break;
		default:
		    type = "??";
	    }
	    if(j+1 > cmd->nargs) MSG_INFO(" [%s]",type);
	    else MSG_INFO(" %s",type);
	}
	MSG_INFO("\n");
    }
}

static int mp_input_print_cmd_list(const config_t* cfg) {
    mp_input_print_cmds();
    exit(0);
}

int mp_input_check_interrupt(int tim) {
    mp_cmd_t* cmd;
    if((cmd = mp_input_get_cmd(tim,0,1)) == NULL) return 0;
    switch(cmd->id) {
	case MP_CMD_QUIT:
	case MP_CMD_SOFT_QUIT:
	case MP_CMD_PLAY_TREE_STEP:
	case MP_CMD_PLAY_TREE_UP_STEP:
	case MP_CMD_PLAY_ALT_SRC_STEP:
	    // The cmd will be executed when we are back in the main loop
	    return 1; //<-- memory leaks here 
    }
    // remove the cmd from the queue
    cmd = mp_input_get_cmd(tim,0,0);
    mp_cmd_free(cmd);
    return 0;
}
