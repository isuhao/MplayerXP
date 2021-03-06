# LINUX Makefile made by A'rpi / Astral
# Some cleanup by LGB: 	* 'make -C dir' instead of 'cd dir;make;cd..'
#			* for loops instead of linear sequence of make directories
#			* some minor problems with make clean and distclean were corrected
#			* DVD support

# a BSD compatible 'install' program
INSTALL = install

include mpxp_config.mak

TARGET_EXE = $(PROGNAME)
# these subdirectories required installation due binaries within them
SUBDIRS = libmpdemux libmpstream2 libmpsub libplaytree2 libmpcodecs libmpconf libao3 osdep postproc input2 nls libvo2 xmpcore

DO_MAKE = @ for i in $(SUBDIRS); do $(MAKE) -C $$i $@ || exit; done
DO_MAKE_ALL = @ for i in $(SUBDIRS); do $(MAKE) -C $$i all || exit; done

MANDIR = ${prefix}/man
LDFLAGS += -Wl,-rpath,${CODECDIR}/codecs

SRCS = main.cpp dump.cpp mpxp_msg.cpp

OBJS = $(SRCS:.cpp=.o)

LAVC_LIBS=../lavc/libavcodec/libavcodec$(LAVC_SUFFIX).a \
	  ../lavc/libswscale/libswscale$(LAVC_SUFFIX).a \
	  ../lavc/libswresample/libswresample$(LAVC_SUFFIX).a \
	  ../lavc/libpostproc/libpostproc$(LAVC_SUFFIX).a \
	  ../lavc/libavformat/libavformat$(LAVC_SUFFIX).a \
	  ../lavc/libavutil/libavutil$(LAVC_SUFFIX).a

MP_LIBS = libmpstream2/libmpstream2.a \
	  libmpdemux/libmpdemux.a \
	  libmpcodecs/libmpcodecs.a \
	  libmpsub/libmpsub.a \
	  libplaytree2/libplaytree2.a \
	  libao3/libao3.a \
	  postproc/libpostproc.a \
	  input2/libinput2.a \
	  libvo2/libvo2.a \
	  osdep/libosdep.a \
	  nls/libnls.a \
	  libmpconf/libmpconf.a \
	  xmpcore/libxmpcore.a

LIBS+= $(MP_LIBS) $(LAVC_LIBS) $(EXTRALIBS) -lm
CXXFLAGS = $(OPTXXFLAGS) $(EXTRA_INC)

.SUFFIXES: .cpp .c .o

# .PHONY: all clean

all:	dep $(TARGET_EXE) $(SUBDIRS)

.cpp.o:
	$(CXX) -c $(CXXFLAGS) -o $@ $<

.PHONY: subdirs $(MP_LIBS)
subdirs: $(MP_LIBS)

$(TARGET_EXE):	$(OBJS) $(MP_LIBS)
	$(DO_MAKE_ALL)
	$(CXX) -o $(TARGET_EXE) $(OBJS) $(LDFLAGS) $(LDXXFLAGS) $(LIBS)
#-Xlinker --export-dynamic -Xlinker --gc-sections -Xlinker --sort-common
$(SRCS):

install: $(TARGET_EXE)
ifeq ($(INSTALL),)
	@echo "*** 'install' utility was not found and you can't run automatic"
	@echo "*** installation. Please download 'fileutils' from ftp://ftp.gnu.org and"
	@echo "*** install them to have possibility perform autiomatic installation"
	@echo "*** of this project" 
	@exit 1
endif
	$(INSTALL) -D -m 755 $(TARGET_EXE) $(DESTDIR)$(BINDIR)/$(TARGET_EXE)
	@if test ! -d $(DESTDIR)$(DATADIR) ; then mkdir -p $(DESTDIR)$(DATADIR) ; fi
	@if test ! -d $(DESTDIR)$(DATADIR)/font ; then mkdir -p $(DESTDIR)$(DATADIR)/font ; fi
	@if test ! -f $(DESTDIR)$(DATADIR)/font/font.desc ; then \
	echo "*** Download font at http://www.mplayerhq.hu/homepage/dload.html" ; \
	echo "*** for OSD/Subtitles support and extract to $(DATADIR)/font/" ; \
	fi

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET_EXE)
	rm -f $(DESTDIR)$(DATADIR)/font/*
	rmdir -p --ignore-fail-on-non-empty $(DESTDIR)$(DATADIR)/font
	rmdir -p --ignore-fail-on-non-empty $(DESTDIR)$(BINDIR)

clean:
	$(DO_MAKE)
	-rm -f *.o *~ $(OBJS) $(XXOBJS)

distclean:
	$(DO_MAKE)
	-rm -f *~ $(TARGET_EXE) $(OBJS)
	-rm -f *.o *.a .depend configure.log
	-rm -f mpxp_config.h mpxp_config.mak mpxp_conf_lavc.h version.h
	-rm -f cpuinfo mpxp_help.h

dep: .depend

.depend: $(SRCS)
	$(CXX) -MM $(CXXFLAGS) $(SRCS) 1>.depend
	$(DO_MAKE)

#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
