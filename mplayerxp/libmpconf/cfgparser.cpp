#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;
/*
 * command line and config file parser
 * by Szabolcs Berecz <szabi@inf.elte.hu>
 * (C) 2001
 *
 * subconfig support by alex
 */

//#define DEBUG

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

enum {
    COMMAND_LINE=0,
    CONFIG_FILE=1,

    CONFIG_GLOBAL=(1<<0),
    CONFIG_RUNNING=(1<<1),

    MAX_RECURSION_DEPTH=8
};
#include "mplayerxp.h"
#include "cfgparser.h"
#include "libplaytree/playtree.h"
#include "parser_msg.h"

inline void SET_GLOBAL(m_config_t& c) { c.flags |= CONFIG_GLOBAL; }
inline void UNSET_GLOBAL(m_config_t& c) { c.flags &= (!CONFIG_GLOBAL); }
inline int  IS_GLOBAL(const m_config_t& c) { return c.flags & CONFIG_GLOBAL; }
inline void SET_RUNNING(m_config_t& c) { c.flags |= CONFIG_RUNNING; }
inline int  IS_RUNNING(const m_config_t& c) { return c.flags & CONFIG_RUNNING; }

typedef int (*cfg_func_arg_param_t)(const config_t *,const char *,const char *);
typedef int (*cfg_func_param_t)(const config_t *,const char *);
typedef int (*cfg_func_t)(const config_t *);

static void
m_config_save_option(m_config_t& config,const config_t* conf,const char* opt,const char *param) {
  config_save_t* save;
  int sl=0;

  switch(conf->type) {
  case CONF_TYPE_PRINT :
  case CONF_TYPE_SUBCONFIG :
    return;
  default :
    ;
  }

  MSG_DBG2("Saving option %s\n",opt);

  save = config.config_stack[config.cs_level];

  if(save) {
    for(sl = 0; save[sl].opt != NULL; sl++){
      // Check to not save the same arg two times
      if(save[sl].opt == conf && (save[sl].opt_name == NULL || strcasecmp(save[sl].opt_name,opt) == 0))
	break;
    }
    if(save[sl].opt)
      return;
  }

  save = (config_save_t*)mp_realloc(save,(sl+2)*sizeof(config_save_t));
  if(save == NULL) {
    MSG_ERR( "Can't allocate %d bytes of memory : %s\n",(sl+2)*sizeof(config_save_t),strerror(errno));
    return;
  }
  memset(&save[sl],0,2*sizeof(config_save_t));
  save[sl].opt = conf;

  switch(conf->type) {
  case CONF_TYPE_FLAG :
  case CONF_TYPE_INC :
  case CONF_TYPE_INT :
    save[sl].param.as_int = *((int*)conf->p);
    break;
  case CONF_TYPE_FLOAT :
    save[sl].param.as_float = *((float*)conf->p);
    break;
  case CONF_TYPE_STRING :
    save[sl].param.as_pointer = *((char**)conf->p);
    break;
  case CONF_TYPE_INCLUDE :
    if(param)
      save->param.as_pointer = mp_strdup(param);
  default :
    MSG_ERR("Should never append in m_config_save_option : conf->type=%d\n",conf->type);
  }

  config.config_stack[config.cs_level] = save;
}

static int m_config_revert_option(m_config_t& config, config_save_t* save) {
  const char* arg = NULL;
  config_save_t* iter=NULL;
  int i=-1;

  arg = save->opt_name ? save->opt_name : save->opt->name;
  MSG_DBG2("Reverting option %s\n",arg);

  switch(save->opt->type) {
  case CONF_TYPE_FLAG:
  case CONF_TYPE_INC :
  case CONF_TYPE_INT :
    *((int*)save->opt->p) = save->param.as_int;
    break;
  case CONF_TYPE_FLOAT :
    *((float*)save->opt->p) = save->param.as_float;
    break;
  case CONF_TYPE_STRING :
    *((char**)save->opt->p) = reinterpret_cast<char*>(save->param.as_pointer);
    break;
  case CONF_TYPE_INCLUDE :
    if(config.cs_level > 0) {
      for(i = config.cs_level - 1 ; i >= 0 ; i--){
	if(config.config_stack[i] == NULL) continue;
	for(iter = config.config_stack[i]; iter != NULL && iter->opt != NULL ; iter++) {
	  if(iter->opt == save->opt &&
	     ((save->param.as_pointer == NULL || iter->param.as_pointer == NULL) || strcasecmp((const char *)save->param.as_pointer,(const char *)iter->param.as_pointer) == 0) &&
	     (save->opt_name == NULL ||
	      (iter->opt_name && strcasecmp(save->opt_name,iter->opt_name)))) break;
	}
      }
    }
    delete save->param.as_pointer;
    if(save->opt_name) delete save->opt_name;
    save->param.as_pointer = NULL;
    save->opt_name = reinterpret_cast<char*>(save->param.as_pointer);
    if(i < 0) break;
    arg = iter->opt_name ? iter->opt_name : iter->opt->name;
    switch(iter->opt->type) {
    case CONF_TYPE_INCLUDE :
      if (iter->param.as_pointer == NULL) {
	MSG_ERR("We lost param for option %s?\n",iter->opt->name);
	return -1;
      }
      if ((((cfg_func_param_t) iter->opt->p)(iter->opt, (char*)iter->param.as_pointer)) < 0)
	return -1;
      break;
    }
    break;
  default :
    MSG_ERR("Why do we reverse this : name=%s type=%d ?\n",save->opt->name,save->opt->type);
  }

  return 1;
}

void m_config_push(m_config_t& config) {

  config.cs_level++;
  config.config_stack = (config_save_t**)mp_realloc(config.config_stack ,sizeof(config_save_t*)*(config.cs_level+1));
  if(config.config_stack == NULL) {
    MSG_ERR( "Can't allocate %d bytes of memory : %s\n",sizeof(config_save_t*)*(config.cs_level+1),strerror(errno));
    config.cs_level = -1;
    return;
  }
  config.config_stack[config.cs_level] = NULL;
  MSG_DBG2("Config pushed level=%d\n",config.cs_level);
}

int m_config_pop(m_config_t& config) {
  int i,ret= 1;
  config_save_t* cs;

  if(config.config_stack[config.cs_level] != NULL) {
    cs = config.config_stack[config.cs_level];
    for(i=0; cs[i].opt != NULL ; i++ ) {
      if (m_config_revert_option(config,&cs[i]) < 0)
	ret = -1;
    }
    delete config.config_stack[config.cs_level];
  }
  config.config_stack = (config_save_t**)mp_realloc(config.config_stack ,sizeof(config_save_t*)*config.cs_level);
  config.cs_level--;
  if(config.cs_level > 0 && config.config_stack == NULL) {
    MSG_ERR( "Can't allocate %d bytes of memory : %s\n",sizeof(config_save_t*)*config.cs_level,strerror(errno));
    config.cs_level = -1;
    return -1;
  }
  MSG_DBG2("Config poped level=%d\n",config.cs_level);
  return ret;
}

m_config_t& m_config_new(play_tree_t* pt,libinput_t&libinput) {
  m_config_t& config = *new(zeromem) m_config_t(libinput);
  config.config_stack = (config_save_t**)mp_calloc(1,sizeof(config_save_t*));
  SET_GLOBAL(config); // We always start with global options
  config.pt = pt;
  return config;
}

static void m_config_add_dynamic(m_config_t& conf,any_t*ptr) {
    if(!conf.dynasize) conf.dynamics = (any_t**)mp_malloc(sizeof(any_t*));
    else		conf.dynamics = (any_t**)mp_realloc(conf.dynamics,(conf.dynasize+1)*sizeof(any_t*));
    conf.dynamics[conf.dynasize] = ptr;
    conf.dynasize++;
}

void m_config_free(m_config_t* config) {
  unsigned i;
  for(i=0;i<config->dynasize;i++) delete config->dynamics[i];
  delete config->dynamics;
  config->dynasize=0;
  delete config->opt_list;
  delete config->config_stack;
  delete config;
}


static int init_conf(m_config_t& config, int mode)
{
	config.parser_mode = mode;
	config.dynamics=NULL;
	config.dynasize=0;

	return 1;
}

static int config_is_entry_option(m_config_t& config,const char *opt,const char *param) {
  play_tree_t* entry = NULL;

  if(strcasecmp(opt,"playlist") == 0) { // We handle playlist here
    if(!param)
      return ERR_MISSING_PARAM;
    entry = parse_playlist_file(config.libinput,param);
    if(!entry) {
      MSG_ERR( "Playlist parsing failed: %s\n",param);
      return 1;
    }
  }

  if(entry) {
    if(config.last_entry)
      play_tree_append_entry(config.last_entry,entry);
    else
      play_tree_set_child(config.pt,entry);
    config.last_entry = entry;
    if(config.parser_mode == COMMAND_LINE)
      UNSET_GLOBAL(config);
    return 1;
  } else
    return 0;
}

static MPXP_Rc cfg_include(m_config_t& conf,const char *filename){
    return m_config_parse_config_file(conf, filename);
}

static int cfg_inc_int(int value){ return ++value; }

static int config_read_option(m_config_t& config,const config_t** conf_list,const char *opt,const char *param)
{
	int i=0,nconf = 0;
	long tmp_int;
	double tmp_float;
	int ret = -1;
	char *endptr;
	const config_t* conf=NULL;

	MSG_DBG3( "read_option: conf=%p opt='%s' param='%s'\n",
	    conf, opt, param);
	for(nconf = 0 ;  conf_list[nconf] != NULL; nconf++) {
	  conf = conf_list[nconf];
		for (i = 0; conf[i].name != NULL; i++) {
			int namelength;
			/* allow 'aa*' in config.name */
//			MSG_DBG3( "cmp_option: conf=%s opt='%s'\n",conf[i].name,opt);
			namelength=strlen(conf[i].name);
			if ( (conf[i].name[namelength-1]=='*') &&
				    !memcmp(opt, conf[i].name, namelength-1))
			  goto option_found;
			if (!strcasecmp(opt, conf[i].name))
			  goto option_found;
		}
	}
	MSG_ERR( "invalid option: %s\n",opt);
	ret = ERR_NOT_AN_OPTION;
	goto out;
	option_found :
	MSG_DBG3( "read_option: name='%s' p=%p type=%d\n",
	    conf[i].name, conf[i].p, conf[i].type);

	if (conf[i].flags & CONF_NOCFG && config.parser_mode == CONFIG_FILE) {
		MSG_ERR( "this option can only be used on command line:\n", opt);
		ret = ERR_NOT_AN_OPTION;
		goto out;
	}
	if (conf[i].flags & CONF_NOCMD && config.parser_mode == COMMAND_LINE) {
		MSG_ERR( "this option can only be used in config file:\n", opt);
		ret = ERR_NOT_AN_OPTION;
		goto out;
	}
	ret = config_is_entry_option(config,opt,param);
	if(ret != 0)
	  return ret;
	else
	  ret = -1;
	if(! IS_RUNNING(config) && ! IS_GLOBAL(config) &&
	   ! (conf[i].flags & CONF_GLOBAL)  && conf[i].type != CONF_TYPE_SUBCONFIG  )
	  m_config_push(config);
	if( !(conf[i].flags & CONF_NOSAVE) && ! (conf[i].flags & CONF_GLOBAL) )
	  m_config_save_option(config,&conf[i],opt,param);
	switch (conf[i].type) {
		case CONF_TYPE_FLAG:
			/* flags need a parameter in config file */
			if (config.parser_mode == CONFIG_FILE) {
				if (!strcasecmp(param, "yes") ||	/* any other language? */
				    !strcasecmp(param, "ja") ||
				    !strcasecmp(param, "si") ||
				    !strcasecmp(param, "igen") ||
				    !strcasecmp(param, "y") ||
				    !strcasecmp(param, "j") ||
				    !strcasecmp(param, "i") ||
				    !strcmp(param, "1"))
					*((int *) conf[i].p) = conf[i].max;
				else if (!strcasecmp(param, "no") ||
				    !strcasecmp(param, "nein") ||
				    !strcasecmp(param, "nicht") ||
				    !strcasecmp(param, "nem") ||
				    !strcasecmp(param, "n") ||
				    !strcmp(param, "0"))
					*((int *) conf[i].p) = conf[i].min;
				else {
					MSG_ERR( "invalid parameter for flag: %s\n", param);
					ret = ERR_OUT_OF_RANGE;
					goto out;
				}
				ret = 1;
			} else {	/* parser_mode == COMMAND_LINE */
				*((int *) conf[i].p) = conf[i].max;
				MSG_DBG3("assigning %s=%i as flag value\n",conf[i].name,conf[i].max);
				ret = 0;
			}
			break;
		case CONF_TYPE_INT:
			if (param == NULL)
				goto err_missing_param;

			tmp_int = strtol(param, &endptr, 0);
			if (*endptr) {
				MSG_ERR( "parameter must be an integer: %s\n", param);
				ret = ERR_OUT_OF_RANGE;
				goto out;
			}

			if (conf[i].flags & CONF_MIN)
				if (tmp_int < conf[i].min) {
					MSG_ERR( "parameter must be >= %d: %s\n", (int) conf[i].min, param);
					ret = ERR_OUT_OF_RANGE;
					goto out;
				}

			if (conf[i].flags & CONF_MAX)
				if (tmp_int > conf[i].max) {
					MSG_ERR( "parameter must be <= %d: %s\n", (int) conf[i].max, param);
					ret = ERR_OUT_OF_RANGE;
					goto out;
				}

			*((int *) conf[i].p) = tmp_int;
			MSG_DBG3("assigning %s=%i as int value\n",conf[i].name,tmp_int);
			ret = 1;
			break;
		case CONF_TYPE_FLOAT:
			if (param == NULL)
				goto err_missing_param;

			tmp_float = strtod(param, &endptr);

			if ((*endptr == ':') || (*endptr == '/'))
				tmp_float /= strtod(endptr+1, &endptr);

			if (*endptr) {
				MSG_ERR( "parameter must be a floating point number"
				       " or a ratio (numerator[:/]denominator): %s\n", param);
				ret = ERR_MISSING_PARAM;
				goto out;
			}

			if (conf[i].flags & CONF_MIN)
				if (tmp_float < conf[i].min) {
					MSG_ERR( "parameter must be >= %f: %s\n", conf[i].min, param);
					ret = ERR_OUT_OF_RANGE;
					goto out;
				}

			if (conf[i].flags & CONF_MAX)
				if (tmp_float > conf[i].max) {
					MSG_ERR( "parameter must be <= %f: %s\n", conf[i].max, param);
					ret = ERR_OUT_OF_RANGE;
					goto out;
				}

			*((float *) conf[i].p) = tmp_float;
			MSG_DBG3("assigning %s=%f as float value\n",conf[i].name,tmp_float);
			ret = 1;
			break;
		case CONF_TYPE_STRING:
			if (param == NULL)
				goto err_missing_param;

			if (conf[i].flags & CONF_MIN)
				if (strlen(param) < conf[i].min) {
					MSG_ERR( "parameter must be >= %d chars: %s\n",
							(int) conf[i].min, param);
					ret = ERR_OUT_OF_RANGE;
					goto out;
				}

			if (conf[i].flags & CONF_MAX)
				if (strlen(param) > conf[i].max) {
					MSG_ERR( "parameter must be <= %d chars: %s\n",
							(int) conf[i].max, param);
					ret = ERR_OUT_OF_RANGE;
					goto out;
				}
			*((char **) conf[i].p) = mp_strdup(param);
			m_config_add_dynamic(config,*((char **) conf[i].p));
			MSG_DBG3("assigning %s=%s as string value\n",conf[i].name,param);
			ret = 1;
			break;
		case CONF_TYPE_INC:
			*((int *) conf[i].p) = cfg_inc_int(*((int *) conf[i].p));
			ret = 1;
			break;
		case CONF_TYPE_INCLUDE:
			if (param == NULL)
				goto err_missing_param;
			if (cfg_include(config, param) < 0) {
				ret = ERR_FUNC_ERR;
				goto out;
			}
			ret = 1;
			break;
		case CONF_TYPE_PRINT:
			MSG_INFO("%s", (char *) conf[i].p);
			exit(1);
		default:
			MSG_ERR( "Unknown config type specified in conf-mplayerxp.h!\n");
			break;
	}
out:
	if(ret >= 0 && ! IS_RUNNING(config) && ! IS_GLOBAL(config) && ! (conf[i].flags & CONF_GLOBAL) && conf[i].type != CONF_TYPE_SUBCONFIG ) {
	  play_tree_t* dest = config.last_entry ? config.last_entry : config.last_parent;
	  char* o;
	  if(config.sub_conf) {
	    o = new char [(strlen(config.sub_conf) + 1 + strlen(opt) + 1)];
	    sprintf(o,"%s:%s",config.sub_conf,opt);
	  } else
	    o =mp_strdup(opt);

	  if(ret == 0)
	    play_tree_set_param(dest,o,NULL);
	  else if(ret > 0)
	    play_tree_set_param(dest,o,param);
	  delete o;
	  m_config_pop(config);
	}
	return ret;
err_missing_param:
	MSG_ERR( "missing parameter for option: %s\n", opt);
	ret = ERR_MISSING_PARAM;
	goto out;
}

static const config_t* m_config_find_option(const config_t**list,const char *name);

int m_config_set_option(m_config_t& config,const char *opt,const char *param) {
  const char *e;
  const config_t**clist=config.opt_list;
  MSG_DBG2( "Setting option %s=%s\n",opt,param);
  clist = config.opt_list;

  if(strchr(opt,'.')) {
    int flg,ret;
    const config_t *subconf=NULL;
    const config_t* olist[] = { NULL, NULL };
    MSG_DBG2("Parsing %s as subconfig\n",opt);
    do {
	if(!(e = strchr(opt,'.'))) break;
	if((e-opt)>0) {
	    char* s = new char [e-opt+1];
	    strncpy(s,opt,e-opt);
	    s[e-opt] = '\0';
	    MSG_DBG2("Treat %s as subconfig name\n",s);
	    subconf = m_config_find_option(clist?clist:olist,s);
	    clist=NULL;
	    delete s;
	    MSG_DBG2("returned %p as subconfig name\n",subconf);
	    if(!subconf) return ERR_NO_SUBCONF;
	    if(subconf->type!=CONF_TYPE_SUBCONFIG) return ERR_NO_SUBCONF;
	    olist[0] = reinterpret_cast<const config_t*>(subconf->p);
	    opt = e+1;
	    MSG_DBG2("switching next subconf=%s\n",subconf->name);
	}
    }while(1);
    flg=config.flags;
    config.flags|=CONFIG_GLOBAL;
    ret=config_read_option(config,olist,opt,param);
    config.flags=flg;
    return ret;
  }

  e = strchr(opt,':');
  if(e && e[1] != '\0') {
    int ret;
    const config_t* opt_list[] = { NULL, NULL };
    char* s = new char [e-opt+1];
    strncpy(s,opt,e-opt);
    s[e-opt] = '\0';
    opt_list[0] = (const config_t*)m_config_get_option_ptr(config,s);
    if(!opt_list[0]) {
      MSG_ERR("m_config_set_option %s=%s : no %s subconfig\n",opt,param,s);
      delete s;
      return ERR_NOT_AN_OPTION;
    }
    e++;
    s = (char*)mp_realloc(s,strlen(e) + 1);
    strcpy(s,e);
    ret = config_read_option(config,opt_list,s,param);
    delete s;
    return ret;
  }

  return config_read_option(config,config.opt_list,opt,param);
}

static void PRINT_LINENUM(const std::string& conffile,int line_num) { mpxp_err<<conffile<<"("<<line_num<<")"<<std::endl; }
static const int MAX_LINE_LEN=1000;
static const int MAX_OPT_LEN=100;
static const int MAX_PARAM_LEN=100;
MPXP_Rc m_config_parse_config_file(m_config_t& config,const char *conffile)
{
    FILE *fp;
    char *line;
    char opt[MAX_OPT_LEN + 1];
    char param[MAX_PARAM_LEN + 1];
    char c;		/* for the "" and '' check */
    int tmp;
    int line_num = 0;
    int line_pos;	/* line pos */
    int opt_pos;	/* opt pos */
    int param_pos;	/* param pos */
    MPXP_Rc ret = MPXP_Ok;
    int errors = 0;

    if (++config.recursion_depth > 1) MSG_INFO("Reading config file: %s", conffile);

    if (config.recursion_depth > MAX_RECURSION_DEPTH) {
	MSG_FATAL(": too deep 'include'. check your configfiles\n");
	ret = MPXP_False;
	goto out;
    }

    if (init_conf(config, CONFIG_FILE) == -1) {
	ret = MPXP_False;
	goto out;
    }

    if ((line = new char [MAX_LINE_LEN + 1]) == NULL) {
	MSG_FATAL("\ncan't get memory for 'line': %s", strerror(errno));
	ret = MPXP_False;
	goto out;
    }

    if ((fp = fopen(conffile, "r")) == NULL) {
	if (config.recursion_depth > 1) MSG_ERR(": %s\n", strerror(errno));
	delete line;
	ret = MPXP_Ok;
	goto out;
    }
    if (config.recursion_depth > 1) MSG_FATAL("\n");

    while (fgets(line, MAX_LINE_LEN, fp)) {
	if (errors >= 16) {
	    MSG_FATAL("too many errors\n");
	    goto out;
	}

	line_num++;
	line_pos = 0;

	/* skip whitespaces */
	while (isspace(line[line_pos])) ++line_pos;

	/* EOL / comment */
	if (line[line_pos] == '\0' || line[line_pos] == '#') continue;

	/* read option. */
	for (opt_pos = 0; isprint(line[line_pos]) &&
		line[line_pos] != ' ' &&
		line[line_pos] != '#' &&
		line[line_pos] != '='; /* NOTHING */) {
	    opt[opt_pos++] = line[line_pos++];
	    if (opt_pos >= MAX_OPT_LEN) {
		PRINT_LINENUM(conffile,line_num);
		mpxp_err<<"too long option"<<std::endl;
		errors++;
		ret = MPXP_False;
		goto nextline;
	    }
	}
	if (opt_pos == 0) {
	    PRINT_LINENUM(conffile,line_num);
	    mpxp_err<<"parse error"<<std::endl;
	    ret = MPXP_False;
	    errors++;
	    continue;
	}
	opt[opt_pos] = '\0';

	/* skip whitespaces */
	while (isspace(line[line_pos])) ++line_pos;

	/* check '=' */
	if (line[line_pos++] != '=') {
	    PRINT_LINENUM(conffile,line_num);
	    mpxp_err<<"option without parameter"<<std::endl;
	    ret = MPXP_False;
	    errors++;
	    continue;
	}

	/* whitespaces... */
	while (isspace(line[line_pos])) ++line_pos;

	/* read the parameter */
	if (line[line_pos] == '"' || line[line_pos] == '\'') {
	    c = line[line_pos];
	    ++line_pos;
	    for (param_pos = 0; line[line_pos] != c; /* NOTHING */) {
		param[param_pos++] = line[line_pos++];
		if (param_pos >= MAX_PARAM_LEN) {
		    PRINT_LINENUM(conffile,line_num);
		    mpxp_err<<"too long parameter"<<std::endl;
		    ret = MPXP_False;
		    errors++;
		    goto nextline;
		}
	    }
	    line_pos++;	/* skip the closing " or ' */
	} else {
	    for (param_pos = 0; isprint(line[line_pos]) && !isspace(line[line_pos])
			&& line[line_pos] != '#'; /* NOTHING */) {
		param[param_pos++] = line[line_pos++];
		if (param_pos >= MAX_PARAM_LEN) {
		    PRINT_LINENUM(conffile,line_num);
		    mpxp_err<<"too long parameter"<<std::endl;
		    ret = MPXP_False;
		    errors++;
		    goto nextline;
		}
	    }
	}
	param[param_pos] = '\0';

	/* did we read a parameter? */
	if (param_pos == 0) {
	    PRINT_LINENUM(conffile,line_num);
	    mpxp_err<<"option without parameter"<<std::endl;
	    ret = MPXP_False;
	    errors++;
	    continue;
	}

	/* now, check if we have some more chars on the line */
	/* whitespace... */
	while (isspace(line[line_pos])) ++line_pos;

	/* EOL / comment */
	if (line[line_pos] != '\0' && line[line_pos] != '#') {
	    PRINT_LINENUM(conffile,line_num);
	    mpxp_err<<"extra characters on line: "<<line+line_pos<<std::endl;
	    ret = MPXP_False;
	}

	tmp = m_config_set_option(config, opt, param);
	switch (tmp) {
	    case ERR_NOT_AN_OPTION:
	    case ERR_MISSING_PARAM:
	    case ERR_OUT_OF_RANGE:
	    case ERR_NO_SUBCONF:
	    case ERR_FUNC_ERR:
		PRINT_LINENUM(conffile,line_num);
		mpxp_err<<opt<<std::endl;
		ret = MPXP_False;
		errors++;
		continue;
		/* break */
	}
nextline:
	;
    }

    delete line;
    fclose(fp);
out:
    --config.recursion_depth;
    return ret;
}

namespace mpxp {
MPXP_Rc mpxp_parse_command_line(m_config_t& config, int argc, char **argv, char **envp)
{
    int i;
    int tmp;
    char *opt;
    int no_more_opts = 0;
    UNUSED(envp);

    if (init_conf(config, COMMAND_LINE) == -1) return MPXP_False;
    if(config.last_parent == NULL) config.last_parent = config.pt;
    /* in order to work recursion detection properly in parse_config_file */
    ++config.recursion_depth;

    for (i = 1; i < argc; i++) {
	 //next:
	opt = argv[i];
	if(strcmp(opt,"--help")==0) {
	    show_help();
	    exit(0);
	}
	if(strcmp(opt,"--long-help")==0) {
	    show_long_help();
	    exit(0);
	}
	/* check for -- (no more options id.) except --help! */
	if ((*opt == '-') && (*(opt+1) == '-')) {
	    no_more_opts = 1;
	    if (i+1 >= argc) {
		MSG_ERR( "You added '--' but no filenames presented!\n");
		goto err_out;
	    }
	    continue;
	}
	if((opt[0] == '{') && (opt[1] == '\0')) {
	    play_tree_t* entry = play_tree_new();
	    UNSET_GLOBAL(config);
	    if(config.last_entry == NULL) {
		play_tree_set_child(config.last_parent,entry);
	    } else {
		play_tree_append_entry(config.last_entry,entry);
		config.last_entry = NULL;
	    }
	    config.last_parent = entry;
	    continue;
	}

	if((opt[0] == '}') && (opt[1] == '\0')) {
	    if( ! config.last_parent || ! config.last_parent->parent) {
		MSG_ERR( "too much }-\n");
		goto err_out;
	    }
	    config.last_entry = config.last_parent;
	    config.last_parent = config.last_entry->parent;
	    continue;
	}

	if ((no_more_opts == 0) && (*opt == '-') && (*(opt+1) != 0)) /* option */ {
	    /* remove leading '-' */
	    char *assign,*item,*parm;
	    unsigned sz;
	    opt++;

	    MSG_DBG2( "this_option: %s\n", opt);
	    parm = argv[i+1];
	    item=opt;
	    assign = strchr(opt,'=');
	    if(assign) {
		sz=assign-opt;
		item = new char [sz+1];
		memcpy(item,opt,sz);
		item[sz]='\0';
		parm = mp_strdup(assign+1);
	    }
	    tmp = m_config_set_option(config, item, parm);
	    if(!tmp && assign) MSG_ERR("Option '%s' doesn't require arguments\n",item);
	    if(assign) {
		delete item;
		delete parm;
	    }
	    if(!tmp && assign) goto err_out;

	    switch (tmp) {
		case ERR_NOT_AN_OPTION:
		case ERR_MISSING_PARAM:
		case ERR_OUT_OF_RANGE:
		case ERR_NO_SUBCONF:
		case ERR_FUNC_ERR:
		    MSG_ERR( "Error '%s' while parsing option: '%s'!\n"
				,tmp==ERR_NOT_AN_OPTION?"no-option":
				 tmp==ERR_MISSING_PARAM?"missing-param":
				 tmp==ERR_OUT_OF_RANGE?"out-of-range":
				 tmp==ERR_NO_SUBCONF?"no-subconfig":
				 "func-error"
				,opt);
		    goto err_out;
		default:
		    i += tmp;
		    if(assign) i--;
		    break;
	    }
	} else /* filename */ {
	    play_tree_t* entry = play_tree_new();
	    MSG_DBG2("Adding file %s\n",argv[i]);
	    play_tree_add_file(entry,argv[i]);
	    if(strcasecmp(argv[i],"-") == 0) m_config_set_option(config,"use-stdin",NULL);
	    /* opt is not an option -> treat it as a filename */
	    UNSET_GLOBAL(config); // We start entry specific options
	    if(config.last_entry == NULL) play_tree_set_child(config.last_parent,entry);
	    else play_tree_append_entry(config.last_entry,entry);
	    config.last_entry = entry;
	}
    }

    --config.recursion_depth;
    if(config.last_parent != config.pt) MSG_ERR("Missing }- ?\n");
    UNSET_GLOBAL(config);
    SET_RUNNING(config);
    return MPXP_Ok;
err_out:
    --config.recursion_depth;
    MSG_ERR( "command line: %s\n", argv[i]);
    return MPXP_False;
}
} // namespace mpxp

int m_config_register_options(m_config_t& config,const config_t *args) {
  int list_len = 0;
  const config_t** conf_list = config.opt_list;

  if(conf_list) {
    for ( ; conf_list[list_len] != NULL; list_len++)
      /* NOTHING */;
  }

  conf_list = (const config_t**)mp_realloc(conf_list,sizeof(struct conf*)*(list_len+2));
  if(conf_list == NULL) {
    MSG_ERR( "Can't allocate %d bytes of memory : %s\n",sizeof(struct conf*)*(list_len+2),strerror(errno));
    return 0;
  }
  conf_list[list_len] = args;
  conf_list[list_len+1] = NULL;

  config.opt_list = conf_list;

  return 1;
}

static const config_t* m_config_find_option(const config_t **list,const char *name) {
  unsigned i,j;
  const config_t *conf;
  if(list) {
    for(j = 0; list[j] != NULL ; j++) {
      conf = list[j];
      for(i=0; conf[i].name != NULL; i++) {
	if(strcasecmp(conf[i].name,name) == 0)
	  return &conf[i];
      }
    }
  }
  return NULL;
}

const config_t* m_config_get_option(const m_config_t& config,const char* arg) {
  const char *e;
  const config_t **conf_list;
  const config_t* cl[] = { NULL, NULL };

  e = strchr(arg,':');

  if(e) {
    char *s;
    s = new char [e-arg+1];
    strncpy(s,arg,e-arg);
    s[e-arg] = '\0';
    cl[0] = m_config_get_option(config,s);
    conf_list = cl;
    delete s;
  } else
    conf_list = config.opt_list;
  return m_config_find_option(conf_list,arg);
}

any_t* m_config_get_option_ptr(const m_config_t& config,const char* arg) {
  const config_t* conf;

  conf = m_config_get_option(config,arg);
  if(!conf) return NULL;
  return conf->p;
}

int m_config_get_int (const m_config_t& config,const char* arg,int* err_ret) {
  int *ret;

  ret = (int*)m_config_get_option_ptr(config,arg);
  if(err_ret)
    *err_ret = 0;
  if(!ret) {
    if(err_ret)
      *err_ret = 1;
    return -1;
  } else
    return (*ret);
}

float m_config_get_float (const m_config_t& config,const char* arg,int* err_ret) {
  float *ret;

  ret = (float*)m_config_get_option_ptr(config,arg);
  if(err_ret)
    *err_ret = 0;
  if(!ret) {
    if(err_ret)
      *err_ret = 1;
    return -1;
  } else
    return (*ret);
}

inline int AS_INT(const config_t* c) { return *((int*)c->p); }
inline void AS_INT(const config_t* c,int val) { *((int*)c->p)=val; }

int m_config_set_int(m_config_t& config,const char* arg,int val) {
  const config_t* opt;

  opt = m_config_get_option(config,arg);

  if(!opt || opt->type != CONF_TYPE_INT)
    return ERR_NOT_AN_OPTION;

  if(opt->flags & CONF_MIN && val < opt->min)
    return ERR_OUT_OF_RANGE;
  if(opt->flags & CONF_MAX && val > opt->max)
    return ERR_OUT_OF_RANGE;

  m_config_save_option(config,opt,arg,NULL);
  AS_INT(opt,val);

  return 1;
}

int m_config_set_float(m_config_t& config,const char* arg,float val) {
  const config_t* opt;

  opt = m_config_get_option(config,arg);

  if(!opt || opt->type != CONF_TYPE_FLOAT)
    return ERR_NOT_AN_OPTION;

  if(opt->flags & CONF_MIN && val < opt->min)
    return ERR_OUT_OF_RANGE;
  if(opt->flags & CONF_MAX && val > opt->max)
    return ERR_OUT_OF_RANGE;

  m_config_save_option(config,opt,arg,NULL);
  *((float*)opt->p) = val;

  return 1;
}


int m_config_switch_flag(m_config_t& config,const char* opt) {
  const config_t *conf;

  conf = m_config_get_option(config,opt);
  if(!conf || conf->type != CONF_TYPE_FLAG) return 0;
  if( AS_INT(conf) == conf->min) AS_INT(conf,conf->max);
  else if(AS_INT(conf) == conf->max) AS_INT(conf,conf->min);
  else return 0;

  return 1;
}

int m_config_set_flag(m_config_t& config,const char* opt, int state) {
  const config_t *conf;

  conf = m_config_get_option(config,opt);
  if(!conf || conf->type != CONF_TYPE_FLAG) return 0;
  if(state) AS_INT(conf,conf->max);
  else AS_INT(conf,conf->min);
  return 1;
}

int m_config_get_flag(const m_config_t& config,const char* opt) {

  const config_t* conf = m_config_get_option(config,opt);
  if(!conf || conf->type != CONF_TYPE_FLAG) return -1;
  if(AS_INT(conf) == conf->max) return 1;
  else if(AS_INT(conf) == conf->min) return 0;
  return -1;
}

int m_config_is_option_set(const m_config_t& config,const char* arg) {
  const config_t* opt;
  config_save_t* save;
  int l,i;

  opt = m_config_get_option(config,arg);

  if(!opt)
    return -1;

  for(l = config.cs_level ; l >= 0 ; l--) {
    save = config.config_stack[l];
    if(!save)
      continue;
    for(i = 0 ; save[i].opt != NULL ; i++) {
      if(save[i].opt == opt)
	return 1;
    }
  }

  return 0;
}

static void __m_config_show_options(unsigned ntabs,const char *pfx,const config_t *opts) {
    unsigned i,n;
    i=0;
    while(opts[i].name) {
	if(opts[i].type==CONF_TYPE_SUBCONFIG  && opts[i].p) {
	    char *newpfx;
	    unsigned pfxlen;
	    for(n=0;n<ntabs;n++) MSG_INFO(" ");
	    MSG_INFO("%s:\n",opts[i].help);
	    pfxlen=strlen(opts[i].name)+1;
	    if(pfx) pfxlen+=strlen(pfx);
	    newpfx=new char [pfxlen+1];
	    if(pfx) strcpy(newpfx,pfx);
	    else    newpfx[0]='\0';
	    strcat(newpfx,opts[i].name);
	    strcat(newpfx,".");
	    __m_config_show_options(ntabs+2,newpfx,(const config_t *)opts[i].p);
	    delete newpfx;
	}
	else
	if(opts[i].type<=CONF_TYPE_PRINT) {
	    for(n=0;n<ntabs;n++) MSG_INFO(" ");
	    if(pfx) MSG_INFO("-%-s",pfx);
	    else    MSG_INFO("-");
	    MSG_INFO("%-11s %s"
		    ,opts[i].name
		    ,(opts[i].type==CONF_TYPE_PRINT && strcmp(opts[i].help,"show help")!=0)?opts[i].p:opts[i].help);
	    if((opts[i].flags&CONF_NOCFG)==0) {
	    MSG_INFO(" {%s=",
		    opts[i].type==CONF_TYPE_FLAG?"flg":
		    opts[i].type==CONF_TYPE_INT?"int":
		    opts[i].type==CONF_TYPE_FLOAT?"flt":
		    opts[i].type==CONF_TYPE_STRING?"str":"");
	    switch(opts[i].type) {
	    case CONF_TYPE_FLAG: {
		int defv = (*((int*)(opts[i].p)))?1:0;
		int max  = opts[i].max ? 1:0;
		int res = !(defv^max);
		MSG_INFO("%s",res?"ON":"OFF");
	    }
	    break;
	    case CONF_TYPE_STRING: {
		const char **defv = (const char**)(opts[i].p);
		if(defv) MSG_INFO("\"%s\"",*defv);
	    }
	    break;
	    case CONF_TYPE_INT: {
		int defv = *((int*)(opts[i].p));
		MSG_INFO("%i",defv);
		if((opts[i].flags&CONF_RANGE)==CONF_RANGE) {
		    MSG_INFO(" [%i...%i]",(int)opts[i].min,(int)opts[i].max);
		}
		else
		if((opts[i].flags&CONF_MIN)==CONF_MIN) {
		    MSG_INFO(" <min=%i>",(int)opts[i].min);
		}
		else
		if((opts[i].flags&CONF_MAX)==CONF_MAX) {
		    MSG_INFO(" <max=%i>",(int)opts[i].max);
		}
	    }
	    break;
	    case CONF_TYPE_FLOAT: {
		float defv = *((float*)(opts[i].p));
		MSG_INFO("%f",defv);
		if((opts[i].flags&CONF_RANGE)==CONF_RANGE) {
		    MSG_INFO(" [%f...%f]",(float)opts[i].min,(float)opts[i].max);
		}
		else
		if((opts[i].flags&CONF_MIN)==CONF_MIN) {
		    MSG_INFO(" <min=%f>",(float)opts[i].min);
		}
		else
		if((opts[i].flags&CONF_MAX)==CONF_MAX) {
		    MSG_INFO(" <max=%f>",(float)opts[i].max);
		}
	    }
	    break;
	    default:
	    break;
	    }
	    MSG_INFO("}");
	    }
	    MSG_INFO("\n");
	}
	i++;
    };
}

void m_config_show_options(const m_config_t& args) {
    unsigned j;
    const config_t *opts;
    j=0;
    MSG_INFO("List of available command-line options:\n");
    while((opts=args.opt_list[j])!=NULL) {
	__m_config_show_options(2,NULL,opts);
	j++;
    };
}
