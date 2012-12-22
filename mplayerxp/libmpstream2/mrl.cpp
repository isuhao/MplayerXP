#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;
#include <stdlib.h>
#include <string.h>

#include "mrl.h"
#include "stream_msg.h"

#undef TEST_MRL

#ifdef TEST_MRL
#include <stdio.h>
#endif
namespace mpxp {
const char *mrl_parse_line(const std::string& _line,char **user,char **pass,char **ms,char **port)
{
    unsigned ssize;
    const char *endp,*endl;
    const char* line=_line.c_str();
    if(user) *user=NULL;
    if(pass) *pass=NULL;
    if(ms) *ms=NULL;
    if(port) *port=NULL;
    if(!line) return line;
    endl=line+strlen(line);
    if(*line == '~')
    {
	line++;
	endp=strpbrk(line,"*@:#");
	if(!endp) endp=endl;
	if(user)
	{
	    ssize=endp-line+1;
	    *user=new char [ssize];
	    memcpy(*user,line,ssize-1);
	    (*user)[ssize-1]='\0';
	}
	line=endp;
    }
    if(*line == '*')
    {
	line++;
	endp=strpbrk(line,"@:#");
	if(!endp) endp=endl;
	if(pass)
	{
	    ssize=endp-line+1;
	    *pass=new char [ssize];
	    memcpy(*pass,line,ssize-1);
	    (*pass)[ssize-1]='\0';
	}
	line=endp;
    }
    if(*line=='@') line++;
    endp=strpbrk(line,":#");
    if(endp && endp!=line)
    {
	if(ms)
	{
	    ssize=endp-line+1;
	    *ms=new char [ssize];
	    memcpy(*ms,line,ssize-1);
	    (*ms)[ssize-1]='\0';
	}
	line=endp;
    }
    if(*line == ':')
    {
	line++;
	endp=strchr(line,'#');
	if(!endp) endp=endl;
	if(port)
	{
	    ssize=endp-line+1;
	    *port=new char [ssize];
	    memcpy(*port,line,ssize-1);
	    (*port)[ssize-1]='\0';
	}
	line=endp;
    }
    if(*line=='#') line++;
    return line;
}

static void mrl_store_args(const std::string& arg,char *value, const mrl_config_t * args)
{
#ifdef TEST_MRL
    mpxp_info<<"arg='"<<arg<<"' value='"<<value<<std::endl;
    return;
#endif
    unsigned i;
    int done=0;
    i=0;
    while(args[i].arg!=NULL) {
	if(arg==args[i].arg) {
	    done=1;
	    switch(args[i].type) {
		case MRL_TYPE_PRINT:
			mpxp_info<<(char *)args[i].value<<std::endl;
		default:
			delete value;
			break;
		case MRL_TYPE_BOOL:
			if(strcasecmp(value,"on")==0 ||
			   strcasecmp(value,"yes")==0 ||
			   strcasecmp(value,"1")==0)
			    *((int *)args[i].value)=args[i].max;
			else
			    *((int *)args[i].value)=args[i].min;
			delete value;
			break;
		case MRL_TYPE_INT: {
		    int result=atoi(value);
		    delete value;
		    if(result < args[i].min) result=args[i].min;
		    if(result > args[i].max) result=args[i].max;
		    *((int *)args[i].value)=result;
		}
		break;
		case MRL_TYPE_FLOAT: {
		    int result=atof(value);
		    delete value;
		    if(result < args[i].min) result=args[i].min;
		    if(result > args[i].max) result=args[i].max;
		    *((float *)args[i].value)=result;
		}
		break;
		case MRL_TYPE_STRING: {
		    char *p=reinterpret_cast<char*>(args[i].value);
		    p=value;
		    break;
		}
	    }
	    break;
	}
	i++;
    }
    if(!done) mpxp_warn<<" Can't handle argument: "<<arg<<std::endl;
}

#define MRL_ARG_SEP ','

const char * mrl_parse_params(const std::string& _param, const mrl_config_t * args)
{
    const char *sep,*endp,*endl;
    char *arg=NULL,*value=NULL;
    unsigned ssize;
    const char* param=_param.c_str();
    endl=param+strlen(param);
    while(*param) {
	sep=strchr(param,'=');
	if(sep) {
	    sep++;
	    endp=strchr(sep,MRL_ARG_SEP);
	    if(!endp) endp=endl;
	    ssize=sep-param-1;
	    if(arg) delete arg;
	    arg=new char [ssize+1];
	    memcpy(arg,param,ssize);
	    arg[ssize]='\0';
	    ssize=endp-sep;
	    value=new char [ssize+1];
	    memcpy(value,sep,ssize);
	    value[ssize]='\0';
	    mrl_store_args(arg,value,args);
	    value=NULL;
	}
	else break;
	param=endp+1;
	if(endp==endl) { param--; break; }
    }
    if(arg) delete arg;
    if(value) delete value;
    return param;
}
} // namespace mpxp

