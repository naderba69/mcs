ifdef target
OPTS =	-DCLI_CSCACHE -DSRV_CSCACHE -DCHECK_NEXTDCW -DSID_FILTER -DNEWCACHE \
		-DCCCAM_CLI -DRADEGAST_CLI -DCAMD35_CLI -DCS378X_CLI \
		-DHTTP_SRV -DTELNET -DMGCAMD_SRV -DCCCAM_SRV -DCAMD35_SRV -DCS378X_SRV -DFREECCCAM_SRV \
		-DEXPIREDATE -DDCWSWAP -DCACHEEX -DTHREAD_DCW -DMONOTHREAD_ACCEPT \
		-DDEBUG_NETWORK
else
OPTS =	-DCLI_CSCACHE -DSRV_CSCACHE -DCHECK_NEXTDCW -DSID_FILTER -DNEWCACHE \
		-DCCCAM_CLI -DRADEGAST_CLI -DCAMD35_CLI -DCS378X_CLI \
		-DHTTP_SRV -DTELNET -DMGCAMD_SRV -DCCCAM_SRV -DCAMD35_SRV -DCS378X_SRV -DFREECCCAM_SRV \
		-DEXPIREDATE -DDCWSWAP -DCACHEEX -DTHREAD_DCW \
		-DEPOLL_NEWCAMD -DEPOLL_CCCAM -DEPOLL_MGCAMD -DEPOLL_ECM ##-DEPOLL_CACHE
		## -DTESTCHANNEL -DDEBUG_NETWORK -DDEBUG_NETWORK2
		##  -DRADEGAST_SRV  -DFREECCCAM_SRV -DEPOLL_CACHE
		## -DMULTICONNECT -DRECVMSG_BLOC -DIPLIST
endif


ifeq ($(target),x32)
  CC        = gcc
  OUTPUT	= x32
  CFLAGS	= -s -ggdb3 -m32 -O3 -I. -fpack-struct $(OPTS) -DEPOLL_NEWCAMD -DEPOLL_ECM
  LFLAGS	= $(CFLAGS) -L/usr/lib/gcc/x86_64-redhat-linux/4.4.4/32
else
ifeq ($(target),x64)
  CC        = gcc
  OUTPUT	= x64
  CFLAGS	= -s -ggdb3 -m64 -O3 -I. -fpack-struct $(OPTS) -DEPOLL_NEWCAMD -DEPOLL_ECM
  LFLAGS	= $(CFLAGS)
else
ifeq ($(target),ppc-old)
  CC        = /root/Desktop/tuxbox-cvs/root/cdk/bin/powerpc-tuxbox-linux-gnu-gcc
  OUTPUT	= ppc-old
  CFLAGS	= -s -ggdb3 -O2 $(OPTS) -DINOTIFY -DSTB
  LFLAGS	= $(CFLAGS)
else
ifeq ($(target),ppc)
  CC        = /opt/powerpc-tuxbox-linux-gnu/bin/powerpc-linux-gcc
  OUTPUT	= ppc
  CFLAGS	= -s -ggdb3 -O2 $(OPTS) -DINOTIFY -DSTB
  LFLAGS	= $(CFLAGS)
else
ifeq ($(target),mipsel)
  CC        = /opt/mipsel-unknown-linux-gnu/bin/mipsel-unknown-linux-gnu-gcc
  OUTPUT	= mipsel
  CFLAGS	= -s -ggdb3 -O2 $(OPTS) -DSTB -EL -march=mips1
  LFLAGS	= $(CFLAGS)
else
ifeq ($(target),mipsel-pli4)
  CC        = /opt/mipsel-tuxbox-linux-gnu/bin/mipsel-tuxbox-linux-gnu-gcc
  OUTPUT	= mipsel-pli4
  CFLAGS	= -s -ggdb3 -O2 $(OPTS) -DSTB -DEPOLL_NEWCAMD -DEPOLL_ECM
  LFLAGS	= $(CFLAGS)
else
ifeq ($(target),sparc)
  CC        = sparc-linux-gnu-gcc-4.7
  OUTPUT	= sparc
  CFLAGS	= -s -ggdb3 -O2 $(OPTS) -DSTB
  LFLAGS	= $(CFLAGS)
else
ifeq ($(target),sparc64)
  CC        = sparc64-linux-gnu-gcc
  OUTPUT	= sparc64
  CFLAGS	= -s -ggdb3 -O2 $(OPTS) -DSTB
  LFLAGS	= $(CFLAGS)
else
ifeq ($(target),sh4)
  CC        = /opt/STM/STLinux-2.4/devkit/sh4/bin/sh4-linux-gcc
  OUTPUT	= sh4
  CFLAGS	= -s -ggdb3 -O2 $(OPTS) -DST_7201 -DST_OSLINUX  -DARCHITECTURE_ST40 -DSTB -DEPOLL_NEWCAMD -DEPOLL_ECM
  LFLAGS	= $(CFLAGS)
else
ifeq ($(target),rpi)
  CC        = /opt/tools-master/arm-bcm2708/gcc-linaro-arm-linux-gnueabihf-raspbian/bin/arm-linux-gnueabihf-gcc
  OUTPUT	= rpi
  CFLAGS	= -s -ggdb3 -O2 $(OPTS) -DEPOLL_NEWCAMD -DEPOLL_ECM
  LFLAGS	= $(CFLAGS)
else

ifeq ($(target),arm-coolstream)
  CC        = /opt/arm-cx2450x-linux-gnueabi/bin/arm-cx2450x-linux-gnueabi-gcc
  OUTPUT	= arm-coolstream
  CFLAGS	= -s -ggdb3 -O2 $(OPTS) -DSTB -fPIC
  LFLAGS	= $(CFLAGS)
else
ifeq ($(target),mips-uclibc)
  CC        = /opt/cross-compiler-mips/bin/mips-gcc
  OUTPUT	= mips-uclibc
  CFLAGS	= -s -ggdb3 -O2 $(OPTS) -DSTB -fPIC
  LFLAGS	= $(CFLAGS)
else
ifeq ($(target),armeb)
  CC		= /opt/OpenWrt-SDK-ixp4xx-2.6-for-Linux-i686/staging_dir_armeb/bin/armeb-linux-uclibc-gcc
  OUTPUT	= armeb
  CFLAGS	= -s -ggdb3 -O2 $(OPTS) -DINOTIFY -DSTB
  LFLAGS	= $(CFLAGS)
else
ifeq ($(target),aarch64)
  CC		= aarch64-linux-gnu-gcc
  OUTPUT	= aarch64
  CFLAGS	= -s -ggdb3 -O2 $(OPTS) -DSTB -DEPOLL_NEWCAMD -DEPOLL_ECM
  LFLAGS	= $(CFLAGS)
else
ifeq ($(target),fritzbox)
  CC        = /opt/gcc-4.2.1-uClibc-0.9.29/mipsel-linux-uclibc/bin/mipsel-linux-uclibc-gcc-4.2.1
  OUTPUT	= fritzbox
  CFLAGS	= -s -ggdb3 -O2 $(OPTS) -DSTB
  LFLAGS	= $(CFLAGS)
else

ifeq ($(target),mips64)
  CC        = mips64-linux-gnu-gcc
  OUTPUT	= mips64
  CFLAGS	= -s -ggdb3 -O2 $(OPTS) -DSTB -DEPOLL_NEWCAMD -DEPOLL_ECM
  LFLAGS	= $(CFLAGS)
else
ifeq ($(target),win32)
  CC        = /usr/bin/i586-mingw32msvc-gcc
  OUTPUT	= win32
  CFLAGS	= -s -ggdb3 -O2 $(OPTS)
  LFLAGS	= $(CFLAGS) -D__USE_W32_SOCKETS -D_WIN32_WINDOWS=0x0501 -lws2_32 -lpthread --disable-stdcall-fixup -mno-cygwin
else
  CC        = gcc
  OUTPUT	= x
  CFLAGS	= -ggdb3 -O3 -m64 -Wall -I. -fpack-struct $(OPTS) -D_GNU_SOURCE # -DSIG_HANDLER ## -DRECVMSG_BLOCK ## -DICACHE # -DCACHEEX_CWCYCLE ##  ##  -D_FORTIFY_SOURCE=0 
  LFLAGS	= $(CFLAGS)
endif
endif
endif
endif
endif
endif
endif
endif
endif
endif
endif
endif
endif
endif
endif
endif
endif

ifndef name
	NAME	= multics
else
	AOUT	= $(name)
endif


# loginthrottle.o: TASK R3 (D57), the login door throttle
OBJECTS = $(OUTPUT)/sha1.o $(OUTPUT)/des.o $(OUTPUT)/md5.o $(OUTPUT)/aes.o \
	$(OUTPUT)/dcw.o $(OUTPUT)/convert.o $(OUTPUT)/tools.o $(OUTPUT)/debug.o $(OUTPUT)/parser.o $(OUTPUT)/ipdata.o \
	$(OUTPUT)/threads.o $(OUTPUT)/sockets.o $(OUTPUT)/msg-newcamd.o $(OUTPUT)/msg-cccam.o $(OUTPUT)/msg-radegast.o $(OUTPUT)/config.o \
	$(OUTPUT)/ecmdata.o $(OUTPUT)/httpserver.o $(OUTPUT)/telnet.o $(OUTPUT)/main.o \
	$(OUTPUT)/loginthrottle.o $(OUTPUT)/peerrep.o $(OUTPUT)/cwconsensus.o $(OUTPUT)/monjson.o

# TASK R6 (D60): the bounded cache queue compiles ONLY under CACHE_QUEUE
# (pass CACHE_QUEUE=1 to make). The default build's OBJECTS line is
# untouched -- the default binary stays byte-identical.
ifdef CACHE_QUEUE
OBJECTS += $(OUTPUT)/cachequeue.o
endif

# TASK R11 (D65): the per-CW verdict ring lives only where the counters
# live. The stats release recipes pass the MCS_DCWSTATS make variable
# alongside the -D flag (the CACHE_QUEUE=1 pattern); the default build
# and the queue-only flavour never link cwlog.o and stay byte-identical.
ifdef MCS_DCWSTATS
OBJECTS += $(OUTPUT)/cwlog.o
endif

$(OUTPUT):
	@mkdir -p $(OUTPUT)

link: $(OBJECTS)
	$(CC) -o $(OUTPUT)/$(NAME) $(LFLAGS) $(OBJECTS) -lpthread

$(OUTPUT)/%.o: ../src/%.c | $(OUTPUT)
	$(CC) -c $(CFLAGS) $< -o $@

$(OUTPUT)/httpserver.o: ../src/httpserver.h ../src/httpbuffer.c ../src/httpserver.c

$(OUTPUT)/main.o: ../src/main.c ../src/common.h ../src/httpserver.h ../src/config.h ../src/clustredcache.c ../src/cacheex.c ../src/pipe.c ../src/pipe.h \
	../src/th-ecm.c ../src/th-cfg.c ../src/th-dns.c ../src/th-date.c ../src/th-srv.c \
	../src/srv-cccam.c ../src/srv-newcamd.c ../src/srv-mgcamd.c ../src/srv-radegast.c ../src/srv-freecccam.c ../src/srv-camd35.c ../src/srv-cs378x.c \
	../src/cli-common.c ../src/cli-cccam.c ../src/cli-newcamd.c ../src/cli-radegast.c ../src/cli-camd35.c ../src/cli-cs378x.c

all:
	$(MAKE) target=ppc-old
	$(MAKE) target=ppc
	$(MAKE) target=mipsel
	$(MAKE) target=mipsel-pli4
	$(MAKE) target=sh4
	$(MAKE) target=sparc
	$(MAKE) target=arm-coolstream
	$(MAKE) target=rpi
	$(MAKE) target=fritzbox
	$(MAKE) target=armeb
	$(MAKE) target=aarch64

clean:
	-rm $(OUTPUT)/*

cleanall:
	$(MAKE) clean
	$(MAKE) target=x64 clean
	$(MAKE) target=x32 clean
	$(MAKE) target=ppc clean
	$(MAKE) target=mips clean
	$(MAKE) target=sh4 clean
	$(MAKE) target=armeb clean
	$(MAKE) target=armel clean
	$(MAKE) target=armhf clean
	$(MAKE) target=sparc clean
	$(MAKE) target=alpha clean
	-rm multics/*


