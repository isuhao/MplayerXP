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
#include "mp_config.h"

#define COMMAND_LINE		0
#define CONFIG_FILE		1

#define CONFIG_GLOBAL (1<<0)
#define CONFIG_RUNNING (1<<1)

#define SET_GLOBAL(c)  (c->flags |= CONFIG_GLOBAL)
#define UNSET_GLOBAL(c) (c->flags &= (!CONFIG_GLOBAL))
#define IS_GLOBAL(c) (c->flags & CONFIG_GLOBAL)
#define SET_RUNNING(c) (c->flags |= CONFIG_RUNNING)
#define IS_RUNNING(c) (c->flags & CONFIG_RUNNING)

#define MAX_RECURSION_DEPTH	8

#ifdef MP_DEBUG
#include <assert.h>
#endif

#include "libmpdemux/stream.h"
#include "cfgparser.h"
#include "osdep/mplib.h"
#define MSGT_CLASS MSGT_CFGPARSER
#include "mp_msg.h"

typedef int (*cfg_func_arg_param_t)(const config_t *,const char *,const char *);
typedef int (*cfg_func_param_t)(const config_t *,const char *);
typedef int (*cfg_func_t)(const config_t *);

static void
m_config_save_option(m_config_t* config,const config_t* conf,const char* opt,const char *param) {
  config_save_t* save;
  int sl=0;

#ifdef MP_DEBUG
  assert(config != NULL);
  assert(config->cs_level >= 0);
  assert(conf != NULL);
  assert(opt != NULL);
  assert( ! (conf->flags & CONF_NOSAVE));
#endif

  switch(conf->type) {
  case CONF_TYPE_PRINT :
  case CONF_TYPE_SUBCONFIG :
    return;
  default :
    ;
  }

  MSG_DBG2("Saving option %s\n",opt);

  save = config->config_stack[config->cs_level];

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
  case CONF_TYPE_INT :
    save[sl].param.as_int = *((int*)conf->p);
    break;
  case CONF_TYPE_FLOAT :
    save[sl].param.as_float = *((float*)conf->p);
    break;
  case CONF_TYPE_STRING :
    save[sl].param.as_pointer = *((char**)conf->p);
    break;
  case CONF_TYPE_FUNC_FULL :
    if(strcasecmp(conf->name,opt) != 0) save->opt_name = mp_strdup(opt);
  case CONF_TYPE_FUNC_PARAM :
    if(param)
      save->param.as_pointer = mp_strdup(param);
  case CONF_TYPE_FUNC :
    break;
  default :
    MSG_ERR("Should never append in m_config_save_option : conf->type=%d\n",conf->type);
  }

  config->config_stack[config->cs_level] = save;
}

static int m_config_revert_option(m_config_t* config, config_save_t* save) {
  const char* arg = NULL;
  config_save_t* iter=NULL;
  int i=-1;

#ifdef MP_DEBUG
  assert(config != NULL);
  assert(config->cs_level >= 0);
  assert(save != NULL);
#endif


  arg = save->opt_name ? save->opt_name : save->opt->name;
  MSG_DBG2("Reverting option %s\n",arg);

  switch(save->opt->type) {
  case CONF_TYPE_FLAG :
  case CONF_TYPE_INT :
    *((int*)save->opt->p) = save->param.as_int;
    break;
  case CONF_TYPE_FLOAT :
    *((float*)save->opt->p) = save->param.as_float;
    break;
  case CONF_TYPE_STRING :
    *((char**)save->opt->p) = save->param.as_pointer;
    break;
  case CONF_TYPE_FUNC_PARAM :
  case CONF_TYPE_FUNC_FULL :
  case CONF_TYPE_FUNC :
    if(config->cs_level > 0) {
      for(i = config->cs_level - 1 ; i >= 0 ; i--){
	if(config->config_stack[i] == NULL) continue;
	for(iter = config->config_stack[i]; iter != NULL && iter->opt != NULL ; iter++) {
	  if(iter->opt == save->opt && 
	     ((save->param.as_pointer == NULL || iter->param.as_pointer == NULL) || strcasecmp(save->param.as_pointer,iter->param.as_pointer) == 0) && 
	     (save->opt_name == NULL || 
	      (iter->opt_name && strcasecmp(save->opt_name,iter->opt_name)))) break;
	}
      }
    }
    mp_free(save->param.as_pointer);
    if(save->opt_name) mp_free(save->opt_name);
    save->opt_name = save->param.as_pointer = NULL;
    if(i < 0) break;
    arg = iter->opt_name ? iter->opt_name : iter->opt->name;
    switch(iter->opt->type) {
    case CONF_TYPE_FUNC :
      if ((((cfg_func_t) iter->opt->p)(iter->opt)) < 0)
	return -1;
      break;
    case CONF_TYPE_FUNC_PARAM :
      if (iter->param.as_pointer == NULL) {
	MSG_ERR("We lost param for option %s?\n",iter->opt->name);
	return -1;
      }
      if ((((cfg_func_param_t) iter->opt->p)(iter->opt, (char*)iter->param.as_pointer)) < 0)
	return -1;
      break;
    case CONF_TYPE_FUNC_FULL :
      if (iter->param.as_pointer != NULL && ((char*)iter->param.as_pointer)[0]=='-'){
	if( ((cfg_func_arg_param_t) iter->opt->p)(iter->opt, arg, NULL) < 0)
	  return -1;
      }else {
	if (((cfg_func_arg_param_t) save->opt->p)(iter->opt, arg, (char*)iter->param.as_pointer) < 0) 
	  return -1;

      }
      break;
    }
    break;
  default :
    MSG_ERR("Why do we reverse this : name=%s type=%d ?\n",save->opt->name,save->opt->type);
  }

  return 1;
}

void m_config_push(m_config_t* config) {

#ifdef MP_DEBUG
  assert(config != NULL);
  assert(config->cs_level >= 0);
#endif

  config->cs_level++;
  config->config_stack = (config_save_t**)mp_realloc(config->config_stack ,sizeof(config_save_t*)*(config->cs_level+1));
  if(config->config_stack == NULL) {
    MSG_ERR( "Can't allocate %d bytes of memory : %s\n",sizeof(config_save_t*)*(config->cs_level+1),strerror(errno));
    config->cs_level = -1;
    return;
  }
  config->config_stack[config->cs_level] = NULL;
  MSG_DBG2("Config pushed level=%d\n",config->cs_level);
}

int m_config_pop(m_config_t* config) {
  int i,ret= 1;
  config_save_t* cs;

#ifdef MP_DEBUG
  assert(config != NULL);
  //assert(config->cs_level > 0);
#endif

  if(config->config_stack[config->cs_level] != NULL) {
    cs = config->config_stack[config->cs_level];
    for(i=0; cs[i].opt != NULL ; i++ ) {
      if (m_config_revert_option(config,&cs[i]) < 0)
	ret = -1;
    }
    mp_free(config->config_stack[config->cs_level]);
  }
  config->config_stack = (config_save_t**)mp_realloc(config->config_stack ,sizeof(config_save_t*)*config->cs_level);
  config->cs_level--;
  if(config->cs_level > 0 && config->config_stack == NULL) {
    MSG_ERR( "Can't allocate %d bytes of memory : %s\n",sizeof(config_save_t*)*config->cs_level,strerror(errno));
    config->cs_level = -1;
    return -1;
  }
  MSG_DBG2("Config poped level=%d\n",config->cs_level);
  return ret;
}

m_config_t* m_config_new(play_tree_t* pt) {
  m_config_t* config;

#ifdef MP_DEBUG
  assert(pt != NULL);
#endif

  config = (m_config_t*)mp_calloc(1,sizeof(m_config_t));
  if(config == NULL) {
    MSG_ERR( "Can't allocate %d bytes of memory : %s\n",sizeof(m_config_t),strerror(errno));
    return NULL;
  }
  config->config_stack = (config_save_t**)mp_calloc(1,sizeof(config_save_t*));
  if(config->config_stack == NULL) {
    MSG_ERR( "Can't allocate %d bytes of memory : %s\n",sizeof(config_save_t*),strerror(errno));
    mp_free(config);
    return NULL;
  }
  SET_GLOBAL(config); // We always start with global options
  config->pt = pt;
  return config;
}

static void m_config_add_dynamic(m_config_t *conf,any_t*ptr) {
    if(!conf->dynasize) conf->dynamics = mp_malloc(sizeof(any_t*));
    else		conf->dynamics = mp_realloc(conf->dynamics,(conf->dynasize+1)*sizeof(any_t*));
    conf->dynamics[conf->dynasize] = ptr;
    conf->dynasize++;
}

void m_config_free(m_config_t* config) {
  unsigned i;
#ifdef MP_DEBUG
  assert(config != NULL);
#endif
  for(i=0;i<config->dynasize;i++) mp_free(config->dynamics[i]);
  mp_free(config->dynamics);
  config->dynasize=0;
  mp_free(config->opt_list);
  mp_free(config->config_stack);
  mp_free(config);
}


static int init_conf(m_config_t *config, int mode)
{
#ifdef MP_DEBUG
	assert(config != NULL);
	assert(config->pt != NULL);
	assert(config->last_entry == NULL || config->last_entry->parent == config->pt);

	if (mode != COMMAND_LINE && mode != CONFIG_FILE) {
		MSG_ERR( "init_conf: wrong mode!\n");
		return -1;
	}
#endif
	config->parser_mode = mode;
	config->dynamics=NULL;
	config->dynasize=0;

	return 1;
}

static int config_is_entry_option(m_config_t *config,const char *opt,const char *param) {
  play_tree_t* entry = NULL;

#ifdef MP_DEBUG
  assert(config->pt != NULL);
#endif

  if(strcasecmp(opt,"playlist") == 0) { // We handle playlist here
    if(!param)
      return ERR_MISSING_PARAM;
    entry = parse_playlist_file(param);
    if(!entry) {
      MSG_ERR( "Playlist parsing failed: %s\n",param);
      return 1;
    }
  }

  if(entry) {
    if(config->last_entry)
      play_tree_append_entry(config->last_entry,entry);
    else
      play_tree_set_child(config->pt,entry);
    config->last_entry = entry;
    if(config->parser_mode == COMMAND_LINE)
      UNSET_GLOBAL(config);
    return 1;
  } else
    return 0;
}

static int config_read_option(m_config_t *config,const config_t** conf_list,const char *opt,const char *param)
{
	int i=0,nconf = 0;
	long tmp_int;
	double tmp_float;
	int ret = -1;
	char *endptr;
	const config_t* conf=NULL;

#ifdef MP_DEBUG
	assert(config != NULL);
	assert(conf_list != NULL);
	assert(opt != NULL);
#endif

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

	if (conf[i].flags & CONF_NOCFG && config->parser_mode == CONFIG_FILE) {
		MSG_ERR( "this option can only be used on command line:\n", opt);
		ret = ERR_NOT_AN_OPTION;
		goto out;
	}
	if (conf[i].flags & CONF_NOCMD && config->parser_mode == COMMAND_LINE) {
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
			if (config->parser_mode == CONFIG_FILE) {
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
		case CONF_TYPE_FUNC_PARAM:
			if (param == NULL)
				goto err_missing_param;
			if ((((cfg_func_param_t) conf[i].p)(conf + i, param)) < 0) {
				ret = ERR_FUNC_ERR;
				goto out;
			}
			ret = 1;
			break;
		case CONF_TYPE_FUNC_FULL:
			if (param!=NULL && param[0]=='-'){
			    ret=((cfg_func_arg_param_t) conf[i].p)(conf + i, opt, NULL);
			    if (ret>=0) ret=0;
			    /* if we return >=0: param is processed again (if there is any) */
			}else{
			    ret=((cfg_func_arg_param_t) conf[i].p)(conf + i, opt, param);
			    /* if we return 0: need no param, precess it again */
			    /* if we return 1: accepted param */
			}
			break;
		case CONF_TYPE_FUNC:
			if ((((cfg_func_t) conf[i].p)(conf + i)) < 0) {
				ret = ERR_FUNC_ERR;
				goto out;
			}
			ret = 0;
			break;
#if 0
// we have handled it in other function
		case CONF_TYPE_SUBCONFIG:
		    {
			char *subparam;
			char *subopt;
			int subconf_optnr;
			config_t *subconf;
			config_t *sublist[] = { NULL , NULL };
			char *token;
			char *p;

			if (param == NULL)
				goto err_missing_param;

			subparam = mp_malloc(strlen(param)+1);
			subopt = mp_malloc(strlen(param)+1);
			p = mp_strdup(param); // In case that param is a static string (cf man strtok)

			subconf = conf[i].p;
			sublist[0] = subconf;
			for (subconf_optnr = 0; subconf[subconf_optnr].name != NULL; subconf_optnr++)
			    /* NOTHING */;
			config->sub_conf = opt;
			token = strtok(p, (char *)&(":"));
			while(token)
			{
			    int sscanf_ret;
			    /* clear out */
			    subopt[0] = subparam[0] = 0;

			    sscanf_ret = sscanf(token, "%[^=]=%s", subopt, subparam);

			    MSG_DBG3( "token: '%s', i=%d, subopt='%s', subparam='%s' (ret: %d)\n", token, i, subopt, subparam, sscanf_ret);
			    switch(sscanf_ret)
			    {
				case 1:
				    subparam[0] = 0;
				case 2:
				    if ((ret = config_read_option(config,sublist, subopt, subparam)) < 0)
				    {
					MSG_ERR( "Subconfig parsing returned error: %d in token: %s\n",
					    ret, token);
					goto out;
				    }
				    break;
				default:
				    MSG_ERR( "Invalid subconfig argument! ('%s')\n", token);
				    ret = ERR_NOT_AN_OPTION;
				    goto out;
			    }
			    token = strtok(NULL, (char *)&(":"));
			}
			config->sub_conf = NULL;
			mp_free(subparam);
			mp_free(subopt);
			mp_free(p);
			ret = 1;
			break;
		    }
#endif
		case CONF_TYPE_PRINT:
			MSG_INFO("%s", (char *) conf[i].p);
			exit(1);
		default:
			MSG_ERR( "Unknown config type specified in conf-mplayerxp.h!\n");
			break;
	}
out:
	if(ret >= 0 && ! IS_RUNNING(config) && ! IS_GLOBAL(config) && ! (conf[i].flags & CONF_GLOBAL) && conf[i].type != CONF_TYPE_SUBCONFIG ) {
	  play_tree_t* dest = config->last_entry ? config->last_entry : config->last_parent;
	  char* o;
#ifdef MP_DEBUG
	  assert(dest != NULL);
#endif
	  if(config->sub_conf) {
	    o = (char*)mp_malloc((strlen(config->sub_conf) + 1 + strlen(opt) + 1)*sizeof(char));
	    sprintf(o,"%s:%s",config->sub_conf,opt);
	  } else
	    o =mp_strdup(opt);

	  if(ret == 0)
	    play_tree_set_param(dest,o,NULL);
	  else if(ret > 0)
	    play_tree_set_param(dest,o,param);
	  mp_free(o);
	  m_config_pop(config);
	}
	return ret;
err_missing_param:
	MSG_ERR( "missing parameter for option: %s\n", opt);
	ret = ERR_MISSING_PARAM;
	goto out;
}

static const config_t* m_config_find_option(const config_t **list,const char *name);

int m_config_set_option(m_config_t *config,const char *opt,const char *param) {
  char *e;
  const config_t **clist=config->opt_list;
#ifdef MP_DEBUG
  assert(config != NULL);
  assert(config->opt_list != NULL);
  assert(opt != NULL);
#endif
  MSG_DBG2( "Setting option %s=%s\n",opt,param);
  clist = config->opt_list;
#if 1
  if(strchr(opt,'.')) {
    int flg,ret;
    const config_t *subconf=NULL;
    const config_t* olist[] = { NULL, NULL };
    MSG_DBG2("Parsing %s as subconfig\n",opt);
    do {
	if(!(e = strchr(opt,'.'))) break;
	if((e-opt)>0) {
	    char* s = (char*)mp_malloc((e-opt+1)*sizeof(char));
	    strncpy(s,opt,e-opt);
	    s[e-opt] = '\0';
	    MSG_DBG2("Treat %s as subconfig name\n",s);
	    subconf = m_config_find_option(clist?clist:olist,s);
	    clist=NULL;
	    mp_free(s);
	    MSG_DBG2("returned %p as subconfig name\n",subconf);
	    if(!subconf) return ERR_NO_SUBCONF;
	    if(subconf->type!=CONF_TYPE_SUBCONFIG) return ERR_NO_SUBCONF;
	    olist[0] = subconf->p;
	    opt = e+1;
	    MSG_DBG2("switching next subconf=%s\n",subconf->name);
	}
    }while(1);
    flg=config->flags;
    config->flags|=CONFIG_GLOBAL;
    ret=config_read_option(config,olist,opt,param);
    config->flags=flg;
    return ret;
  }
#endif
  e = strchr(opt,':');
  if(e && e[1] != '\0') {
    int ret;
    const config_t* opt_list[] = { NULL, NULL };
    char* s = (char*)mp_malloc((e-opt+1)*sizeof(char));
    strncpy(s,opt,e-opt);
    s[e-opt] = '\0';
    opt_list[0] = m_config_get_option_ptr(config,s);
    if(!opt_list[0]) {
      MSG_ERR("m_config_set_option %s=%s : no %s subconfig\n",opt,param,s);
      mp_free(s);
      return ERR_NOT_AN_OPTION;
    }
    e++;
    s = (char*)mp_realloc(s,strlen(e) + 1);
    strcpy(s,e);
    ret = config_read_option(config,opt_list,s,param);
    mp_free(s);
    return ret;
  }

  return config_read_option(config,config->opt_list,opt,param);
}

MPXP_Rc m_config_parse_config_file(m_config_t *config, char *conffile)
{
#define PRINT_LINENUM	MSG_ERR("%s(%d): ", conffile, line_num)
#define MAX_LINE_LEN	1000
#define MAX_OPT_LEN	100
#define MAX_PARAM_LEN	100
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

#ifdef MP_DEBUG
    assert(config != NULL);
    //	assert(conf_list != NULL);
#endif
    if (++config->recursion_depth > 1) MSG_INFO("Reading config file: %s", conffile);

    if (config->recursion_depth > MAX_RECURSION_DEPTH) {
	MSG_FATAL(": too deep 'include'. check your configfiles\n");
	ret = MPXP_False;
	goto out;
    }

    if (init_conf(config, CONFIG_FILE) == -1) {
	ret = MPXP_False;
	goto out;
    }

    if ((line = (char *) mp_malloc(MAX_LINE_LEN + 1)) == NULL) {
	MSG_FATAL("\ncan't get memory for 'line': %s", strerror(errno));
	ret = MPXP_False;
	goto out;
    }

    if ((fp = fopen(conffile, "r")) == NULL) {
	if (config->recursion_depth > 1) MSG_ERR(": %s\n", strerror(errno));
	mp_free(line);
	ret = MPXP_Ok;
	goto out;
    }
    if (config->recursion_depth > 1) MSG_FATAL("\n");

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
		PRINT_LINENUM;
		MSG_ERR("too long option\n");
		errors++;
		ret = MPXP_False;
		goto nextline;
	    }
	}
	if (opt_pos == 0) {
	    PRINT_LINENUM;
	    MSG_ERR("parse error\n");
	    ret = MPXP_False;
	    errors++;
	    continue;
	}
	opt[opt_pos] = '\0';

#ifdef MP_DEBUG
	PRINT_LINENUM;
	MSG_DBG2("option: %s\n", opt);
#endif

	/* skip whitespaces */
	while (isspace(line[line_pos])) ++line_pos;

	/* check '=' */
	if (line[line_pos++] != '=') {
	    PRINT_LINENUM;
	    MSG_ERR("option without parameter\n");
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
		    PRINT_LINENUM;
		    MSG_ERR("too long parameter\n");
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
		    PRINT_LINENUM;
		    MSG_ERR("too long parameter\n");
		    ret = MPXP_False;
		    errors++;
		    goto nextline;
		}
	    }
	}
	param[param_pos] = '\0';

	/* did we read a parameter? */
	if (param_pos == 0) {
	    PRINT_LINENUM;
	    MSG_ERR("option without parameter\n");
	    ret = MPXP_False;
	    errors++;
	    continue;
	}

#ifdef MP_DEBUG
	PRINT_LINENUM;
	MSG_DBG2("parameter: %s\n", param);
#endif

	/* now, check if we have some more chars on the line */
	/* whitespace... */
	while (isspace(line[line_pos])) ++line_pos;

	/* EOL / comment */
	if (line[line_pos] != '\0' && line[line_pos] != '#') {
	    PRINT_LINENUM;
	    MSG_ERR("extra characters on line: %s\n", line+line_pos);
	    ret = MPXP_False;
	}

	tmp = m_config_set_option(config, opt, param);
	switch (tmp) {
	    case ERR_NOT_AN_OPTION:
	    case ERR_MISSING_PARAM:
	    case ERR_OUT_OF_RANGE:
	    case ERR_NO_SUBCONF:
	    case ERR_FUNC_ERR:
		PRINT_LINENUM;
		MSG_ERR("%s\n", opt);
		ret = MPXP_False;
		errors++;
		continue;
		/* break */
	}
nextline:
	;
    }

    mp_free(line);
    fclose(fp);
out:
    --config->recursion_depth;
    return ret;
}

extern void show_help(void);
extern void show_long_help(void);
MPXP_Rc m_config_parse_command_line(m_config_t *config, int argc, char **argv, char **envp)
{
    int i;
    int tmp;
    char *opt;
    int no_more_opts = 0;
    UNUSED(envp);

#ifdef MP_DEBUG
    assert(config != NULL);
    assert(config->pt != NULL);
    assert(argv != NULL);
    assert(envp != NULL);
    assert(argc >= 1);
#endif

    if (init_conf(config, COMMAND_LINE) == -1) return MPXP_False;
    if(config->last_parent == NULL) config->last_parent = config->pt;
    /* in order to work recursion detection properly in parse_config_file */
    ++config->recursion_depth;

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
	    if(config->last_entry == NULL) {
		play_tree_set_child(config->last_parent,entry);
	    } else {
		play_tree_append_entry(config->last_entry,entry);
		config->last_entry = NULL;
	    }
	    config->last_parent = entry;
	    continue;
	}

	if((opt[0] == '}') && (opt[1] == '\0')) {
	    if( ! config->last_parent || ! config->last_parent->parent) {
		MSG_ERR( "too much }-\n");
		goto err_out;
	    }
	    config->last_entry = config->last_parent;
	    config->last_parent = config->last_entry->parent;
	    continue;
	}

	if ((no_more_opts == 0) && (*opt == '-') && (*(opt+1) != 0)) /* option */ {
	    /* remove leading '-' */
	    char *assign,*item,*parm;
	    opt++;

	    MSG_DBG2( "this_option: %s\n", opt);
	    parm = argv[i+1];
	    item=opt;
	    assign = strchr(opt,'=');
	    if(assign) {
		item = mp_malloc(assign-opt);
		memcpy(item,opt,assign-opt);
		item[assign-opt]='\0';
		parm = mp_strdup(assign+1);
	    }
	    tmp = m_config_set_option(config, item, parm);
	    if(!tmp && assign) MSG_ERR("Option '%s' doesn't require arguments\n",item);
	    if(assign) {
		mp_free(item);
		mp_free(parm);
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
	    if(config->last_entry == NULL) play_tree_set_child(config->last_parent,entry);
	    else play_tree_append_entry(config->last_entry,entry);
	    config->last_entry = entry;
	}
    }

    --config->recursion_depth;
    if(config->last_parent != config->pt) MSG_ERR("Missing }- ?\n");
    UNSET_GLOBAL(config);
    SET_RUNNING(config);
    return MPXP_Ok;
#if 0
err_out_mem:
    MSG_ERR( "can't allocate memory for filenames (%s)\n", strerror(errno));
#endif
err_out:
    --config->recursion_depth;
    MSG_ERR( "command line: %s\n", argv[i]);
    return MPXP_False;
}

int m_config_register_options(m_config_t *config,const config_t *args) {
  int list_len = 0;
  const config_t** conf_list = config->opt_list;

#ifdef MP_DEBUG
  assert(config != NULL);
  assert(args != NULL);
#endif

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

  config->opt_list = conf_list;

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

const config_t* m_config_get_option(m_config_t const*config,const char* arg) {
  char *e;
  const config_t **conf_list;
  const config_t* cl[] = { NULL, NULL };

#ifdef MP_DEBUG
  assert(config != NULL);
  assert(arg != NULL);
#endif

  e = strchr(arg,':');

  if(e) {
    char *s;
    s = (char*)mp_malloc((e-arg+1)*sizeof(char));
    strncpy(s,arg,e-arg);
    s[e-arg] = '\0';
    cl[0] = m_config_get_option(config,s);
    conf_list = cl;
    mp_free(s);
  } else
    conf_list = config->opt_list;
  return m_config_find_option(conf_list,arg);
}

any_t* m_config_get_option_ptr(m_config_t const*config,const char* arg) {
  const config_t* conf;

#ifdef MP_DEBUG
  assert(config != NULL);
  assert(arg != NULL);
#endif

  conf = m_config_get_option(config,arg);
  if(!conf) return NULL;
  return conf->p;
}

int m_config_get_int (m_config_t const *config,const char* arg,int* err_ret) {
  int *ret;

#ifdef MP_DEBUG
  assert(config != NULL);
  assert(arg != NULL);
#endif

  ret = m_config_get_option_ptr(config,arg);
  if(err_ret)
    *err_ret = 0;
  if(!ret) {
    if(err_ret)
      *err_ret = 1;
    return -1;
  } else
    return (*ret);
}

float m_config_get_float (m_config_t const *config,const char* arg,int* err_ret) {
  float *ret;

#ifdef MP_DEBUG
  assert(config != NULL);
  assert(arg != NULL);
#endif

  ret = m_config_get_option_ptr(config,arg);
  if(err_ret)
    *err_ret = 0;
  if(!ret) {
    if(err_ret)
      *err_ret = 1;
    return -1;
  } else
    return (*ret);
}

#define AS_INT(c) (*((int*)c->p))

int m_config_set_int(m_config_t *config,const char* arg,int val) {
  const config_t* opt;

#ifdef MP_DEBUG
  assert(config != NULL);
  assert(arg != NULL);
#endif

  opt = m_config_get_option(config,arg);

  if(!opt || opt->type != CONF_TYPE_INT)
    return ERR_NOT_AN_OPTION;

  if(opt->flags & CONF_MIN && val < opt->min)
    return ERR_OUT_OF_RANGE;
  if(opt->flags & CONF_MAX && val > opt->max)
    return ERR_OUT_OF_RANGE;

  m_config_save_option(config,opt,arg,NULL);
  AS_INT(opt) = val;

  return 1;
}

int m_config_set_float(m_config_t *config,const char* arg,float val) {
  const config_t* opt;

#ifdef MP_DEBUG
  assert(config != NULL);
  assert(arg != NULL);
#endif

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


int m_config_switch_flag(m_config_t *config,const char* opt) {
  const config_t *conf;

#ifdef MP_DEBUG
  assert(config != NULL);
  assert(opt != NULL);
#endif

  conf = m_config_get_option(config,opt);
  if(!conf || conf->type != CONF_TYPE_FLAG) return 0;
  if( AS_INT(conf) == conf->min) AS_INT(conf) = conf->max;
  else if(AS_INT(conf) == conf->max) AS_INT(conf) = conf->min;
  else return 0;

  return 1;
}

int m_config_set_flag(m_config_t* config,const char* opt, int state) {
  const config_t *conf;

#ifdef MP_DEBUG
  assert(config != NULL);
  assert(opt != NULL);
#endif

  conf = m_config_get_option(config,opt);
  if(!conf || conf->type != CONF_TYPE_FLAG) return 0;
  if(state) AS_INT(conf) = conf->max;
  else AS_INT(conf) = conf->min;
  return 1;
}

int m_config_get_flag(m_config_t const *config,const char* opt) {
  const config_t *conf;

#ifdef MP_DEBUG
  assert(config != NULL);
  assert(opt != NULL);
#endif

  conf = m_config_get_option(config,opt);
  if(!conf || conf->type != CONF_TYPE_FLAG) return -1;
  if(AS_INT(conf) == conf->max)
    return 1;
  else if(AS_INT(conf) == conf->min)
    return 0;
  else
    return -1;
}

int m_config_is_option_set(m_config_t const*config,const char* arg) {
  const config_t* opt;
  config_save_t* save;
  int l,i;

#ifdef MP_DEBUG
  assert(config != NULL);
  assert(arg != NULL);
#endif

  opt = m_config_get_option(config,arg);

  if(!opt)
    return -1;

  for(l = config->cs_level ; l >= 0 ; l--) {
    save = config->config_stack[l];
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
	    newpfx=mp_malloc(pfxlen+1);
	    if(pfx) strcpy(newpfx,pfx);
	    else    newpfx[0]='\0';
	    strcat(newpfx,opts[i].name);
	    strcat(newpfx,".");
	    __m_config_show_options(ntabs+2,newpfx,(const config_t *)opts[i].p);
	    mp_free(newpfx);
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

void m_config_show_options(const m_config_t *args) {
    unsigned j;
    const config_t *opts;
    j=0;
    MSG_INFO("List of available command-line options:\n");
    while((opts=args->opt_list[j])!=NULL) {
	__m_config_show_options(2,NULL,opts);
	j++;
    };
}

#undef AS_INT
