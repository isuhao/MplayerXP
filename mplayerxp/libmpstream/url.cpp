#include "mp_config.h"
#include "osdep/mplib.h"
using namespace mpxp;
/*
 * URL Helper
 * by Bertrand Baudet <bertrand_baudet@yahoo.com>
 * (C) 2001, MPlayer team.
 *
 */
#include <limits>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <inttypes.h>

#include "url.h"
#include "help_mp.h"
#include "stream_msg.h"
#include "mplayerxp.h"

namespace mpxp {
URL *url_redirect(URL **url, const std::string& _redir) {
  std::string redir=_redir;
  URL *u = *url;
  URL *res;
  if (redir.find('/')==std::string::npos || redir[0] == '/') {
    char *tmp;
    std::string newurl;
    newurl=u->url;
    if (redir[0] == '/') {
      redir=redir.substr(1);
      tmp = strstr(const_cast<char*>(newurl.c_str()), "://");
      if (tmp) tmp = strchr(tmp + 3, '/');
    } else
      tmp = strrchr(const_cast<char*>(newurl.c_str()), '/');
    if (tmp) tmp[1] = 0;
    newurl+=redir;
    res = url_new(newurl);
  } else
    res = url_new(redir);
  delete u;
  *url = res;
  return res;
}

URL* url_new(const std::string& url) {
	int pos1, pos2,v6addr = 0;
	URL* Curl = NULL;
	char *escfilename=NULL;
	char *ptr1=NULL, *ptr2=NULL, *ptr3=NULL, *ptr4=NULL;
	int jumpSize = 3;

	if( url.empty()) return NULL;

	if (url.length() > (std::numeric_limits<size_t>::max() / 3 - 1)) {
		MSG_FATAL("MemAllocFailed\n");
		goto err_out;
	}
	escfilename=new char [url.length()*3+1];
	if (!escfilename ) {
		MSG_FATAL("MemAllocFailed\n");
		goto err_out;
	}

	// Create the URL container
	Curl = new(zeromem) URL;
	if( Curl==NULL ) {
		MSG_FATAL("MemAllocFailed\n");
		goto err_out;
	}

	string2url(escfilename,url);

	// Copy the url in the URL container
	Curl->url = mp_strdup(escfilename);
	if( Curl->url==NULL ) {
		MSG_FATAL("MemAllocFailed\n");
		goto err_out;
	}
	MSG_V("Filename for url is now %s\n",escfilename);

	// extract the protocol
	ptr1 = strstr(escfilename, "://");
	if( ptr1==NULL ) {
		// Check for a special case: "sip:" (without "//"):
		if (strstr(escfilename, "sip:") == escfilename) {
			ptr1 = (char *)&url[3]; // points to ':'
			jumpSize = 1;
		} else {
			MSG_V("Not an URL!\n");
			goto err_out;
		}
	}
	pos1 = ptr1-escfilename;
	Curl->protocol = new char [pos1+1];
	if( Curl->protocol==NULL ) {
		MSG_FATAL("MemAllocFailed\n");
		goto err_out;
	}
	strncpy(Curl->protocol, escfilename, pos1);
	Curl->protocol[pos1] = '\0';

	// jump the "://"
	ptr1 += jumpSize;
	pos1 += jumpSize;

	// check if a username:password is given
	ptr2 = strstr(ptr1, "@");
	ptr3 = strstr(ptr1, "/");
	if( ptr3!=NULL && ptr3<ptr2 ) {
		// it isn't really a username but rather a part of the path
		ptr2 = NULL;
	}
	if( ptr2!=NULL ) {
		// We got something, at least a username...
		int len = ptr2-ptr1;
		Curl->username = new char [len+1];
		if( Curl->username==NULL ) {
			MSG_FATAL("MemAllocFailed\n");
			goto err_out;
		}
		strncpy(Curl->username, ptr1, len);
		Curl->username[len] = '\0';

		ptr3 = strstr(ptr1, ":");
		if( ptr3!=NULL && ptr3<ptr2 ) {
			// We also have a password
			int len2 = ptr2-ptr3-1;
			Curl->username[ptr3-ptr1]='\0';
			Curl->password = new char [len2+1];
			if( Curl->password==NULL ) {
				MSG_FATAL("MemAllocFailed\n");
				goto err_out;
			}
			strncpy( Curl->password, ptr3+1, len2);
			Curl->password[len2]='\0';
		}
		ptr1 = ptr2+1;
		pos1 = ptr1-escfilename;
	}

	// before looking for a port number check if we have an IPv6 type numeric address
	// in IPv6 URL the numeric address should be inside square braces.
	ptr2 = strstr(ptr1, "[");
	ptr3 = strstr(ptr1, "]");
	ptr4 = strstr(ptr1, "/");
	if( ptr2!=NULL && ptr3!=NULL && ptr2 < ptr3 && (!ptr4 || ptr4 > ptr3)) {
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
			pos2 = strlen(escfilename);
		} else {
			// We have an URL like http://www.hostname.com/file.txt
			pos2 = ptr3-escfilename;
		}
	} else {
		// We have an URL beginning like http://www.hostname.com:1212
		// Get the port number
		Curl->port = atoi(ptr2+1);
		pos2 = ptr2-escfilename;
	}
	if( v6addr ) pos2--;
	// copy the hostname in the URL container
	Curl->hostname = new char [pos2-pos1+1];
	if( Curl->hostname==NULL ) {
		MSG_FATAL("MemAllocFailed\n");
		goto err_out;
	}
	strncpy(Curl->hostname, ptr1, pos2-pos1);
	Curl->hostname[pos2-pos1] = '\0';

	// Look if a path is given
	ptr2 = strstr(ptr1, "/");
	if( ptr2!=NULL ) {
		// A path/filename is given
		// check if it's not a trailing '/'
		if( strlen(ptr2)>1 ) {
			// copy the path/filename in the URL container
			Curl->file = mp_strdup(ptr2);
			if( Curl->file==NULL ) {
				MSG_FATAL("MemAllocFailed\n");
				goto err_out;
			}
		}
	}
	// Check if a filename was given or set, else set it with '/'
	if( Curl->file==NULL ) {
		Curl->file = new char [2];
		if( Curl->file==NULL ) {
			MSG_FATAL("MemAllocFailed\n");
			goto err_out;
		}
		strcpy(Curl->file, "/");
	}

	delete escfilename;
	return Curl;
err_out:
	if (escfilename) delete escfilename;
	if (Curl) delete Curl;
	return NULL;
}

URL::URL() {}
URL::~URL() {
    if(url) delete url;
    if(protocol) delete protocol;
    if(hostname) delete hostname;
    if(file) delete file;
    if(username) delete username;
    if(password) delete password;
}

/* Replace escape sequences in an URL (or a part of an URL) */
/* works like strcpy(), but without return argument */
void
url2string(char *outbuf, const std::string& inbuf)
{
	unsigned char c,c1,c2;
	int i,len=inbuf.length();
	for (i=0;i<len;i++){
		c = inbuf[i];
		if (c == '%' && i<len-2) { //must have 2 more chars
			c1 = toupper(inbuf[i+1]); // we need uppercase characters
			c2 = toupper(inbuf[i+2]);
			if (	((c1>='0' && c1<='9') || (c1>='A' && c1<='F')) &&
				((c2>='0' && c2<='9') || (c2>='A' && c2<='F')) ) {
				if (c1>='0' && c1<='9') c1-='0';
				else c1-='A'-10;
				if (c2>='0' && c2<='9') c2-='0';
				else c2-='A'-10;
				c = (c1<<4) + c2;
				i=i+2; //only skip next 2 chars if valid esc
			}
		}
		*outbuf++ = c;
	}
	*outbuf++='\0'; //add nullterm to string
}

static void
url_escape_string_part(char *outbuf, const char *inbuf) {
	unsigned char c,c1,c2;
	int i,len=strlen(inbuf);

	for  (i=0;i<len;i++) {
		c = inbuf[i];
		if ((c=='%') && i<len-2 ) { //need 2 more characters
		    c1=toupper(inbuf[i+1]); c2=toupper(inbuf[i+2]); // need uppercase chars
		   } else {
		    c1=129; c2=129; //not escape chars
		   }

		if(	(c >= 'A' && c <= 'Z') ||
			(c >= 'a' && c <= 'z') ||
			(c >= '0' && c <= '9') ||
			(c >= 0x7f)) {
			*outbuf++ = c;
		} else if ( c=='%' && ((c1 >= '0' && c1 <= '9') || (c1 >= 'A' && c1 <= 'F')) &&
			   ((c2 >= '0' && c2 <= '9') || (c2 >= 'A' && c2 <= 'F'))) {
							      // check if part of an escape sequence
			    *outbuf++=c;                      // already

							      // dont escape again
			    MSG_ERR("URL String already escaped: %c %c %c\n",c,c1,c2);
							      // error as this should not happen against RFC 2396
							      // to escape a string twice
		} else {
			/* all others will be escaped */
			c1 = ((c & 0xf0) >> 4);
			c2 = (c & 0x0f);
			if (c1 < 10) c1+='0';
			else c1+='A'-10;
			if (c2 < 10) c2+='0';
			else c2+='A'-10;
			*outbuf++ = '%';
			*outbuf++ = c1;
			*outbuf++ = c2;
		}
	}
	*outbuf++='\0';
}

/* Replace specific characters in the URL string by an escape sequence */
/* works like strcpy(), but without return argument */
void
string2url(char *outbuf, const std::string& _inbuf) {
    char* inbuf=mp_strdup(_inbuf.c_str());
    int i = 0,j,len = strlen(inbuf);
    char* tmp,*in;
    char *unesc = NULL;

	// Look if we have an ip6 address, if so skip it there is
	// no need to escape anything in there.
	tmp = strstr(inbuf,"://[");
	if(tmp) {
		tmp = strchr(tmp+4,']');
		if(tmp && (tmp[1] == '/' || tmp[1] == ':' ||
			   tmp[1] == '\0')) {
			i = tmp+1-inbuf;
			strncpy(outbuf,inbuf,i);
			outbuf += i;
			tmp = NULL;
		}
	}

	while(i < len) {
		unsigned char c='\0';
		// look for the next char that must be kept
		for  (j=i;j<len;j++) {
			c = inbuf[j];
			if(c=='-' || c=='_' || c=='.' || c=='!' || c=='~' ||	/* mark characters */
			   c=='*' || c=='\'' || c=='(' || c==')' || 	 	/* do not touch escape character */
			   c==';' || c=='/' || c=='?' || c==':' || c=='@' || 	/* reserved characters */
			   c=='&' || c=='=' || c=='+' || c=='$' || c==',') 	/* see RFC 2396 */
				break;
		}
		// we are on a reserved char, write it out
		if(j == i) {
			*outbuf++ = c;
			i++;
			continue;
		}
		// we found one, take that part of the string
		if(j < len) {
			if(!tmp) tmp = new char [len+1];
			strncpy(tmp,inbuf+i,j-i);
			tmp[j-i] = '\0';
			in = tmp;
		} else // take the rest of the string
			in = (char*)inbuf+i;

		if(!unesc) unesc = new char [len+1];
		// unescape first to avoid escaping escape
		url2string(unesc,in);
		// then escape, including mark and other reserved chars
		// that can come from escape sequences
		url_escape_string_part(outbuf,unesc);
		outbuf += strlen(outbuf);
		i += strlen(in);
	}
    *outbuf = '\0';
    if(tmp) delete tmp;
    if(unesc) delete unesc;
    delete inbuf;
}

#ifdef __URL_DEBUG
void
url_debug(const URL *url) {
	if( url==NULL ) {
		printf("URL pointer NULL\n");
		return;
	}
	if( url->url!=NULL ) {
		printf("url=%s\n", url->url );
	}
	if( url->protocol!=NULL ) {
		printf("protocol=%s\n", url->protocol );
	}
	if( url->hostname!=NULL ) {
		printf("hostname=%s\n", url->hostname );
	}
	printf("port=%d\n", url->port );
	if( url->file!=NULL ) {
		printf("file=%s\n", url->file );
	}
	if( url->username!=NULL ) {
		printf("username=%s\n", url->username );
	}
	if( url->password!=NULL ) {
		printf("password=%s\n", url->password );
	}
}
#endif //__URL_DEBUG
} // namespace mpxp
