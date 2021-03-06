#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace	usr;
#include <iostream>
#include <fstream>

#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>
#include <limits.h>

#include "mpxp_help.h"

#include "libmpconf/m_struct.h"
#include "libmpconf/m_option.h"
#include "libmpconf/cfgparser.h"

#include "libvo2/img_format.h"
#include "xmpcore/xmp_image.h"

#include "menu.h"
#include "menu_list.h"
#include "input2/input.h"
#include "osdep/keycodes.h"
#include "pp_msg.h"

static const char* MENU_KEEP_PATH="/tmp/mpxp_current_path";

int file_filter=0;
int menu_keepdir = 0;
char *menu_chroot = NULL;

struct list_entry_s {
  struct list_entry p;
  int d;
};

struct menu_priv_s {
  menu_list_priv_t p;
  char* dir; // current dir
  /// Cfg fields
  char* path;
  const char* title;
  const char* file_action;
  const char* dir_action;
  int auto_close;
  char** actions;
  const char* filter;
};

static struct menu_priv_s cfg_dflt = {
  MENU_LIST_PRIV_DFLT,
  NULL,

  NULL,
  "Select a file: %p",
  "loadfile '%p'",
  NULL,
  0,
  NULL,
  NULL
};

#define ST_OFF(m) M_ST_OFF(struct menu_priv_s,m)

static m_option_t cfg_fields[] = {
  MENU_LIST_PRIV_FIELDS,
  { "path", ST_OFF(path),  MCONF_TYPE_STRING, 0, 0, 0, NULL },
  { "title", ST_OFF(title),  MCONF_TYPE_STRING, 0, 0, 0, NULL },
  { "file-action", ST_OFF(file_action),  MCONF_TYPE_STRING, 0, 0, 0, NULL },
  { "dir-action", ST_OFF(dir_action),  MCONF_TYPE_STRING, 0, 0, 0, NULL },
  { "auto-close", ST_OFF(auto_close), MCONF_TYPE_FLAG, 0, 0, 1, NULL },
//  { "actions", ST_OFF(actions), MCONF_TYPE_STRING_LIST, 0, 0, 0, NULL},
  { "filter", ST_OFF(filter), MCONF_TYPE_STRING, 0, 0, 0, NULL},
  { NULL, NULL, NULL, 0,0,0,NULL }
};

#define mpriv (menu->priv)

static void free_entry(list_entry_t* entry) {
  delete entry;
}

static const char* replace_path(const std::string& _title,const std::string& _dir) {
  const char* title=_title.c_str();
  const char* dir=_dir.c_str();
  const char* p = strstr(title,"%p");
  if(p) {
    int tl = strlen(title);
    int dl = strlen(dir);
    int t1l = p-title;
    int l = tl - 2 + dl;
    char *r, *n;
    const char *d = dir;
    char term = *(p-1);

    do {
      if (*d == '\\' || *d == term)
	l++;
    } while (*d++);
    r = new char [l + 1];
    n = r + t1l;
    memcpy(r,title,t1l);
    do {
      if (*dir == '\\' || *dir == term)
	*n++ = '\\';
    } while ((*n++ = *dir++));
    if(tl - t1l - 2 > 0)
      strcpy(n-1,p+2);
    return r;
  } else
    return title;
}

typedef int (*kill_warn)(const any_t*, const any_t*);

static int mylstat(const char *dir,const char *file,struct stat* st) {
  int l = strlen(dir) + strlen(file);
  char s[l+2];
  sprintf(s,"%s/%s",dir,file);
  return stat(s,st);
}

static int compare(char **a, char **b){
  if((*a)[strlen(*a) - 1] == '/') {
    if((*b)[strlen(*b) - 1] == '/')
      return strcmp(*b, *a) ;
    else
      return 1;
  } else {
    if((*b)[strlen(*b) - 1] == '/')
      return -1;
    else
      return strcmp(*b, *a);
  }
}

static char **get_extensions(menu_t *menu){
  char **extensions, ext[32];
  std::ifstream fp;
  int n = 1;

  if (!mpriv->filter)
    return NULL;

  fp.open(mpriv->filter,std::ios_base::in);
  if(!fp.is_open())  return NULL;

  extensions = (char **) mp_malloc(sizeof(*extensions));
  *extensions = NULL;

  while(!fp.eof()) {
    fp.getline(ext,sizeof(ext));
    char **l, *e;
    int s = strlen (ext);

    if(ext[s-1] == '\n') {
      ext[s-1] = '\0';
      s--;
    }
    e = new char[s+1];
    extensions = (char **) mp_realloc(extensions, ++n * sizeof(*extensions));
    extensions = (char **) mp_realloc(extensions, ++n * sizeof(*extensions));
    strcpy (e, ext);
    for (l=extensions; *l; l++);
    *l++ = e;
    *l = NULL;
  }

  fp.close();
  return extensions;
}

static void free_extensions(char **extensions){
  if (extensions) {
    char **l = extensions;
    while (*l)
      delete *l++;
    delete extensions;
  }
}

static int open_dir(menu_t* menu,const char* args) {
  char **namelist, **tp;
  struct dirent *dp;
  struct stat st;
  int n;
  int path_fp;
  list_entry_t* e;
  DIR* dirp;
  extern int file_filter;
  char **extensions, **elem, *ext;

  menu_list_init(menu);

  if(mpriv->dir)
    delete mpriv->dir;
  mpriv->dir = mp_strdup(args);
  if(mpriv->p.title && mpriv->p.title != mpriv->title && mpriv->p.title != cfg_dflt.p.title)
    delete mpriv->p.title;

  mpriv->p.title = replace_path(mpriv->title,mpriv->dir);

  if ((dirp = opendir (mpriv->dir)) == NULL){
    mpxp_err<<"[libmenu] Opendir error: "<<strerror(errno)<<std::endl;
    return 0;
  }

  if (menu_keepdir) {
    path_fp = open (MENU_KEEP_PATH, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (path_fp >= 0) {
      write (path_fp, mpriv->dir, strlen (mpriv->dir));
      close (path_fp);
    }
  }

  namelist = new char*;
  extensions = get_extensions(menu);

  n=0;
  while ((dp = readdir(dirp)) != NULL) {
    if(dp->d_name[0] == '.' && strcmp(dp->d_name,"..") != 0)
      continue;
    if (menu_chroot && !strcmp (dp->d_name,"..")) {
      unsigned len = strlen (menu_chroot);
      if ((strlen (mpriv->dir) == len || strlen (mpriv->dir) == len + 1)
	  && !strncmp (mpriv->dir, menu_chroot, len))
	continue;
    }
    mylstat(args,dp->d_name,&st);
    if (file_filter && extensions && !S_ISDIR(st.st_mode)) {
      if((ext = strrchr(dp->d_name,'.')) == NULL)
	continue;
      ext++;
      elem = extensions;
      do {
	if (!strcasecmp(ext, *elem))
	  break;
      } while (*++elem);
      if (*elem == NULL)
	continue;
    }
    if(n%20 == 0){ // Get some more mem
      if((tp = (char **) mp_realloc(namelist, (n+20) * sizeof (char *)))
	 == NULL) {
	mpxp_err<<"[libmenu] Realloc error: "<<strerror(errno)<<std::endl;
	n--;
	goto bailout;
      }
      namelist=tp;
    }

    namelist[n] = new char[strlen(dp->d_name) + 2];

    strcpy(namelist[n], dp->d_name);
    if(S_ISDIR(st.st_mode))
      strcat(namelist[n], "/");
    n++;
  }

bailout:
  free_extensions (extensions);
  closedir(dirp);

  qsort(namelist, n, sizeof(char *), (kill_warn)compare);

  if (n < 0) {
    mpxp_err<<"[libmenu] Readdir error: "<<strerror(errno)<<std::endl;
    return 0;
  }
  while(n--) {
    if((e = new(zeromem) list_entry_t) != NULL){
    e->p.next = NULL;
    e->p.txt = mp_strdup(namelist[n]);
    if(strchr(namelist[n], '/') != NULL)
      e->d = 1;
    menu_list_add_entry(menu,e);
    }else{
      mpxp_err<<"[libmenu] Malloc error: "<<strerror(errno)<<std::endl;
    }
    delete namelist[n];
  }
  delete namelist;

  return 1;
}


static char *action;

static void read_cmd(menu_t* menu,int cmd) {
  mp_cmd_t* c = NULL;
  switch(cmd) {
  case MENU_CMD_LEFT:
    mpriv->p.current = mpriv->p.menu; // Hack : we consider that the first entry is ../
  case MENU_CMD_RIGHT:
  case MENU_CMD_OK: {
    // Directory
    if(mpriv->p.current->d) {
      if(mpriv->dir_action) {
	std::string filename;
	const char* str;
	filename=mpriv->dir+mpriv->p.current->p.txt;
	str = replace_path(mpriv->dir_action,filename);
	c = mp_input_parse_cmd(str);
	if(str != mpriv->dir_action)
	  delete str;
      } else { // Default action : open this dirctory ourself
	int l = strlen(mpriv->dir);
	char *slash =  NULL, *p = NULL;
	if(mpriv->p.current->p.txt=="../") {
	  if(l <= 1) break;
	  mpriv->dir[l-1] = '\0';
	  slash = strrchr(mpriv->dir,'/');
#if defined(__MINGW32__) || defined(__CYGWIN__)
	  if (!slash)
	    slash = strrchr(mpriv->dir,'\\');
#endif
	  if(!slash) break;
	  slash[1] = '\0';
	  p = mp_strdup(mpriv->dir);
	} else {
	  p = new char [l + mpriv->p.current->p.txt.length() + 1];
	  sprintf(p,"%s%s",mpriv->dir,mpriv->p.current->p.txt.c_str());
	}
	menu_list_uninit(menu,free_entry);
	if(!open_dir(menu,p)) {
	  mpxp_err<<"[libmenu] Can't open directory: "<<p<<std::endl;
	  menu->cl = 1;
	}
	delete p;
      }
    } else { // Files
      std::string filename;
      const char *str;
      filename=mpriv->dir+mpriv->p.current->p.txt;
      str = replace_path(mpriv->file_action,filename);
      c = mp_input_parse_cmd(str);
      if(str != mpriv->file_action)
	delete str;
    }
    if(c) {
      mp_input_queue_cmd(menu->libinput,c);
      if(mpriv->auto_close)
	menu->cl = 1;
    }
  } break;
  case MENU_CMD_ACTION: {
    std::string filename;
    const char *str;
    filename=mpriv->dir+mpriv->p.current->p.txt;
    str = replace_path(action, filename);
    mp_input_queue_cmd(menu->libinput,mp_input_parse_cmd(str));
    if(str != action)
      delete str;
  } break;
  default:
    menu_list_read_cmd(menu,cmd);
  }
}

static void read_key(menu_t* menu,int c){
  if(c == KEY_BS)
    read_cmd(menu,MENU_CMD_LEFT);
  else {
    char **str;
    for (str=mpriv->actions; str && *str; str++)
      if (c == (*str)[0]) {
	action = &(*str)[2];
	read_cmd(menu,MENU_CMD_ACTION);
	break;
      }
    if (!str || !*str)
      menu_list_read_key(menu,c,1);
  }
}

static void clos(menu_t* menu) {
  menu_list_uninit(menu,free_entry);
  delete mpriv->dir;
}

static int open_fs(menu_t* menu,const char* args) {
  char *path = mpriv->path, *freepath = NULL;
  int r = 0;
  char wd[PATH_MAX+1];
  args = NULL; // Warning kill

  menu->draw = menu_list_draw;
  menu->read_cmd = read_cmd;
  menu->read_key = read_key;
  menu->close = clos;

  if (menu_keepdir) {
    if (!path || path[0] == '\0') {
      struct stat st;
      int path_fp;

      path_fp = open (MENU_KEEP_PATH, O_RDONLY);
      if (path_fp >= 0) {
	if (!fstat (path_fp, &st) && (st.st_size > 0)) {
	  path = new char [st.st_size+1];
	  if ((read(path_fp, path, st.st_size) == st.st_size) && path[0] != '\0'){
	    freepath = path;
	    path[st.st_size] = '\0';
	  }
	  else {
	    delete path;
	    path = NULL;
	  }
	}
	close (path_fp);
      }
    }
  }

  getcwd(wd,PATH_MAX);
  if(!path || path[0] == '\0') {
    int l = strlen(wd) + 2;
    char b[l];
    sprintf(b,"%s/",wd);
    r = open_dir(menu,b);
  } else if(path[0] != '/') {
    int al = strlen(path);
    int l = strlen(wd) + al + 3;
    char b[l];
    if(b[al-1] != '/')
      sprintf(b,"%s/%s/",wd,path);
    else
      sprintf(b,"%s/%s",wd,path);
    r = open_dir(menu,b);
  } else
    r = open_dir(menu,path);

  if (freepath)
    delete freepath;

  return r;
}

static struct m_struct_t m_priv =
{
    "fs_cfg",
    sizeof(struct menu_priv_s),
    &cfg_dflt,
    cfg_fields
};

extern const menu_info_t menu_info_filesel = {
  "File seletor menu",
  "filesel",
  "Albeu",
  "",
  &m_priv,
  open_fs
};
