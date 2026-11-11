#include "common.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <pthread.h>

#ifdef WIN32

#include <windows.h>
#include <sys/types.h>
#include <sys/_default_fcntl.h>
#include <sys/poll.h>
#include <cygwin/types.h>
#include <cygwin/socket.h>
#include <sys/errno.h>
#include <cygwin/in.h>
#include <sched.h>
#include <netdb.h>
#include <netinet/tcp.h>

#else

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <errno.h>
#include <poll.h>

#endif

#include "debug.h"
#include "msg-cccam.h"
#include "sockets.h"

/* CCcam's stream cipher mutates sendblock for every byte. Serialize the
 * entire encrypt-and-send operation for each shared sendblock, not merely the
 * encryption call; otherwise two packets can reach the socket out of order. */
#define CC_SEND_LOCK_COUNT 64
static pthread_mutex_t cc_send_locks[CC_SEND_LOCK_COUNT];
static pthread_once_t cc_send_locks_once = PTHREAD_ONCE_INIT;
static int cc_send_locks_error;

static void cc_send_locks_init(void)
{
	unsigned int i;
	for (i = 0; i < CC_SEND_LOCK_COUNT; i++) {
		int rc = pthread_mutex_init(&cc_send_locks[i], NULL);
		if (rc != 0) {
			while (i > 0) pthread_mutex_destroy(&cc_send_locks[--i]);
			cc_send_locks_error = rc;
			return;
		}
	}
}

static pthread_mutex_t *cc_send_lock_for_block(struct cc_crypt_block *block)
{
	uintptr_t key = (uintptr_t)block;
	if (!block || pthread_once(&cc_send_locks_once, cc_send_locks_init) != 0 ||
	    cc_send_locks_error)
		return NULL;
	return &cc_send_locks[(key >> 4) % CC_SEND_LOCK_COUNT];
}

///////////////////////////////////////////////////////////////////////////////
inline void cc_crypt_swap(unsigned char *p1, unsigned char *p2)
{
	register unsigned char tmp=*p1;
	*p1=*p2; *p2=tmp;
}

///////////////////////////////////////////////////////////////////////////////
void cc_crypt_init( struct cc_crypt_block *block, uint8_t *key, int len)
{
  register int i;
  for (i=0; i<256; i++) block->keytable[i] = i;

  register uint8_t j = 0;
  for (i=0; i<256; i++) {
    j += key[i % len] + block->keytable[i];
    cc_crypt_swap(&block->keytable[i], &block->keytable[j]);
  }

  block->state = *key;
  block->counter=0;
  block->sum=0;
}

///////////////////////////////////////////////////////////////////////////////
// XOR init bytes with 'CCcam'
void cc_crypt_xor(uint8_t *buf)
{
	const char cccam[] = "CCcam";

    buf[8+0] = 0 * buf[0]; buf[0] ^= cccam[0];
    buf[8+1] = 1 * buf[1]; buf[1] ^= cccam[1];
    buf[8+2] = 2 * buf[2]; buf[2] ^= cccam[2];
    buf[8+3] = 3 * buf[3]; buf[3] ^= cccam[3];
    buf[8+4] = 4 * buf[4]; buf[4] ^= cccam[4];
    buf[8+5] = 5 * buf[5]; buf[5] ^= cccam[5];
    buf[8+6] = 6 * buf[6];
    buf[8+7] = 7 * buf[7];
}

/*
// XOR init bytes with 'CCcam'
void cc_crypt_xor(uint8_t *buf)
{
  const char cccam[] = "CCcam";
  register unsigned int i;

  for ( i = 0; i < 8; i++ ) {
    buf[8 + i] = i * buf[i];
    if ( i <= 5 ) {
      buf[i] ^= cccam[i];
    }
  }
}
*/

///////////////////////////////////////////////////////////////////////////////
__inline void cc_decrypt(struct cc_crypt_block *block, uint8_t *data, int len)
{
  register int i;
  uint8_t z;

  for (i = 0; i < len; i++) {
    block->counter++;
    block->sum += block->keytable[block->counter];
    cc_crypt_swap(&block->keytable[block->counter], &block->keytable[block->sum]);
    z = data[i];
    data[i] = z ^ block->keytable[(block->keytable[block->counter] + block->keytable[block->sum]) & 0xff] ^ block->state;
    z = data[i];
    block->state = block->state ^ z;
  }
}

///////////////////////////////////////////////////////////////////////////////
__inline void cc_encrypt(struct cc_crypt_block *block, uint8_t *data, int len)
{
  register int i;
  uint8_t z;
  // There is a side-effect in this function:
  // If in & out pointer are the same, then state is xor'ed with modified input
  // (because output(=in ptr) is written before state xor)
  // This side-effect is used when initialising the encrypt state!
  for (i = 0; i < len; i++) {
    block->counter++;
    block->sum += block->keytable[block->counter];
    cc_crypt_swap(&block->keytable[block->counter], &block->keytable[block->sum]);
    z = data[i];
    data[i] = z ^ block->keytable[(block->keytable[block->counter] + block->keytable[block->sum]) & 0xff] ^ block->state;
    block->state = block->state ^ z;
  }
}

///////////////////////////////////////////////////////////////////////////////
// node_id : client nodeid, the sender of the ECM Request(big endian)
// card_id : local card_id for the server
void cc_crypt_cw(uint8_t *nodeid/*client node id*/, uint32_t card_id, uint8_t *cws)
{
	uint8_t tmp;
	register int i;
	register int n;
	uint8_t nod[8];

	for(i=0; i<8; i++) nod[i] = nodeid[7-i];
	for (i = 0; i < 16; i++) {
		if (i&1)
			if (i!=15) n = (nod[i>>1]>>4) | (nod[(i>>1)+1]<<4); else n = nod[i>>1]>>4;
		else n = nod[i>>1];
		n = n & 0xff;
		tmp = cws[i] ^ n;
		if (i & 1) tmp = ~tmp;
		cws[i] = (card_id >> (2 * i)) ^ tmp;
		//printf("(%d) n=%02x, tmp=%02x, cw=%02x\n",i,n,tmp,cws[i]); 
	}
}



///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
// Return:
// =0: Disconnected
// -1: Packet Error
// >0: Success


int cc_msg_recv(int handle,struct cc_crypt_block *recvblock, uint8_t *buf, int timeout)
{
	int len;
	uint8_t netbuf[CC_MAXMSGSIZE];

	if (handle < 0) return -1;

	len = recv_nonb(handle, netbuf, 4, timeout);

	if (len<=0) {
		debugf( getdbgflag(DBG_ERROR,0,0), " CCcam: recv error %d(%d)\n",len, errno);
		return len;
	}

	if (len != 4) { // invalid header length read
		debugf( getdbgflag(DBG_ERROR,0,0), " CCcam: invalid header length\n");
		//debugdump(netbuf, len, "Header:");
		return -1;
	}

	cc_decrypt(recvblock, netbuf, 4);
	//debugdump(netbuf, 4, "CCcam: decrypted header:");

	if (((netbuf[2] << 8) | netbuf[3]) != 0) {  // check if any data is expected in msg
		if (((netbuf[2] << 8) | netbuf[3]) > CC_MAXMSGSIZE - 2) {
			debugf( getdbgflag(DBG_ERROR,0,0), " CCcam: message too big\n");
			return -1;
		}

		len = recv_nonb(handle, netbuf+4, (netbuf[2] << 8) | netbuf[3], timeout);

		if (len != ((netbuf[2] << 8) | netbuf[3])) {
			debugf( getdbgflag(DBG_ERROR,0,0), " CCcam: invalid message length read %d(%d)\n",len,errno);
			return -1;
		}

		cc_decrypt(recvblock, netbuf+4, len);
		len += 4;
	}

	//debugdump(netbuf, len, "CCcam: Reveive Data");
#ifdef DEBUG_NETWORK
	if (flag_debugnet) {
		debugf( getdbgflag(DBG_CCCAM,0,0), " CCcam: receive data %d\n",len);
		debughex(netbuf,len);
	}
#endif
	memcpy(buf, netbuf, len);
	return len;
}




// -1: not yet
// 0: disconnect
// >0: ok
int cc_msg_chkrecv(int handle,struct cc_crypt_block *recvblock)
{
	int len;
	uint8_t netbuf[CC_MAXMSGSIZE];
	struct cc_crypt_block block;

	if (handle<=0) return -1;

	//len = recv(handle, netbuf, 4, 0);
	len = recv(handle, netbuf, 4, MSG_PEEK|MSG_NOSIGNAL|MSG_DONTWAIT);
	if (len==0) return 0;
	if (len!=4) return -1;

	memcpy( &block, recvblock, sizeof(struct cc_crypt_block));
	cc_decrypt(&block, netbuf, 4);

	int datasize = (netbuf[2] << 8) | netbuf[3];
	if ( datasize!=0 ) {  // check if any data is expected in msg
		if ( datasize > CC_MAXMSGSIZE - 2) return 0; // Disconnect
		len = recv(handle, netbuf, 4+datasize, MSG_PEEK|MSG_NOSIGNAL|MSG_DONTWAIT);
		if (len==0) return 0;
		if (len != 4+datasize) return -1;
		cc_decrypt(&block, netbuf+4, len-4);
	}
	//debugf( getdbgflag(DBG_CCCAM,0,0), "CCcam: Check Reveive Data %d\n",datasize+4);
	return len;
}

///////////////////////////////////////////////////////////////////////////////
// Return:
// =0: Disconnected
// -1: Packet Error
// >0: Success
int cc_msg_recv_nohead(int handle, struct cc_crypt_block *recvblock, uint8_t *buf, int len)
{
	if (handle < 0) return -1;
	len = recv_nonb(handle, buf, len, 2000);  // read rest of msg
	cc_decrypt(recvblock, buf, len);
	return len;
}

///////////////////////////////////////////////////////////////////////////////
static int cc_msg_send_locked(int handle, struct cc_crypt_block *sendblock,
                              cc_msg_cmd cmd, int len, uint8_t *buf)
{
	uint8_t netbuf[CC_MAXMSGSIZE];
	int result;
	if (handle < 0 || !sendblock || len < 0 ||
	    (cmd == CC_MSG_NO_HEADER ? len > CC_MAXMSGSIZE : len > CC_MAXMSGSIZE - 4) ||
	    (cmd == CC_MSG_NO_HEADER && len > 0 && !buf))
		return 0;
	memset(netbuf, 0, cmd == CC_MSG_NO_HEADER ? len : len + 4);
	if (cmd == CC_MSG_NO_HEADER) memcpy(netbuf, buf, len);
	else {
		// build command message
		netbuf[0] = 0;   // flags??
		netbuf[1] = cmd & 0xff;
		netbuf[2] = len >> 8;
		netbuf[3] = len & 0xff;
		if (buf) memcpy(netbuf+4, buf, len);
		len += 4;
	}
	//debugdump(netbuf, len, "CCcam: Send data");
#ifdef DEBUG_NETWORK
	if (flag_debugnet) {
		debugf( getdbgflag(DBG_CCCAM,0,0), " CCcam: send data %d\n",len);
		debughex(netbuf,len);
	}
#endif
	cc_encrypt(sendblock, netbuf, len);
	result = send_nonb(handle, netbuf, len, 100);
	return result;
}

/* Keep CCcam's immediate post-packet cipher transition inside the same
 * per-stream critical section as encryption and the socket write. CW replies
 * need an additional 16-byte state advance; a separate cc_encrypt() after
 * cc_msg_send() would allow another writer to interleave its packet. */
static int cc_msg_send_transition(int handle, struct cc_crypt_block *sendblock,
                                  cc_msg_cmd cmd, int len, uint8_t *buf,
                                  uint8_t *followup, int followup_len)
{
	pthread_mutex_t *send_lock = cc_send_lock_for_block(sendblock);
	int result;
	if (handle < 0 || !send_lock || followup_len < 0 ||
	    followup_len > CC_MAXMSGSIZE || (followup_len && !followup))
		return 0;
	if (pthread_mutex_lock(send_lock) != 0) return 0;
	result = cc_msg_send_locked(handle, sendblock, cmd, len, buf);
	if (result && followup_len) cc_encrypt(sendblock, followup, followup_len);
	pthread_mutex_unlock(send_lock);
	return result;
}

int cc_msg_send(int handle, struct cc_crypt_block *sendblock,
                cc_msg_cmd cmd, int len, uint8_t *buf)
{
	return cc_msg_send_transition(handle, sendblock, cmd, len, buf, NULL, 0);
}

int cc_msg_send_and_encrypt(int handle, struct cc_crypt_block *sendblock,
                            cc_msg_cmd cmd, int len, uint8_t *buf,
                            uint8_t *followup, int followup_len)
{
	if (!followup || followup_len <= 0) return 0;
	return cc_msg_send_transition(handle, sendblock, cmd, len, buf,
	                              followup, followup_len);
}


/* Keep partial encrypted frame bytes in msg->data across readiness events.
 * Advance the stream cipher only after a complete header/body segment is read. */
int cc_peekmsg(int handle,struct cc_crypt_block *recvblock, struct message_data *msg, uint8_t *buf)
{
	int len;
	if (handle<0) return 0;
	if (msg->len<4) {
		len = recv(handle, msg->data+msg->len, 4-msg->len, MSG_NOSIGNAL|MSG_DONTWAIT);
		if (len==0) return 0;
		if (len==-1) {
			if ( (errno==EINTR)||(errno==EWOULDBLOCK)||(errno==EAGAIN) ) return -1;
			else return 0;
		}
		if (len>0) msg->len += len;
		if (msg->len==4) cc_decrypt(recvblock, msg->data, 4);
	}
	if (msg->len<4) return -1;
	int datasize = 4 + ((msg->data[2] << 8) | msg->data[3]);
	if (datasize > 4) {  // check if any data is expected in msg
		if (datasize >= CC_MAXMSGSIZE) return 0;
		len = recv(handle, msg->data+msg->len, datasize-msg->len, MSG_NOSIGNAL|MSG_DONTWAIT);
		if (len==0) return 0;
		if (len==-1) {
			if ( (errno==EINTR)||(errno==EWOULDBLOCK)||(errno==EAGAIN) ) return -1;
			else return 0;
		}
		if (len>0) msg->len += len;
		if (msg->len<datasize) return -1;
		cc_decrypt(recvblock, msg->data+4, datasize-4);
	}
	memcpy(buf, msg->data, datasize);
	msg->len = 0;
	return datasize;
}


