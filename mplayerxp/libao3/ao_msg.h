#ifndef AO_MSG_H
#define AO_MSG_H

#include "mpxp_msg.h"

namespace	usr {
    static mpxp_ostream_info	mpxp_info(MSGT_AO);
    static mpxp_ostream_fatal	mpxp_fatal(MSGT_AO);
    static mpxp_ostream_err	mpxp_err(MSGT_AO);
    static mpxp_ostream_warn	mpxp_warn(MSGT_AO);
    static mpxp_ostream_ok	mpxp_ok(MSGT_AO);
    static mpxp_ostream_hint	mpxp_hint(MSGT_AO);
    static mpxp_ostream_status	mpxp_status(MSGT_AO);
    static mpxp_ostream_v	mpxp_v(MSGT_AO);
    static mpxp_ostream_dbg2	mpxp_dbg2(MSGT_AO);
    static mpxp_ostream_dbg3	mpxp_dbg3(MSGT_AO);
    static mpxp_ostream_dbg4	mpxp_dbg4(MSGT_AO);
} // namespace	usr

#endif
