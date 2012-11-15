#include "mp_config.h"

#ifdef HAVE_LIRC

#include <lirc/lirc_client.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdlib.h>

#include "mp_msg.h"
#include "help_mp.h"
#include "input.h"
#include "in_msg.h"

static struct lirc_config *lirc_config;
char *lirc_configfile;

typedef struct priv_s {
    int		lirc_sock;
    char*	cmd_buf;
}priv_t;

any_t* mp_input_lirc_init(void) {
    priv_t* priv=mp_mallocz(sizeof(priv_t));

    MSG_INFO(MSGTR_SettingUpLIRC);
    if((priv->lirc_sock=lirc_init("mplayer",1))==-1){
	MSG_ERR(MSGTR_LIRCopenfailed MSGTR_LIRCdisabled);
	mo_free(priv);
	return NULL;
    }

    if(lirc_readconfig( lirc_configfile,&lirc_config,NULL )!=0 ){
	MSG_ERR(MSGTR_LIRCcfgerr MSGTR_LIRCdisabled,
		lirc_configfile == NULL ? "~/.lircrc" : lirc_configfile);
	lirc_deinit();
	mp_free(priv);
	return NULL;
    }
    return priv;
}

int mp_input_lirc_read(any_t* ctx,char* dest, int s) {
    priv_t* priv = (priv_t*)ctx;
    fd_set fds;
    struct timeval tv;
    int r,cl = 0;
    char *code = NULL,*c = NULL;

    // We have something in the buffer return it
    if(priv->cmd_buf != NULL) {
	int l = strlen(priv->cmd_buf), w = l > s ? s : l;
	memcpy(dest,priv->cmd_buf,w);
	l -= w;
	if(l > 0) memmove(priv->cmd_buf,&cmd_buf[w],l+1);
	else {
	    mp_free(priv->cmd_buf);
	    priv->cmd_buf = NULL;
	}
	return w;
    }
    // Nothing in the buffer, pool the lirc fd
    FD_ZERO(&fds);
    FD_SET(priv->lirc_sock,&fds);
    memset(&tv,0,sizeof(tv));
    while((r = select(fd+1,&fds,NULL,NULL,&tv)) <= 0) {
	if(r < 0) {
	    if(errno == EINTR) continue;
	    MSG_ERR("Select error : %s\n",strerror(errno));
	    return MP_INPUT_ERROR;
	} else
	    return MP_INPUT_NOTHING;
    }
    // There's something to read
    if(lirc_nextcode(&code) != 0) {
	MSG_ERR("Lirc error :(\n");
	return MP_INPUT_DEAD;
    }

    if(!code) return MP_INPUT_NOTHING;

    // We put all cmds in a single buffer separated by \n
    while((r = lirc_code2char(lirc_config,code,&c))==0 && c!=NULL) {
	int l = strlen(c);
	if(l <= 0) continue;
	priv->cmd_buf = mp_realloc(priv->cmd_buf,cl+l+2);
	memcpy(&priv->cmd_buf[cl],c,l);
	cl += l+1;
	priv->cmd_buf[cl-1] = '\n';
	priv->cmd_buf[cl] = '\0';
    }
    mp_free(code);

    if(r < 0) return MP_INPUT_DEAD;
    else if(priv->cmd_buf) // return the first command in the buffer
	return mp_input_lirc_read(priv->lirc_sock,dest,s);
    else
	return MP_INPUT_NOTHING;
}

void mp_input_lirc_close(any_t* ctx) {
    priv_t* priv = (priv_t*)ctx;
    if(priv->cmd_buf) {
	mp_free(priv->cmd_buf);
	priv->cmd_buf = NULL;
    }
    lirc_freeconfig(lirc_config);
    lirc_deinit();
    mp_free(priv);
}

#endif
