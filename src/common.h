#ifndef _COMMON_H_
#define _COMMON_H_

#ifdef CCCAM_SRV
#define CCCAM
#else
#ifdef CCCAM_CLI
#define CCCAM
#endif
#endif

#ifdef RADEGAST_SRV
#define RADEGAST
#else
#ifdef RADEGAST_CLI
#define RADEGAST
#endif
#endif

#define REVISION    82
#define REVISION_STR  "82"

#define FALSE 0
#define TRUE 1

#include <stddef.h>
/* Address of a member of a packed struct, formed without writing
   "&base->member" (which triggers -Waddress-of-packed-member under
   -fpack-struct). The value is bit-identical to &base->member; use it only
   where the member's alignment is irrelevant: opaque handles (pthread_t)
   and kernel int[2] pipes. Do not use it to build typed pointers that are
   later dereferenced as scalars -- copy by value instead. */
#define PACKED_MPTR(base, mtype, member) \
	((mtype *)(void *)((char *)(base) + offsetof(__typeof__(*(base)), member)))

#ifdef WIN32

#define pthread_t DWORD

#else

typedef int SOCKET;
#define INVALID_HANDLE_VALUE -1
#define INVALID_SOCKET       -1
#define SOCKET_ERROR         -1
#define closesocket          close

#endif


#define MAX_ECM_SIZE 700

struct message_data
{
	int len;
	unsigned char data[2048]; // max size for cccam servers/clients
};


#define MAX_PFD 1024*15
#define SERVER_MAX_PFD 1024*2
#define CCCAM_MAX_PFD 1024*10
#define MGCAMD_MAX_PFD 1024*10
#define NEWCAMD_MAX_PFD 512
#define CACHEEX_MAX_PFD 1024


#endif


