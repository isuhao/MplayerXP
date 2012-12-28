#include "mpxp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;

#include "osdep_msg.h"
namespace mpxp {

std::string get_path(const std::map<std::string,std::string>& envm,const std::string& filename) {
    std::map<std::string,std::string>::const_iterator it;
    it = envm.find("HOME");
    const std::string homedir = (*it).second;
    std::string rs;
    std::string config_dir = std::string("/.")+PROGNAME;

    if (homedir.empty()) throw "No 'HOME' environment found";
    rs=homedir+config_dir;
    if (!filename.empty()) rs+="/"+filename;
    mpxp_v<<"get_path('"<<homedir<<":"<<filename<<"') -> "<<rs<<std::endl;
    return rs;
}

}// namespace mpxp
