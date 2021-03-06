#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace	usr;

/// \file
/// \ingroup Options

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <unistd.h>

#include "cfgparser.h"
#include "m_option.h"
#include "libmpstream2/url.h"
#include "parser_msg.h"

// Don't mp_free for 'production' atm
#ifndef MP_DEBUG
//#define NO_FREE
#endif

const m_option_t* m_option_list_find(const m_option_t* list,const char* name) {
  int i;

  for(i = 0 ; list[i].name ; i++) {
    int l = strlen(list[i].name) - 1;
    if((list[i].type->flags & M_OPT_TYPE_ALLOW_WILDCARD) &&
       (l > 0) && (list[i].name[l] == '*')) {
      if(strncasecmp(list[i].name,name,l) == 0)
	return &list[i];
    } else if(strcasecmp(list[i].name,name) == 0)
      return &list[i];
  }
  return NULL;
}

// Default function that just does a memcpy

static void copy_opt(const m_option_t* opt,any_t* dst,const any_t* src) {
  if(dst && src)
    memcpy(dst,src,opt->type->size);
}

// Helper for the print funcs (from man printf)
static char* dup_printf(const char *fmt, ...) {
  /* Guess we need no more than 50 bytes. */
  int n, size = 50;
  char *p;
  va_list ap;
  if ((p = new char [size]) == NULL)
    return NULL;
  while (1) {
    /* Try to print in the allocated space. */
    va_start(ap, fmt);
    n = vsnprintf (p, size, fmt, ap);
    va_end(ap);
    /* If that worked, return the string. */
    if (n > -1 && n < size)
      return p;
    /* Else try again with more space. */
    if (n > -1)    /* glibc 2.1 */
      size = n+1; /* precisely what is needed */
    else           /* glibc 2.0 */
      size *= 2;  /* twice the old size */
    if ((p = (char*)mp_realloc (p, size)) == NULL)
      return NULL;
  }
}


// Flag

#define VAL(x) (*(int*)(x))

static int parse_flag(const m_option_t* opt,const char *name,const char *param, any_t* dst, int src) {
  if (src == M_CONFIG_FILE) {
    if(!param) return M_OPT_MISSING_PARAM;
    if (!strcasecmp(param, "yes") ||	/* any other language? */
	!strcasecmp(param, "on") ||
	!strcasecmp(param, "ja") ||
	!strcasecmp(param, "si") ||
	!strcasecmp(param, "igen") ||
	!strcasecmp(param, "y") ||
	!strcasecmp(param, "j") ||
	!strcasecmp(param, "i") ||
	!strcasecmp(param, "tak") ||
	!strcasecmp(param, "ja") ||
	!strcasecmp(param, "true") ||
	!strcmp(param, "1")) {
      if(dst) VAL(dst) = opt->max;
    } else if (!strcasecmp(param, "no") ||
	       !strcasecmp(param, "off") ||
	       !strcasecmp(param, "nein") ||
	       !strcasecmp(param, "nicht") ||
	       !strcasecmp(param, "nem") ||
	       !strcasecmp(param, "n") ||
	       !strcasecmp(param, "nie") ||
	       !strcasecmp(param, "nej") ||
	       !strcasecmp(param, "false") ||
	       !strcmp(param, "0")) {
      if(dst) VAL(dst) = opt->min;
    } else {
      MSG_ERR("Invalid parameter for %s flag: %s\n",name, param);
      return M_OPT_INVALID;
    }
    return 1;
  } else {
    if(dst) VAL(dst) = opt->max;
    return 0;
  }
}

static char* print_flag(const m_option_t* opt,const any_t* val) {
  if(VAL(val) == opt->min)
    return mp_strdup("no");
  else
    return mp_strdup("yes");
}

extern const m_option_type_t m_option_type_flag = {
  "Flag",
  "need yes or no in config files",
  sizeof(int),
  0,
  parse_flag,
  print_flag,
  copy_opt,
  copy_opt,
  NULL,
  NULL
};

// Integer

static int parse_int(const m_option_t* opt,const char *name,const char *param, any_t* dst, int src) {
  long tmp_int;
  char *endptr;
  src = 0;

  if (param == NULL)
    return M_OPT_MISSING_PARAM;

  tmp_int = strtol(param, &endptr, 10);
  if (*endptr)
  tmp_int = strtol(param, &endptr, 0);
  if (*endptr) {
    MSG_ERR("The %s option must be an integer: %s\n",name, param);
    return M_OPT_INVALID;
  }

  if ((opt->flags & M_OPT_MIN) && (tmp_int < opt->min)) {
    MSG_ERR("The %s option must be >= %d: %s\n", name, (int) opt->min, param);
    return M_OPT_OUT_OF_RANGE;
  }

  if ((opt->flags & M_OPT_MAX) && (tmp_int > opt->max)) {
    MSG_ERR("The %s option must be <= %d: %s\n",name, (int) opt->max, param);
    return M_OPT_OUT_OF_RANGE;
  }

  if(dst) VAL(dst) = tmp_int;

  return 1;
}

static char* print_int(const m_option_t* opt,const any_t* val) {
  opt = NULL;
  return dup_printf("%d",VAL(val));
}

extern const m_option_type_t m_option_type_int = {
  "Integer",
  "",
  sizeof(int),
  0,
  parse_int,
  print_int,
  copy_opt,
  copy_opt,
  NULL,
  NULL
};

// Float

#undef VAL
#define VAL(x) (*(double*)(x))

static int parse_double(const m_option_t* opt,const char *name,const char *param, any_t* dst, int src) {
  double tmp_float;
  char* endptr;
  src = 0;

  if (param == NULL)
    return M_OPT_MISSING_PARAM;

  tmp_float = strtod(param, &endptr);

  switch(*endptr) {
  case ':':
  case '/':
    tmp_float /= strtod(endptr+1, &endptr);
    break;
  case '.':
  case ',':
    /* we also handle floats specified with
     * non-locale decimal point ::atmos
     */
    if(tmp_float<0)
      tmp_float -= 1.0/pow(10,strlen(endptr+1)) * strtod(endptr+1, &endptr);
    else
      tmp_float += 1.0/pow(10,strlen(endptr+1)) * strtod(endptr+1, &endptr);
    break;
  }

  if (*endptr) {
    MSG_ERR("The %s option must be a floating point "
	   "number or a ratio (numerator[:/]denominator): %s\n",name, param);
    return M_OPT_INVALID;
  }

  if (opt->flags & M_OPT_MIN)
    if (tmp_float < opt->min) {
      MSG_ERR("The %s option must be >= %f: %s\n", name, opt->min, param);
      return M_OPT_OUT_OF_RANGE;
    }

  if (opt->flags & M_OPT_MAX)
    if (tmp_float > opt->max) {
      MSG_ERR("The %s option must be <= %f: %s\n", name, opt->max, param);
      return M_OPT_OUT_OF_RANGE;
    }

  if(dst) VAL(dst) = tmp_float;
  return 1;
}

static char* print_double(const m_option_t* opt,const any_t* val) {
  opt = NULL;
  return dup_printf("%f",VAL(val));
}

extern const m_option_type_t m_option_type_double = {
  "Double",
  "double precission floating point number or ratio (numerator[:/]denominator)",
  sizeof(double),
  0,
  parse_double,
  print_double,
  copy_opt,
  copy_opt,
  NULL,
  NULL
};

#undef VAL
#define VAL(x) (*(float*)(x))

static int parse_float(const m_option_t* opt,const char *name,const char *param, any_t* dst, int src) {
    double tmp;
    int r= parse_double(opt, name, param, &tmp, src);
    if(r==1 && dst) VAL(dst) = tmp;
    return r;
}

static char* print_float(const m_option_t* opt,const any_t* val) {
  opt = NULL;
  return dup_printf("%f",VAL(val));
}

extern const m_option_type_t m_option_type_float = {
  "Float",
  "floating point number or ratio (numerator[:/]denominator)",
  sizeof(float),
  0,
  parse_float,
  print_float,
  copy_opt,
  copy_opt,
  NULL,
  NULL
};

///////////// Position
#undef VAL
#define VAL(x) (*(off_t*)(x))

static int parse_position(const m_option_t* opt,const char *name,const char *param, any_t* dst, int src) {
    UNUSED(src);
  off_t tmp_off;
  char dummy;

  if (param == NULL)
    return M_OPT_MISSING_PARAM;
  if (sscanf(param, "%d%c", &tmp_off, &dummy) != 1) {
    MSG_ERR("The %s option must be an integer: %s\n",opt->name,param);
    return M_OPT_INVALID;
  }

  if (opt->flags & M_OPT_MIN)
    if (tmp_off < opt->min) {
      MSG_ERR("The %s option must be >= %d: %s\n",
	     name, (int64_t) opt->min, param);
      return M_OPT_OUT_OF_RANGE;
    }

  if (opt->flags & M_OPT_MAX)
    if (tmp_off > opt->max) {
      MSG_ERR("The %s option must be <= %d: %s\n",
	     name, (int64_t) opt->max, param);
      return M_OPT_OUT_OF_RANGE;
    }

  if(dst)
    VAL(dst) = tmp_off;
  return 1;
}

static char* print_position(const m_option_t* opt,const any_t* val) {
  UNUSED(opt);
  return dup_printf("%d",(int64_t)VAL(val));
}

extern const m_option_type_t m_option_type_position = {
  "Position",
  "Integer (off_t)",
  sizeof(off_t),
  0,
  parse_position,
  print_position,
  copy_opt,
  copy_opt,
  NULL,
  NULL
};


///////////// String

#undef VAL
#define VAL(x) (*(char**)(x))

static int parse_str(const m_option_t* opt,const char *name,const char *param, any_t* dst, int src) {
  UNUSED(src);
  UNUSED(name);
  UNUSED(opt);

  if (param == NULL)
      return M_OPT_MISSING_PARAM;

  if ((opt->flags & M_OPT_MIN) && (strlen(param) < opt->min)) {
    MSG_ERR("Parameter must be >= %d chars: %s\n",
	   (int) opt->min, param);
    return M_OPT_OUT_OF_RANGE;
  }

  if ((opt->flags & M_OPT_MAX) && (strlen(param) > opt->max)) {
    MSG_ERR("Parameter must be <= %d chars: %s\n",
	   (int) opt->max, param);
    return M_OPT_OUT_OF_RANGE;
  }

  if(dst) {
    if(VAL(dst))
      delete VAL(dst);
    VAL(dst) = mp_strdup(param);
  }

  return 1;

}

static char* print_str(const m_option_t* opt,const any_t* val) {
  UNUSED(opt);
  UNUSED(val);
  return (val && VAL(val) && strlen(VAL(val)) > 0) ? mp_strdup(VAL(val)) : NULL;
}

static void copy_str(const m_option_t* opt,any_t* dst,const any_t* src) {
  UNUSED(opt);
  if(dst && src) {
#ifndef NO_FREE
    if(VAL(dst)) delete VAL(dst); //FIXME!!!
#endif
    VAL(dst) = VAL(src) ? mp_strdup(VAL(src)) : NULL;
  }
}

static void free_str(const any_t* src) {
  if(src && VAL(src)){
#ifndef NO_FREE
    delete VAL(src); //FIXME!!!
#endif
    VAL(src) = NULL;
  }
}

extern const m_option_type_t m_option_type_string = {
  "String",
  "",
  sizeof(char*),
  M_OPT_TYPE_DYNAMIC,
  parse_str,
  print_str,
  copy_str,
  copy_str,
  copy_str,
  free_str
};

//////////// String list

#define LIST_SEPARATOR ','
#undef VAL
#define VAL(x) (*(char***)(x))

#define OP_NONE 0
#define OP_ADD 1
#define OP_PRE 2
#define OP_DEL 3
#define OP_CLR 4

static void free_str_list(const any_t* dst) {
  char** d;
  int i;

  if(!dst || !VAL(dst)) return;
  d = VAL(dst);

// FIXME!!!
#ifndef NO_FREE
  for(i = 0 ; d[i] != NULL ; i++)
    delete d[i];
  delete d;
#endif
  VAL(dst) = NULL;
}

static int str_list_add(const char** add, int n,any_t* dst,int pre) {
  char** lst = VAL(dst);
  int ln;

  if(!dst) return M_OPT_PARSER_ERR;
  lst = VAL(dst);

  for(ln = 0 ; lst && lst[ln] ; ln++)
    /**/;

  lst = (char**)mp_realloc(lst,(n+ln+1)*sizeof(char*));

  if(pre) {
    memmove(&lst[n],lst,(ln+1)*sizeof(char*));
    memcpy(lst,add,n*sizeof(char*));
  } else
    memcpy(&lst[ln],add,(n+1)*sizeof(char*));

  delete add;

  VAL(dst) = lst;

  return 1;
}

static int str_list_del(char** del, int n,const any_t* dst) {
  char **lst,*ep,**d;
  int i,ln,s;
  long idx;

  if(!dst) return M_OPT_PARSER_ERR;
  lst = VAL(dst);

  for(ln = 0 ; lst && lst[ln] ; ln++)
    /**/;
  s = ln;

  for(i = 0 ; del[i] != NULL ; i++) {
    idx = strtol(del[i], &ep, 0);
    if(*ep) {
      MSG_ERR("Invalid index: %s\n",del[i]);
      delete del[i];
      continue;
    }
    delete del[i];
    if(idx < 0 || idx >= ln) {
      MSG_ERR("Index %ld is out of range.\n",idx);
      continue;
    } else if(!lst[idx])
      continue;
    delete lst[idx];
    lst[idx] = NULL;
    s--;
  }
  delete del;

  if(s == 0) {
    if(lst) delete lst;
    VAL(dst) = NULL;
    return 1;
  }

  d = new(zeromem) char*[s+1];
  for(i = 0, n = 0 ; i < ln ; i++) {
    if(!lst[i]) continue;
    d[n] = lst[i];
    n++;
  }
  d[s] = NULL;

  if(lst) delete lst;
  VAL(dst) = d;

  return 1;
}

static const char *get_nextsep(const char *ptr, char sep) {
    const char *last_ptr = ptr;
    for(;;){
	ptr = strchr(ptr, sep);
	if(ptr && ptr>last_ptr && ptr[-1]=='\\'){
	    ptr++;
	}else
	    break;
    }
    return ptr;
}

static char *get_nextsep(char *ptr, char sep, int modify) {
    const char *last_ptr = ptr;
    for(;;){
	ptr = strchr(ptr, sep);
	if(ptr && ptr>last_ptr && ptr[-1]=='\\'){
	    if (modify) memmove(ptr-1, ptr, strlen(ptr)+1);
	    else ptr++;
	}else
	    break;
    }
    return ptr;
}

static int parse_str_list(const m_option_t* opt,const char *name,const char *param, any_t* dst, int src) {
  int n = 0,len = strlen(opt->name);
  char *str;
  const char *ptr = param;
  char *last_ptr, **res;
  int op = OP_NONE;
  UNUSED(src);

  if(opt->name[len-1] == '*' && ((int)strlen(name) > len - 1)) {
    const char* ns = &name[len-1];
    if(strcasecmp(ns,"-add") == 0)
      op = OP_ADD;
    else if(strcasecmp(ns,"-pre") == 0)
      op = OP_PRE;
    else if(strcasecmp(ns,"-del") == 0)
      op = OP_DEL;
    else if(strcasecmp(ns,"-clr") == 0)
      op = OP_CLR;
    else
      return M_OPT_UNKNOWN;
  }

  // Clear the list ??
  if(op == OP_CLR) {
    if(dst)
      free_str_list(dst);
    return 0;
  }

  // All other ops need a param
  if (param == NULL || strlen(param) == 0)
      return M_OPT_MISSING_PARAM;


  while(ptr[0] != '\0') {
    ptr = get_nextsep(ptr, LIST_SEPARATOR);
    if(!ptr) {
      n++;
      break;
    }
    ptr++;
    n++;
  }
  if(n == 0)
    return M_OPT_INVALID;
  if( ((opt->flags & M_OPT_MIN) && (n < opt->min)) ||
      ((opt->flags & M_OPT_MAX) && (n > opt->max)) )
    return M_OPT_OUT_OF_RANGE;

  if(!dst) return 1;

  res = new char*[n+2];
  ptr = str = mp_strdup(param);
  n = 0;

  while(1) {
    last_ptr = const_cast<char*>(ptr);
    ptr = get_nextsep(last_ptr, LIST_SEPARATOR, 1);
    if(!ptr) {
      res[n] = mp_strdup(last_ptr);
      n++;
      break;
    }
    len = ptr - last_ptr;
    res[n] = new char [len + 1];
    if(len) strncpy(res[n],last_ptr,len);
    res[n][len] = '\0';
    ptr++;
    n++;
  }
  res[n] = NULL;
  delete str;

  switch(op) {
  case OP_ADD:
    return str_list_add(const_cast<const char**>(res),n,dst,0);
  case OP_PRE:
    return str_list_add(const_cast<const char**>(res),n,dst,1);
  case OP_DEL:
    return str_list_del(res,n,dst);
  }

  if(VAL(dst))
    free_str_list(dst);
  VAL(dst) = res;

  return 1;
}

static void copy_str_list(const m_option_t* opt,any_t* dst,const any_t* src) {
  int n;
  char **d,**s;
  UNUSED(opt);

  if(!(dst && src)) return;
  s = VAL(src);

  if(VAL(dst))
    free_str_list(dst);

  if(!s) {
    VAL(dst) = NULL;
    return;
  }

  for(n = 0 ; s[n] != NULL ; n++)
    /* NOTHING */;
  d = new char*[n+1];
  for( ; n >= 0 ; n--)
    d[n] = s[n] ? mp_strdup(s[n]) : NULL;

  VAL(dst) = d;
}

static char* print_str_list(const m_option_t* opt,const any_t* src) {
  char **lst = NULL;
  char *ret = NULL,*last = NULL;
  int i;
  UNUSED(opt);

  if(!(src && VAL(src))) return NULL;
  lst = VAL(src);

  for(i = 0 ; lst[i] ; i++) {
    if(last) {
      ret = dup_printf("%s,%s",last,lst[i]);
      delete last;
    } else
      ret = mp_strdup(lst[i]);
    last = ret;
  }
  if(last && last != ret) delete last;
  return ret;
}

extern const m_option_type_t m_option_type_string_list = {
  "String list",
  "A list of strings separated by ','\n"
  "Option with a name ending in an * permits using the following suffix: \n"
  "\t-add: Add the given parameters at the end of the list.\n"
  "\t-pre: Add the given parameters at the begining of the list.\n"
  "\t-del: Remove the entry at the given indices.\n"
  "\t-clr: Clear the list.\n"
  "e.g: -vf-add flip,mirror -vf-del 2,5\n",
  sizeof(char**),
  M_OPT_TYPE_DYNAMIC | M_OPT_TYPE_ALLOW_WILDCARD,
  parse_str_list,
  print_str_list,
  copy_str_list,
  copy_str_list,
  copy_str_list,
  free_str_list
};


///////////////////  Func based options

// A chained list to save the various calls for func_param and func_full
typedef struct m_func_save m_func_save_t;
struct m_func_save {
  m_func_save_t* next;
  char* name;
  char* param;
};

#undef VAL
#define VAL(x) (*(m_func_save_t**)(x))

/////////////////// Print

static int parse_print(const m_option_t* opt,const char *name,const char *param, any_t* dst, int src) {
  UNUSED(src);
  UNUSED(dst);
  UNUSED(param);
  UNUSED(name);
  if(opt->type == MCONF_TYPE_PRINT_INDIRECT)
    MSG_INFO("%s", *(char **) opt->p);
  else
    MSG_INFO("%s", (char *) opt->p);

  if(opt->priv == NULL)
    return M_OPT_EXIT;
  return 1;
}

extern const m_option_type_t m_option_type_print = {
  "Print",
  "",
  0,
  0,
  parse_print,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};

extern const m_option_type_t m_option_type_print_indirect = {
  "Print",
  "",
  0,
  0,
  parse_print,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};

const m_option_type_t m_option_type_print_func = {
  "Print",
  "",
  0,
  M_OPT_TYPE_ALLOW_WILDCARD,
  parse_print,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};


/////////////////////// Subconfig
#undef VAL
#define VAL(x) (*(char***)(x))

static int parse_subconf(const m_option_t* opt,const char *name,const char *param, any_t* dst, int src) {
  char *subparam;
  char *subopt;
  int nr = 0,i,r;
  const m_option_t *subopts;
  const char *p;
  char** lst = NULL;

  if (param == NULL || strlen(param) == 0)
    return M_OPT_MISSING_PARAM;

  subparam = new char [strlen(param)+1];
  subopt = new char [strlen(param)+1];
  p = param;

  subopts = const_cast<const m_option_t*>(reinterpret_cast<m_option_t*>(opt->p));

  while(p[0])
    {
      int sscanf_ret = 1;
      int optlen = strcspn(p, ":=");
      /* clear out */
      subopt[0] = subparam[0] = 0;
      strncpy(subopt, p, optlen + 1);
      p = &p[optlen];
      if (p[0] == '=') {
	sscanf_ret = 2;
	p = &p[1];
	if (p[0] == '"') {
	  p = &p[1];
	  optlen = strcspn(p, "\"");
	  strncpy(subparam, p, optlen + 1);
	  p = &p[optlen];
	  if (p[0] != '"') {
	    MSG_ERR("Terminating '\"' missing for '%s'\n", subopt);
	    return M_OPT_INVALID;
	  }
	  p = &p[1];
	} else if (p[0] == '%') {
	  p = &p[1];
	  optlen = (int)strtol(p, (char**)&p, 0);
	  if (!p || p[0] != '%' || (unsigned(optlen) > strlen(p) - 1)) {
	    MSG_ERR("Invalid length %i for '%s'\n", optlen, subopt);
	    return M_OPT_INVALID;
	  }
	  p = &p[1];
	  strncpy(subparam, p, optlen + 1);
	  p = &p[optlen];
	} else {
	  optlen = strcspn(p, ":");
	  strncpy(subparam, p, optlen + 1);
	  p = &p[optlen];
	}
      }
      if (p[0] == ':')
	p = &p[1];
      else if (p[0]) {
	MSG_ERR("Incorrect termination for '%s'\n", subopt);
	return M_OPT_INVALID;
      }

      switch(sscanf_ret)
	{
	case 1:
	  subparam[0] = 0;
	case 2:
	  for(i = 0 ; subopts[i].name ; i++) {
	    if(!strcmp(subopts[i].name,subopt)) break;
	  }
	  if(!subopts[i].name) {
	    MSG_ERR("Option %s: Unknown suboption %s\n",name,subopt);
	    return M_OPT_UNKNOWN;
	  }
	  r = m_option_parse(&subopts[i],subopt,
			     subparam[0] == 0 ? NULL : subparam,NULL,src);
	  if(r < 0) return r;
	  if(dst) {
	    lst = (char**)mp_realloc(lst,2 * (nr+2) * sizeof(char*));
	    lst[2*nr] = mp_strdup(subopt);
	    lst[2*nr+1] = subparam[0] == 0 ? NULL : mp_strdup(subparam);
	    memset(&lst[2*(nr+1)],0,2*sizeof(char*));
	    nr++;
	  }
	  break;
	}
    }

  delete subparam;
  delete subopt;
  if(dst)
    VAL(dst) = lst;

  return 1;
}

extern const m_option_type_t m_option_type_subconfig = {
  "Subconfig",
  "The syntax is -option opt1=foo:flag:opt2=blah",
  sizeof(int),
  M_OPT_TYPE_HAS_CHILD,
  parse_subconf,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};

#include "libvo2/img_format.h"

/* FIXME: snyc with img_format.h */
static struct {
  const char* name;
  unsigned int fmt;
} mp_imgfmt_list[] = {
  {"444p16be", IMGFMT_444P16_BE},
  {"444p16le", IMGFMT_444P16_LE},
  {"444p16", IMGFMT_444P16},
  {"422p16be", IMGFMT_422P16_BE},
  {"422p16le", IMGFMT_422P16_LE},
  {"422p16", IMGFMT_422P16},
  {"420p16be", IMGFMT_420P16_BE},
  {"420p16le", IMGFMT_420P16_LE},
  {"420p16", IMGFMT_420P16},
  {"444p", IMGFMT_444P},
  {"422p", IMGFMT_422P},
  {"411p", IMGFMT_411P},
  {"420a", IMGFMT_420A},
  {"yuy2", IMGFMT_YUY2},
  {"uyvy", IMGFMT_UYVY},
  {"yvu9", IMGFMT_YVU9},
  {"if09", IMGFMT_IF09},
  {"yv12", IMGFMT_YV12},
  {"i420", IMGFMT_I420},
  {"iyuv", IMGFMT_IYUV},
  {"clpl", IMGFMT_CLPL},
  {"hm12", IMGFMT_HM12},
  {"y800", IMGFMT_Y800},
  {"y8", IMGFMT_Y8},
  {"nv12", IMGFMT_NV12},
  {"nv21", IMGFMT_NV21},
  {"bgr48be", IMGFMT_BGR48BE},
  {"bgr48le", IMGFMT_BGR48LE},
  {"bgr48", IMGFMT_BGR48NE},
  {"bgr24", IMGFMT_BGR24},
  {"bgr32", IMGFMT_BGR32},
  {"bgr16", IMGFMT_BGR16},
  {"bgr15", IMGFMT_BGR15},
  {"bgr8", IMGFMT_BGR8},
  {"bgr4", IMGFMT_BGR4},
  {"bg4b", IMGFMT_BG4B},
  {"bgr1", IMGFMT_BGR1},
  {"rgb48be", IMGFMT_RGB48BE},
  {"rgb48le", IMGFMT_RGB48LE},
  {"rgb48", IMGFMT_RGB48NE},
  {"rgb24", IMGFMT_RGB24},
  {"rgb32", IMGFMT_RGB32},
  {"rgb16", IMGFMT_RGB16},
  {"rgb15", IMGFMT_RGB15},
  {"rgb8", IMGFMT_RGB8},
  {"rgb4", IMGFMT_RGB4},
  {"rg4b", IMGFMT_RG4B},
  {"rgb1", IMGFMT_RGB1},
  {"rgba", IMGFMT_RGBA},
  {"argb", IMGFMT_ARGB},
  {"bgra", IMGFMT_BGRA},
  {"abgr", IMGFMT_ABGR},
//  {"mjpeg", IMGFMT_MJPEG},
//  {"mjpg", IMGFMT_MJPEG},
  { NULL, 0 }
};

static int parse_imgfmt(const m_option_t* opt,const char *name,const char *param, any_t* dst, int src) {
  uint32_t fmt = 0;
  int i;
  UNUSED(src);
  UNUSED(opt);

  if (param == NULL || strlen(param) == 0)
    return M_OPT_MISSING_PARAM;

  if(!strcmp(param,"help")) {
    MSG_INFO("Available formats:");
    for(i = 0 ; mp_imgfmt_list[i].name ; i++)
      MSG_INFO(" %s",mp_imgfmt_list[i].name);
    MSG_INFO("\n");
    return M_OPT_EXIT - 1;
  }

  if (sscanf(param, "0x%x", &fmt) != 1)
  {
  for(i = 0 ; mp_imgfmt_list[i].name ; i++) {
    if(!strcasecmp(param,mp_imgfmt_list[i].name)) {
      fmt=mp_imgfmt_list[i].fmt;
      break;
    }
  }
  if(!mp_imgfmt_list[i].name) {
    MSG_ERR("Option %s: unknown format name: '%s'\n",name,param);
    return M_OPT_INVALID;
  }
  }

  if(dst)
    *((uint32_t*)dst) = fmt;

  return 1;
}

extern const m_option_type_t m_option_type_imgfmt = {
  "Image format",
  "Please report any missing colorspaces.",
  sizeof(uint32_t),
  0,
  parse_imgfmt,
  NULL,
  copy_opt,
  copy_opt,
  NULL,
  NULL
};

#include "libao3/afmt.h"

/* FIXME: sync with af_format.h */
static struct {
  const char* name;
  unsigned int fmt;
} mp_afmt_list[] = {
  // SPECIAL
  {"mulaw", AFMT_MU_LAW},
  {"alaw", AFMT_A_LAW},
  {"mpeg2", AFMT_MPEG},
  {"ac3", AFMT_AC3},
  {"imaadpcm", AFMT_IMA_ADPCM},
  // ORIDNARY
  {"u8", AFMT_U8},
  {"s8", AFMT_S8},
  {"u16le", AFMT_U16_LE},
  {"u16be", AFMT_U16_BE},
  {"s16le", AFMT_S16_LE},
  {"s16be", AFMT_S16_BE},
  {"u24le", AFMT_U24_LE},
  {"u24be", AFMT_U24_BE},
  {"s24le", AFMT_S24_LE},
  {"s24be", AFMT_S24_BE},
  {"u32le", AFMT_U32_LE},
  {"u32be", AFMT_U32_BE},
  {"s32le", AFMT_S32_LE},
  {"s32be", AFMT_S32_BE},
  {"float", AFMT_FLOAT32},
  { NULL, 0 }
};

static int parse_afmt(const m_option_t* opt,const char *name,const char *param, any_t* dst, int src) {
  uint32_t fmt = 0;
  int i;
  UNUSED(src);
  UNUSED(opt);

  if (param == NULL || strlen(param) == 0)
    return M_OPT_MISSING_PARAM;

  if(!strcmp(param,"help")) {
    MSG_INFO("Available formats:");
    for(i = 0 ; mp_afmt_list[i].name ; i++)
      MSG_INFO(" %s",mp_afmt_list[i].name);
    MSG_INFO("\n");
    return M_OPT_EXIT - 1;
  }

  if (sscanf(param, "0x%x", &fmt) != 1)
  {
  for(i = 0 ; mp_afmt_list[i].name ; i++) {
    if(!strcasecmp(param,mp_afmt_list[i].name)) {
      fmt=mp_afmt_list[i].fmt;
      break;
    }
  }
  if(!mp_afmt_list[i].name) {
    MSG_ERR("Option %s: unknown format name: '%s'\n",name,param);
    return M_OPT_INVALID;
  }
  }

  if(dst)
    *((uint32_t*)dst) = fmt;

  return 1;
}

extern const m_option_type_t m_option_type_afmt = {
  "Audio format",
  "Please report any missing formats.",
  sizeof(uint32_t),
  0,
  parse_afmt,
  NULL,
  copy_opt,
  copy_opt,
  NULL,
  NULL
};


static double parse_timestring(const char *str)
{
  int a, b;
  double d;
  if (sscanf(str, "%d:%d:%lf", &a, &b, &d) == 3)
    return 3600*a + 60*b + d;
  else if (sscanf(str, "%d:%lf", &a, &d) == 2)
    return 60*a + d;
  else if (sscanf(str, "%lf", &d) == 1)
    return d;
  return -1e100;
}

static int parse_time(const m_option_t* opt,const char *name,const char *param, any_t* dst, int src)
{
  double time;
  UNUSED(src);
  UNUSED(opt);

  if (param == NULL || strlen(param) == 0)
    return M_OPT_MISSING_PARAM;

  time = parse_timestring(param);
  if (time == -1e100) {
    MSG_ERR("Option %s: invalid time: '%s'\n",
	   name,param);
    return M_OPT_INVALID;
  }

  if (dst)
    *(double *)dst = time;
  return 1;
}

extern const m_option_type_t m_option_type_time = {
  "Time",
  "",
  sizeof(double),
  0,
  parse_time,
  print_double,
  copy_opt,
  copy_opt,
  NULL,
  NULL
};


// Time or size (-endpos)

static int parse_time_size(const m_option_t* opt,const char *name,const char *param, any_t* dst, int src) {
  m_time_size_t ts;
  char unit[4];
  double end_at;
  UNUSED(src);
  UNUSED(opt);

  if (param == NULL || strlen(param) == 0)
    return M_OPT_MISSING_PARAM;

  ts.pos=0;
  /* End at size parsing */
  if(sscanf(param, "%lf%3s", &end_at, unit) == 2) {
    ts.type = END_AT_SIZE;
    if(!strcasecmp(unit, "b"))
      ;
    else if(!strcasecmp(unit, "kb"))
      end_at *= 1024;
    else if(!strcasecmp(unit, "mb"))
      end_at *= 1024*1024;
    else if(!strcasecmp(unit, "gb"))
      end_at *= 1024*1024*1024;
    else
      ts.type = END_AT_NONE;

    if (ts.type == END_AT_SIZE) {
      ts.pos  = end_at;
      goto out;
    }
  }

  /* End at time parsing. This has to be last because the parsing accepts
   * even a number followed by garbage */
  if ((end_at = parse_timestring(param)) == -1e100) {
    MSG_ERR("Option %s: invalid time or size: '%s'\n",
	   name,param);
    return M_OPT_INVALID;
  }

  ts.type = END_AT_TIME;
  ts.pos  = end_at;
out:
  if(dst)
    *(m_time_size_t *)dst = ts;
  return 1;
}

extern const m_option_type_t m_option_type_time_size = {
  "Time or size",
  "",
  sizeof(m_time_size_t),
  0,
  parse_time_size,
  NULL,
  copy_opt,
  copy_opt,
  NULL,
  NULL
};

//// Objects (i.e. filters, etc) settings

#include "m_struct.h"

#undef VAL
#define VAL(x) (*(m_obj_settings_t**)(x))

static int find_obj_desc(const char* name,const m_obj_list_t* l,const m_struct_t** ret) {
  int i;
  char* n;

  for(i = 0 ; l->list[i] ; i++) {
    n = M_ST_MB(char*,l->list[i],l->name_off);
    if(!strcmp(n,name)) {
      *ret = M_ST_MB(m_struct_t*,l->list[i],l->desc_off);
      return 1;
    }
  }
  return 0;
}

static int get_obj_param(const char* opt_name,const char* obj_name, const m_struct_t* desc,
			 char* str,int* nold,int oldmax,char** dst) {
  char* eq;
  const m_option_t* opt;
  int r;

  eq = strchr(str,'=');
  if(eq && eq == str)
    eq = NULL;

  if(eq) {
    char* p = eq + 1;
    if(p[0] == '\0') p = NULL;
    eq[0] = '\0';
    opt = m_option_list_find(desc->fields,str);
    if(!opt) {
      MSG_ERR("Option %s: %s doesn't have a %s parameter.\n",opt_name,obj_name,str);
      return M_OPT_UNKNOWN;
    }
    r = m_option_parse(opt,str,p,NULL,M_CONFIG_FILE);
    if(r < 0) {
      if(r > M_OPT_EXIT)
	MSG_ERR("Option %s: Error while parsing %s parameter %s (%s)\n",opt_name,obj_name,str,p);
      eq[0] = '=';
      return r;
    }
    if(dst) {
      dst[0] = mp_strdup(str);
      dst[1] = p ? mp_strdup(p) : NULL;
    }
    eq[0] = '=';
  } else {
    if((*nold) >= oldmax) {
      MSG_ERR("Option %s: %s has only %d params, so you can't give more than %d unnamed params.\n",
	     opt_name,obj_name,oldmax,oldmax);
      return M_OPT_OUT_OF_RANGE;
    }
    opt = &desc->fields[(*nold)];
    r = m_option_parse(opt,opt->name,str,NULL,M_CONFIG_FILE);
    if(r < 0) {
      if(r > M_OPT_EXIT)
	MSG_ERR("Option %s: Error while parsing %s parameter %s (%s)\n",opt_name,obj_name,opt->name,str);
      return r;
    }
    if(dst) {
      dst[0] = mp_strdup(opt->name);
      dst[1] = mp_strdup(str);
    }
    (*nold)++;
  }
  return 1;
}

static int get_obj_params(const char* opt_name, const char* name,char* params,
			  const m_struct_t* desc,char separator, char*** _ret) {
  int n = 0,nold = 0, nopts,r;
  char* ptr,*last_ptr = params;
  char** ret;

  if(!strcmp(params,"help")) { // Help
    char min[50],max[50];
    if(!desc->fields) {
      printf("%s doesn't have any options.\n\n",name);
      return M_OPT_EXIT - 1;
    }
    printf("\n Name                 Type            Min        Max\n\n");
    for(n = 0 ; desc->fields[n].name ; n++) {
      const m_option_t* opt = &desc->fields[n];
      if(opt->type->flags & M_OPT_TYPE_HAS_CHILD) continue;
      if(opt->flags & M_OPT_MIN)
	sprintf(min,"%-8.0f",opt->min);
      else
	strcpy(min,"No");
      if(opt->flags & M_OPT_MAX)
	sprintf(max,"%-8.0f",opt->max);
      else
	strcpy(max,"No");
      printf(" %-20.20s %-15.15s %-10.10s %-10.10s\n",
	     opt->name,
	     opt->type->name,
	     min,
	     max);
    }
    printf("\n");
    return M_OPT_EXIT - 1;
  }

  for(nopts = 0 ; desc->fields[nopts].name ; nopts++)
    /* NOP */;

  // TODO : Check that each opt can be parsed
  r = 1;
  while(last_ptr && last_ptr[0] != '\0') {
    ptr = strchr(last_ptr,separator);
    if(!ptr) {
      r = get_obj_param(opt_name,name,desc,last_ptr,&nold,nopts,NULL);
      n++;
      break;
    }
    if(ptr == last_ptr) { // Empty field, count it and go on
      nold++;
      last_ptr = ptr+1;
      continue;
    }
    ptr[0] = '\0';
    r = get_obj_param(opt_name,name,desc,last_ptr,&nold,nopts,NULL);
    ptr[0] = separator;
    if(r < 0) break;
    n++;
    last_ptr = ptr+1;
  }
  if(r < 0) return r;
  if (!last_ptr[0]) // count an empty field at the end, too
    nold++;
  if (nold > nopts) {
    MSG_ERR("Too many options for %s\n", name);
    return M_OPT_OUT_OF_RANGE;
  }
  if(!_ret) // Just test
    return 1;
  if (n == 0) // No options or only empty options
    return 1;

  ret = new char*[(n+2)*2];
  n = nold = 0;
  last_ptr = params;

  while(last_ptr && last_ptr[0] != '\0') {
    ptr = strchr(last_ptr,separator);
    if(!ptr) {
      get_obj_param(opt_name,name,desc,last_ptr,&nold,nopts,&ret[n*2]);
      n++;
      break;
    }
    if(ptr == last_ptr) { // Empty field, count it and go on
      last_ptr = ptr+1;
      nold++;
      continue;
    }
    ptr[0] = '\0';
    get_obj_param(opt_name,name,desc,last_ptr,&nold,nopts,&ret[n*2]);
    n++;
    last_ptr = ptr+1;
  }
  ret[n*2] = ret[n*2+1] = NULL;
  *_ret = ret;

  return 1;
}

static int parse_obj_params(const m_option_t* opt,const char *name,
			    const char *param, any_t* dst, int src) {
  char** opts;
  int r;
  m_obj_params_t* p = reinterpret_cast<m_obj_params_t*>(opt->priv);
  const m_struct_t* desc;
  char* cpy = mp_strdup(param);
  UNUSED(src);

  // We need the object desc
  if(!p) {
    delete cpy;
    return M_OPT_INVALID;
  }
  desc = p->desc;
  r = get_obj_params(name,desc->name,cpy,desc,p->separator,dst ? &opts : NULL);
  delete cpy;
  if(r < 0)
    return r;
  if(!dst)
    return 1;
  if (!opts) // no arguments given
    return 1;

  for(r = 0 ; opts[r] ; r += 2)
    m_struct_set(desc,dst,opts[r],opts[r+1]);

  return 1;
}


extern const m_option_type_t m_option_type_obj_params = {
  "Object params",
  "",
  0,
  0,
  parse_obj_params,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};

/// Some predefined types as a definition would be quite lengthy

/// Span arguments
static const m_span_t m_span_params_dflts = { -1, -1 };
static const m_option_t m_span_params_fields[] = {
  {"start", M_ST_OFF(m_span_t,start), MCONF_TYPE_INT, M_OPT_MIN, 1 ,0, NULL},
  {"end", M_ST_OFF(m_span_t,end), MCONF_TYPE_INT, M_OPT_MIN , 1 ,0, NULL},
  { NULL, NULL, 0, 0, 0, 0,  NULL }
};
static const struct m_struct_t m_span_opts = {
  "m_span",
  sizeof(m_span_t),
  &m_span_params_dflts,
  m_span_params_fields
};
const m_obj_params_t m_span_params_def = {
  &m_span_opts,
  '-'
};

static int parse_obj_settings(const char* opt,char* str,const m_obj_list_t* list,
			      m_obj_settings_t **_ret, int ret_n) {
  int r;
  char *param,**plist = NULL;
  const m_struct_t* desc;
  m_obj_settings_t *ret = _ret ? *_ret : NULL;

  // Now check that the object exists
  param = strchr(str,'=');
  if(param) {
    param[0] = '\0';
    param++;
    if(strlen(param) <= 0)
      param = NULL;
  }


  if(!find_obj_desc(str,list,&desc)) {
    MSG_ERR("Option %s: %s doesn't exist.\n",opt,str);
    return M_OPT_INVALID;
  }

  if(param) {
    if(!desc && _ret) {
      if(!strcmp(param,"help")) {
	MSG_INFO("Option %s: %s have no option description.\n",opt,str);
	return M_OPT_EXIT - 1;
      }
      plist = new(zeromem) char*[4];
      plist[0] = mp_strdup("_oldargs_");
      plist[1] = mp_strdup(param);
    } else if(desc) {
      r = get_obj_params(opt,str,param,desc,':',_ret ? &plist : NULL);
      if(r < 0)
	return r;
    }
  }
  if(!_ret)
    return 1;

  ret = (m_obj_settings_t*)mp_realloc(ret,(ret_n+2)*sizeof(m_obj_settings_t));
  memset(&ret[ret_n],0,2*sizeof(m_obj_settings_t));
  ret[ret_n].name = mp_strdup(str);
  ret[ret_n].attribs = plist;

  *_ret = ret;
  return 1;
}

static void free_obj_settings_list(const any_t* dst);

static int obj_settings_list_del(const char *opt_name,const char *param,any_t* dst, int src) {
  char** str_list = NULL;
  int r,i,idx_max = 0;
  const char* rem_id = "_removed_marker_";
  const m_option_t list_opt = {opt_name , NULL, MCONF_TYPE_STRING_LIST,
			   0, 0, 0, NULL };
  m_obj_settings_t* obj_list = dst ? VAL(dst) : NULL;

  if(dst && !obj_list) {
    MSG_WARN("Option %s: the list is empty.\n",opt_name);
    return 1;
  } else if(obj_list) {
    for(idx_max = 0 ; obj_list[idx_max].name != NULL ; idx_max++)
      /* NOP */;
  }

  r = m_option_parse(&list_opt,opt_name,param,&str_list,src);
  if(r < 0 || !str_list)
    return r;

  for(r = 0 ; str_list[r] ; r++) {
    int id;
    char* endptr;
    id = strtol(str_list[r],&endptr,0);
    if(endptr == str_list[r]) {
      MSG_ERR("Option %s: invalid parameter. We need a list of integers which are the indices of the elements to remove.\n",opt_name);
      m_option_free(&list_opt,&str_list);
      return M_OPT_INVALID;
    }
    if(!obj_list) continue;
    if(id >= idx_max || id < -idx_max) {
      MSG_WARN("Option %s: Index %d is out of range.\n",opt_name,id);
      continue;
    }
    if(id < 0)
      id = idx_max + id;
    delete obj_list[id].name;
    free_str_list(&(obj_list[id].attribs));
    obj_list[id].name = rem_id;
  }

  if(!dst) {
    m_option_free(&list_opt,&str_list);
    return 1;
  }

  for(i = 0 ; obj_list[i].name ; i++) {
    while(obj_list[i].name == rem_id) {
      memmove(&obj_list[i],&obj_list[i+1],sizeof(m_obj_settings_t)*(idx_max - i));
      idx_max--;
    }
  }
  obj_list = (m_obj_settings_t*)mp_realloc(obj_list,sizeof(m_obj_settings_t)*(idx_max+1));
  VAL(dst) = obj_list;

  return 1;
}

static int parse_obj_settings_list(const m_option_t* opt,const char *name,
				   const char *param, any_t* dst, int src) {
  int n = 0,r,len = strlen(opt->name);
  char *str;
  char *ptr, *last_ptr;
  m_obj_settings_t *res = NULL,*queue = NULL,*head = NULL;
  int op = OP_NONE;

  // We need the objects list
  if(!opt->priv)
    return M_OPT_INVALID;

  if(opt->name[len-1] == '*' && ((int)strlen(name) > len - 1)) {
    const char* ns = &name[len-1];
    if(strcasecmp(ns,"-add") == 0)
      op = OP_ADD;
    else if(strcasecmp(ns,"-pre") == 0)
      op = OP_PRE;
    else if(strcasecmp(ns,"-del") == 0)
      op = OP_DEL;
    else if(strcasecmp(ns,"-clr") == 0)
      op = OP_CLR;
    else {
      char prefix[len];
      strncpy(prefix,opt->name,len-1);
      prefix[len-1] = '\0';
      MSG_ERR("Option %s: unknown postfix %s\n"
	     "Supported postfixes are:\n"
	     "  %s-add\n"
	     " Append the given list to the current list\n\n"
	     "  %s-pre\n"
	     " Prepend the given list to the current list\n\n"
	     "  %s-del x,y,...\n"
	     " Remove the given elements. Take the list element index (starting from 0).\n"
	     " Negative index can be used (i.e. -1 is the last element)\n\n"
	     "  %s-clr\n"
	     " Clear the current list.\n",name,n,prefix,prefix,prefix,prefix);

      return M_OPT_UNKNOWN;
    }
  }

  // Clear the list ??
  if(op == OP_CLR) {
    if(dst)
      free_obj_settings_list(dst);
    return 0;
  }

  if (param == NULL || strlen(param) == 0)
    return M_OPT_MISSING_PARAM;

  switch(op) {
  case OP_ADD:
    if(dst) head = VAL(dst);
    break;
  case OP_PRE:
    if(dst) queue = VAL(dst);
     break;
  case OP_DEL:
    return obj_settings_list_del(name,param,dst,src);
  case OP_NONE:
    if(dst && VAL(dst))
      free_obj_settings_list(dst);
    break;
  default:
    MSG_ERR("Option %s: FIXME\n",name);
    return M_OPT_UNKNOWN;
  }

  if(!strcmp(param,"help")) {
    m_obj_list_t* ol = reinterpret_cast<m_obj_list_t*>(opt->priv);
    MSG_INFO("Available video filters:\n"
	     "ID_VIDEO_FILTERS\n");
    for(n = 0 ; ol->list[n] ; n++)
      MSG_INFO("  %-15s: %s\n",
	     M_ST_MB(char*,ol->list[n],ol->name_off),
	     M_ST_MB(char*,ol->list[n],ol->info_off));
    MSG_INFO("\n");
    return M_OPT_EXIT - 1;
  }
  ptr = str = mp_strdup(param);

  while(ptr[0] != '\0') {
    last_ptr = ptr;
    ptr = get_nextsep(ptr, LIST_SEPARATOR, 1);

    const m_obj_list_t* mobjl = const_cast<const m_obj_list_t*>(reinterpret_cast<m_obj_list_t*>(opt->priv));
    if(!ptr) {
      r = parse_obj_settings(name,last_ptr,mobjl,dst ? &res : NULL,n);
      if(r < 0) {
	delete str;
	return r;
      }
      n++;
      break;
    }
    ptr[0] = '\0';
    r = parse_obj_settings(name,last_ptr,mobjl,dst ? &res : NULL,n);
    if(r < 0) {
      delete str;
      return r;
    }
    ptr++;
    n++;
  }
  delete str;
  if(n == 0)
    return M_OPT_INVALID;

  if( ((opt->flags & M_OPT_MIN) && (n < opt->min)) ||
      ((opt->flags & M_OPT_MAX) && (n > opt->max)) )
    return M_OPT_OUT_OF_RANGE;

  if(dst) {
    if(queue) {
      int qsize;
      for(qsize = 0 ; queue[qsize].name ; qsize++)
	/* NOP */;
      res = (m_obj_settings_t*)mp_realloc(res,(qsize+n+1)*sizeof(m_obj_settings_t));
      memcpy(&res[n],queue,(qsize+1)*sizeof(m_obj_settings_t));
      n += qsize;
      delete queue;
    }
    if(head) {
      int hsize;
      for(hsize = 0 ; head[hsize].name ; hsize++)
	/* NOP */;
      head = (m_obj_settings_t*)mp_realloc(head,(hsize+n+1)*sizeof(m_obj_settings_t));
      memcpy(&head[hsize],res,(n+1)*sizeof(m_obj_settings_t));
      delete res;
      res = head;
    }
    VAL(dst) = res;
  }
  return 1;
}

static void free_obj_settings_list(const any_t* dst) {
  int n;
  m_obj_settings_t *d;

  if(!dst || !VAL(dst)) return;

  d = VAL(dst);
#ifndef NO_FREE
  for(n = 0 ; d[n].name ; n++) {
    delete d[n].name;
    free_str_list(&(d[n].attribs));
  }
  delete d;
#endif
  VAL(dst) = NULL;
}

static void copy_obj_settings_list(const m_option_t* opt,any_t* dst,const any_t* src) {
  m_obj_settings_t *d,*s;
  int n;
  UNUSED(opt);

  if(!(dst && src))
    return;

  s = VAL(src);

  if(VAL(dst))
    free_obj_settings_list(dst);
  if(!s) return;

  for(n = 0 ; s[n].name ; n++)
    /* NOP */;
  d = new m_obj_settings_t [n+1];
  for(n = 0 ; s[n].name ; n++) {
    d[n].name = mp_strdup(s[n].name);
    d[n].attribs = NULL;
    copy_str_list(NULL,&(d[n].attribs),&(s[n].attribs));
  }
  d[n].name = NULL;
  d[n].attribs = NULL;
  VAL(dst) = d;
}

extern const m_option_type_t m_option_type_obj_settings_list = {
  "Object settings list",
  "",
  sizeof(m_obj_settings_t*),
  M_OPT_TYPE_DYNAMIC|M_OPT_TYPE_ALLOW_WILDCARD,
  parse_obj_settings_list,
  NULL,
  copy_obj_settings_list,
  copy_obj_settings_list,
  copy_obj_settings_list,
  free_obj_settings_list,
};

static int parse_obj_presets(const m_option_t* opt,const char *name,
			    const char *param, any_t* dst, int src) {
  m_obj_presets_t* obj_p = (m_obj_presets_t*)opt->priv;
  m_struct_t *in_desc,*out_desc;
  int s,i;
  unsigned char* pre;
  char* pre_name = NULL;
  UNUSED(src);
  if(!obj_p) {
    MSG_ERR("Option %s: Presets need a pointer to a m_obj_presets_t in the priv field.\n",name);
    return M_OPT_PARSER_ERR;
  }

  if(!param)
    return M_OPT_MISSING_PARAM;

  pre = reinterpret_cast<unsigned char*>(obj_p->presets);
  in_desc = obj_p->in_desc;
  out_desc = obj_p->out_desc ? obj_p->out_desc : obj_p->in_desc;
  s = in_desc->size;

  if(!strcmp(param,"help")) {
    MSG_INFO("Available presets for %s->%s:",out_desc->name,name);
    for(pre = reinterpret_cast<unsigned char*>(obj_p->presets);(pre_name = M_ST_MB(char*,pre,obj_p->name_off)) ;
	pre +=  s)
      MSG_ERR(" %s",pre_name);
    MSG_ERR("\n");
    return M_OPT_EXIT - 1;
  }

  for(pre_name = M_ST_MB(char*,pre,obj_p->name_off) ; pre_name ;
      pre +=  s, pre_name = M_ST_MB(char*,pre,obj_p->name_off)) {
    if(!strcmp(pre_name,param)) break;
  }
  if(!pre_name) {
    MSG_ERR("Option %s: There is no preset named %s\n"
	   "Available presets are:",name,param);
    for(pre = reinterpret_cast<unsigned char*>(obj_p->presets);(pre_name = M_ST_MB(char*,pre,obj_p->name_off)) ;
	pre +=  s)
      MSG_ERR(" %s",pre_name);
    MSG_ERR("\n");
    return M_OPT_INVALID;
  }

  if(!dst) return 1;

  for(i = 0 ; in_desc->fields[i].name ; i++) {
    const m_option_t* out_opt = m_option_list_find(out_desc->fields,
					     in_desc->fields[i].name);
    if(!out_opt) {
      MSG_ERR("Option %s: Unable to find the target option for field %s.\nPlease report this to the developers.\n",name,in_desc->fields[i].name);
      return M_OPT_PARSER_ERR;
    }
    m_option_copy(out_opt,M_ST_MB_P(dst,out_opt->p),M_ST_MB_P(pre,in_desc->fields[i].p));
  }
  return 1;
}


extern const m_option_type_t m_option_type_obj_presets = {
  "Object presets",
  "",
  0,
  0,
  parse_obj_presets,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};

#ifdef HAVE_STREAMING
static int parse_custom_url(const m_option_t* opt,const char *name,
			    const char *url, any_t* dst, int src) {
  int pos1, pos2, r, v6addr = 0;
  char *ptr1=NULL, *ptr2=NULL, *ptr3=NULL, *ptr4=NULL;
  m_struct_t* desc = reinterpret_cast<m_struct_t*>(opt->priv);
  UNUSED(src);
  if(!desc) {
    MSG_ERR("Option %s: Custom URL needs a pointer to a m_struct_t in the priv field.\n",name);
    return M_OPT_PARSER_ERR;
  }

  // extract the protocol
  ptr1 = strstr(const_cast<char*>(url), "://");
  if( ptr1==NULL ) {
    // Filename only
    if(m_option_list_find(desc->fields,"filename")) {
      m_struct_set(desc,dst,"filename",url);
      return 1;
    }
    MSG_ERR("Option %s: URL doesn't have a valid protocol!\n",name);
    return M_OPT_INVALID;
  }
  if(m_option_list_find(desc->fields,"string")) {
    if(strlen(ptr1)>3) {
      m_struct_set(desc,dst,"string",ptr1+3);
      return 1;
    }
  }
  pos1 = ptr1-url;
  if(dst && m_option_list_find(desc->fields,"protocol")) {
    ptr1[0] = '\0';
    r = m_struct_set(desc,dst,"protocol",url);
    ptr1[0] = ':';
    if(r < 0) {
      MSG_ERR("Option %s: Error while setting protocol.\n",name);
      return r;
    }
  }

  // jump the "://"
  ptr1 += 3;
  pos1 += 3;

  // check if a username:password is given
  ptr2 = strstr(ptr1, "@");
  ptr3 = strstr(ptr1, "/");
  if( ptr3!=NULL && ptr3<ptr2 ) {
    // it isn't really a username but rather a part of the path
    ptr2 = NULL;
  }
  if( ptr2!=NULL ) {

    // We got something, at least a username...
    if(!m_option_list_find(desc->fields,"username")) {
      MSG_WARN("Option %s: This URL doesn't have a username part.\n",name);
      // skip
    } else {
      ptr3 = strstr(ptr1, ":");
      if( ptr3!=NULL && ptr3<ptr2 ) {
	// We also have a password
	if(!m_option_list_find(desc->fields,"password")) {
	  MSG_WARN("Option %s: This URL doesn't have a password part.\n",name);
	  // skip
	} else { // Username and password
	  if(dst) {
	    ptr3[0] = '\0';
	    r = m_struct_set(desc,dst,"username",ptr1);
	    ptr3[0] = ':';
	    if(r < 0) {
	      MSG_ERR("Option %s: Error while setting username.\n",name);
	      return r;
	    }
	    ptr2[0] = '\0';
	    r = m_struct_set(desc,dst,"password",ptr3+1);
	    ptr2[0] = '@';
	    if(r < 0) {
	      MSG_ERR("Option %s: Error while setting password.\n",name);
	      return r;
	    }
	  }
	}
      } else { // User name only
	ptr2[0] = '\0';
	r = m_struct_set(desc,dst,"username",ptr1);
	ptr2[0] = '@';
	if(r < 0) {
	  MSG_ERR("Option %s: Error while setting username.\n",name);
	  return r;
	}
      }
    }
    ptr1 = ptr2+1;
    pos1 = ptr1-url;
  }

  // before looking for a port number check if we have an IPv6 type numeric address
  // in an IPv6 URL the numeric address should be inside square braces.
  ptr2 = strstr(ptr1, "[");
  ptr3 = strstr(ptr1, "]");
  // If the [] is after the first it isn't the hostname
  ptr4 = strstr(ptr1, "/");
  if( ptr2!=NULL && ptr3!=NULL && (ptr2 < ptr3) && (!ptr4 || ptr4 > ptr3)) {
    // we have an IPv6 numeric address
    ptr1++;
    pos1++;
    ptr2 = ptr3;
    v6addr = 1;
  } else {
    ptr2 = ptr1;
  }

  // look if the port is given
  ptr2 = strstr(ptr2, ":");
  // If the : is after the first / it isn't the port
  ptr3 = strstr(ptr1, "/");
  if(ptr3 && ptr3 - ptr2 < 0) ptr2 = NULL;
  if( ptr2==NULL ) {
    // No port is given
    // Look if a path is given
    if( ptr3==NULL ) {
      // No path/filename
      // So we have an URL like http://www.hostname.com
      pos2 = strlen(url);
    } else {
      // We have an URL like http://www.hostname.com/file.txt
      pos2 = ptr3-url;
    }
  } else {
    // We have an URL beginning like http://www.hostname.com:1212
    // Get the port number
    if(!m_option_list_find(desc->fields,"port")) {
      MSG_WARN("Option %s: This URL doesn't have a port part.\n",name);
      // skip
    } else {
      if(dst) {
	int p = atoi(ptr2+1);
	char tmp[100];
	snprintf(tmp,99,"%d",p);
	r = m_struct_set(desc,dst,"port",tmp);
	if(r < 0) {
	  MSG_ERR("Option %s: Error while setting port.\n",name);
	  return r;
	}
      }
    }
    pos2 = ptr2-url;
  }
  if( v6addr ) pos2--;
  // Get the hostname
  if(pos2-pos1 > 0) {
    if(!m_option_list_find(desc->fields,"hostname")) {
      MSG_WARN("Option %s: This URL doesn't have a hostname part.\n",name);
      // skip
    } else {
      char tmp[pos2-pos1+1];
      strncpy(tmp,ptr1, pos2-pos1);
      tmp[pos2-pos1] = '\0';
      r = m_struct_set(desc,dst,"hostname",tmp);
      if(r < 0) {
	MSG_ERR("Option %s: Error while setting hostname.\n",name);
	return r;
      }
    }
  }
  // Look if a path is given
  ptr2 = strstr(ptr1, "/");
  if( ptr2!=NULL ) {
    // A path/filename is given
    // check if it's not a trailing '/'
    if( strlen(ptr2)>1 ) {
      // copy the path/filename in the URL container
      if(!m_option_list_find(desc->fields,"filename")) {
	MSG_WARN("Option %s: This URL doesn't have a hostname part.\n",name);
	// skip
      } else {
	if(dst) {
	  int l = strlen(ptr2+1) + 1;
	  char* fname = ptr2+1;
	  if(l > 1) {
	    fname = new char [l];
	    url2string(fname,ptr2+1);
	  }
	  r = m_struct_set(desc,dst,"filename",fname);
	  if(fname != ptr2+1)
	    delete fname;
	  if(r < 0) {
	    MSG_ERR("Option %s: Error while setting filename.\n",name);
	    return r;
	  }
	}
      }
    }
  }
  return 1;
}

/// TODO : Write the other needed funcs for 'normal' options
extern const m_option_type_t m_option_type_custom_url = {
  "Custom URL",
  "",
  0,
  0,
  parse_custom_url,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};
#endif
