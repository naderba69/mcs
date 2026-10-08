# TASK 3.19 — best-effort cross build.
#
# This does not replace the upstream /opt toolchain paths in src/Makefile.
# Those branches still name compilers that are not in this image. Here the
# Debian cross compilers are used, with the same flags as make-x64 except
# -m64, which is x86_64-only.
#
#   make -C make-cross mipsel
#   make -C make-cross arm
#   make -C make-cross sh4
#   make -C make-cross all
#
# Output: ../bin/multics-r82a-<arch>
# Objects stay in this directory (cross-mipsel/, cross-arm/, cross-sh4/).

OPTS = -DCLI_CSCACHE -DSRV_CSCACHE -DCHECK_NEXTDCW -DSID_FILTER -DNEWCACHE \
	-DCCCAM_CLI -DRADEGAST_CLI -DCAMD35_CLI -DCS378X_CLI \
	-DHTTP_SRV -DTELNET -DMGCAMD_SRV -DCCCAM_SRV -DCAMD35_SRV -DCS378X_SRV \
	-DFREECCCAM_SRV -DEXPIREDATE -DDCWSWAP -DCACHEEX -DTHREAD_DCW \
	-DMONOTHREAD_ACCEPT -DDEBUG_NETWORK -DEPOLL_NEWCAMD -DEPOLL_ECM

PORTFLAGS = -include ../make-x64/port_gnu89.h -std=gnu89 -fcommon \
	-fno-strict-aliasing -Wno-implicit-function-declaration \
	-Wno-int-conversion -Wno-incompatible-pointer-types \
	-Wno-builtin-declaration-mismatch -Wno-stringop-overread \
	-Wno-stringop-truncation -Wno-format-truncation -Wno-unused-result \
	-Wno-pointer-sign -Wno-deprecated-declarations \
	-Wno-implicit-int -Wno-unused-but-set-variable -Wno-unused-variable \
	-Wno-implicit-fallthrough -Wno-dangling-pointer -Wno-array-bounds \
	-Wno-shift-count-overflow -Wno-format

CFLAGS = -O2 -I../src -I../make-x64 -fpack-struct $(PORTFLAGS) $(OPTS)
SRCS = sha1.c des.c md5.c aes.c dcw.c convert.c tools.c debug.c parser.c \
	ipdata.c threads.c sockets.c msg-newcamd.c msg-cccam.c msg-radegast.c \
	config.c ecmdata.c httpserver.c telnet.c main.c \
	loginthrottle.c peerrep.c cwconsensus.c monjson.c

.PHONY: all mipsel arm sh4

all: mipsel arm sh4

mipsel:
	$(MAKE) --no-print-directory arch ARCH=mipsel CC=mipsel-linux-gnu-gcc-14 STRIP=mipsel-linux-gnu-strip

arm:
	$(MAKE) --no-print-directory arch ARCH=arm CC=arm-linux-gnueabi-gcc-14 STRIP=arm-linux-gnueabi-strip

sh4:
	$(MAKE) --no-print-directory arch ARCH=sh4 CC=sh4-linux-gnu-gcc-14 STRIP=sh4-linux-gnu-strip

arch:
	@test -n "$(CC)" || { echo "CC required"; exit 1; }
	@mkdir -p cross-$(ARCH) ../bin
	@set -e; for s in $(SRCS); do \
	  echo "  $(ARCH) $$s"; \
	  $(CC) -c $(CFLAGS) ../src/$$s -o cross-$(ARCH)/$${s%.c}.o; \
	done
	$(CC) -o cross-$(ARCH)/multics $(CFLAGS) cross-$(ARCH)/*.o -pthread
	$(STRIP) -o ../bin/multics-r82a-$(ARCH) cross-$(ARCH)/multics
	@echo "built ../bin/multics-r82a-$(ARCH)"
	@file ../bin/multics-r82a-$(ARCH)
