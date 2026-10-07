/*
 * port_gnu89.h -- modern toolchain compatibility shim for MultiCS r82a
 *
 * Injected into every translation unit with:  -include port_gnu89.h
 * No upstream .c/.h file is modified by this header alone.
 *
 * Why it is needed (verified on gcc 14.2 / Debian):
 *  1. GCC 14 turned `-Werror=implicit-function-declaration` and
 *     `-Werror=int-conversion` ON by default. r82a was written for GCC 4.x
 *     where those were warnings. Verified real errors:
 *       ../config.c:2329: error: implicit declaration of function 'strptime'
 *       ../ecmdata.c:206: error: implicit declaration of function 'malloc'
 *       ../ecmdata.c:264: error: implicit declaration of function 'MD5'
 *  2. Modern glibc hides strptime() unless an XSI feature macro is set;
 *     config.c defines only _GNU_SOURCE.
 *  3. GCC 10 made -fno-common the default; r82a relies on tentative
 *     definitions merging into one common symbol across files (handled by
 *     -fcommon in the Makefile, listed here for completeness).
 */
#ifndef MCS_PORT_GNU89_H
#define MCS_PORT_GNU89_H

/* (2) expose strptime() and friends on modern glibc */
#define _XOPEN_SOURCE   700
#define _DEFAULT_SOURCE 1

/* (1) provide the declarations the upstream sources forgot to include */
#include <stdlib.h>   /* malloc/free  -- ecmdata.c:206 uses them bare */
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>     /* strptime -- config.c:2329                    */
#include <unistd.h>

#include "md5.h"      /* MD5() -- ecmdata.c:264 calls it bare         */

#endif /* MCS_PORT_GNU89_H */
