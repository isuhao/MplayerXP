/*
  MplayerXP's Signal handling
*/
#ifndef _GNU_SOURCE
#define _GNU_SOURCE   /* to get definition of strsignal */
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

#include "mp_config.h"
#include "mplayerxp.h"
#include "xmpcore/xmp_core.h"
#include "mp_msg.h"
#include "sig_hand.h"
#include "osdep/mplib.h"
#define MSGT_CLASS MSGT_CPLAYER
#include "mp_msg.h"

using namespace mpxp;

#ifdef HAVE_BACKTRACE
#include <execinfo.h>
/* A dummy function to make the backtrace more interesting. */
static void dump_trace (void) {
    show_backtrace("Obtained %zd stack frames.\n",mp_conf.max_trace);
}
#endif
static void my_callback(int signo)
{
    int i;
    pthread_t _self = pthread_self();
    for(i=0; i < xp_core->num_threads && !pthread_equal(xp_core->mpxp_threads[i]->pth_id, _self); i++);
    if(i >= xp_core->num_threads ||
	!pthread_equal(xp_core->mpxp_threads[i]->pth_id, _self)) i = 0; /* Use 0 as default handler */

    MSG_FATAL("catching signal: %s in thread: %s (%i) in module: %s\n"
	,strsignal(signo)
	,xp_core->mpxp_threads[i]->name
	,i
	,xp_core->mpxp_threads[i]->unit);
#ifdef HAVE_BACKTRACE
    dump_trace();
#endif
    xp_core->mpxp_threads[i]->sigfunc();

    signal(signo,SIG_DFL); /* try coredump*/

    return;
}

void init_signal_handling( void )
{
#ifdef MP_DEBUG
  /*========= Catch terminate signals: ================*/
  /* terminate requests:*/
  signal(SIGTERM,my_callback); /* kill*/
  signal(SIGHUP,my_callback);  /* kill -HUP  /  xterm closed*/

  signal(SIGINT,my_callback);  /* Interrupt from keyboard */

  signal(SIGQUIT,my_callback); /* Quit from keyboard */
  /* fatal errors: */
  signal(SIGBUS,my_callback);  /* bus error */
  signal(SIGSEGV,my_callback); /* segfault */
  signal(SIGILL,my_callback);  /* illegal instruction */
  signal(SIGFPE,my_callback);  /* floating point exc. */
  signal(SIGABRT,my_callback); /* abort() */
#endif
#ifdef RLIMIT_CORE
  {
    /* on many systems default coresize is 0.
       Enable any coresize here. */
    struct rlimit rl;
    getrlimit(RLIMIT_CORE,&rl);
    rl.rlim_cur = rl.rlim_max;
    setrlimit(RLIMIT_CORE,&rl);
  }
#endif
}
