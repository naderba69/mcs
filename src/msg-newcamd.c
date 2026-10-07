#include "common.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>

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

#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <netdb.h> 
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <poll.h>

#endif

#include "des.h"
#include "debug.h"
#include "sockets.h"
#include "msg-newcamd.h"

int cs_message_send(int sock,struct cs_custom_data *cd, unsigned char *buffer, int len, unsigned char *deskey)
{
	unsigned char netbuf[CWS_NETMSGSIZE];

	if (sock==INVALID_SOCKET) return 0;

	if (!len) len = 0x0fff & ( (buffer[1]<<8)|buffer[2] );
	else {
		buffer[1] = (buffer[1] & 0xf0) | (((len - 3) >> 8) & 0x0f);
		buffer[2] = (len - 3) & 0xff;
	}

	if (len < 3 || len + 12 > CWS_NETMSGSIZE) return -1;

	//NetBuf Header

	memset(netbuf, 0, 12);
	if (cd) {
		netbuf[2] = (cd->msgid >> 8) & 0xff; 
		netbuf[3] = cd->msgid & 0xff;
		netbuf[4] = (cd->sid >> 8) & 0xff;
		netbuf[5] = cd->sid & 0xff;
		netbuf[6] = (cd->caid >> 8) & 0xff;
		netbuf[7] = cd->caid & 0xff;
		netbuf[8] = (cd->provid >> 16) & 0xff;
		netbuf[9] = (cd->provid >> 8) & 0xff;
		netbuf[10] = cd->provid & 0xff;
		netbuf[11] = (cd->provid >> 24) & 0xff; // used for mgcamd
	}
	memcpy(netbuf+12, buffer, len);
	// adding packet header size
	len += 12;

	//debugdump(netbuf,len,"SEND BUF ");
#ifdef DEBUG_NETWORK
	if (flag_debugnet) {
		debugf(0," newcamd: send data %d\n",len);
		debughex(netbuf,len);
	}
#endif
	len=des_encrypt(netbuf, len, deskey);

	if (len < 0) return -1;
	netbuf[0] = (len - 2) >> 8;
	netbuf[1] = (len - 2) & 0xff;

	return send_nonb(sock, netbuf, len,100);
}


// -2: timeout
// -1: Error
// =0: disconnect
// >0: ok
int cs_message_receive(int sock,struct cs_custom_data *cd, unsigned char *buffer, unsigned char *deskey, int timeout)
{
  int len;
  unsigned char netbuf[CWS_NETMSGSIZE];
  int returnLen;

  if (sock<=0) {
	//printf("newcamd: Invalid Socket\n");
	return -1;
  }

	len = recv_nonb(sock, netbuf, 2, timeout);

  if (len<=0) {
	//printf("newcamd: first recive error (%d)\n",errno);
	return len; // disconnected
  }
  if (len != 2) {
	//printf("newcamd: header length error (%d)\n",len);
	return -1;
  }
  if (((netbuf[0] << 8) | netbuf[1]) > CWS_NETMSGSIZE - 2) {
	//printf("newcamd: big message size (%d)\n", (netbuf[0] << 8) | netbuf[1] );
	return -1;
  }
  int netlen = (netbuf[0] << 8) | netbuf[1];

  // TEST LENGTH
  if (netlen>(CWS_NETMSGSIZE-2)) return -1;

	len = recv_nonb(sock, netbuf+2, netlen, timeout);

  if (len<=0) {
	//printf("newcamd: recive error\n");
	return len; // disconnected
  }
  if (len!=netlen) {
	//printf("newcamd: data length error\n");
	return -1;
  }
  len += 2;

  len= des_decrypt(netbuf, len, deskey);

  if (len < 15) {
	//printf("newcamd: Error des_decrypt length %d\n",len);
    //debug_dump(netbuf, len, "netbuf:");
	return -2;
  }

  if ( (returnLen = (((netbuf[13] & 0x0f) << 8) | netbuf[14]) + 3)  > len-12) {
	//printf("newcamd: wrong data length\n");
	return -1;
  }

  //debugdump(netbuf,returnLen+12,"RECV BUF ");
#ifdef DEBUG_NETWORK
	if (flag_debugnet) {
		debugf(0," newcamd: receive data %d\n",returnLen+12);
		debughex(netbuf,returnLen+12);
	}
#endif
	// Setup Custom Data
	if (cd) {
		cd->msgid = (netbuf[2] << 8) | netbuf[3];
		cd->sid = (netbuf[4] << 8) | netbuf[5];
		cd->caid = (netbuf[6] << 8) | netbuf[7];
		cd->provid = (netbuf[8] << 16) | (netbuf[9] << 8) | netbuf[10];
	}
  memcpy(buffer, netbuf+12, returnLen);
  return returnLen;
}


// -1: not yet
// 0: disconnect
// >0: ok
int cs_msg_chkrecv(int sock)
{
	int len;
	unsigned char netbuf[CWS_NETMSGSIZE];

	len = recv(sock, netbuf, 2, MSG_PEEK|MSG_NOSIGNAL|MSG_DONTWAIT);

	if (len==0) return 0;
	if (len!=2) return -1;

	int datasize = (netbuf[0] << 8) | netbuf[1];
	if ( datasize > CWS_NETMSGSIZE-2) return 0;

	len = recv(sock, netbuf, 2+datasize, MSG_PEEK|MSG_NOSIGNAL|MSG_DONTWAIT);

	if (len!=2+datasize) return -1;

	return len;
}


// -1: not yet
// 0: disconnect
// >0: ok
int cs_peekmsg( int handle, struct message_data *msg, uint8_t *sessionkey,struct cs_custom_data *cd, unsigned char *buffer)
{
	int len;
	if (msg->len<=0) {
		len = recv( handle, msg->data, 2, MSG_NOSIGNAL|MSG_DONTWAIT);
		if (len==0) return 0;
		if (len==-1) {
			if ( (errno==EINTR)||(errno==EWOULDBLOCK)||(errno==EAGAIN) ) return -1; 
			else return 0;
		}
		if (len>0) msg->len = len;
	}
	else if (msg->len==1) {
		len = recv( handle, msg->data+1, 1, MSG_NOSIGNAL|MSG_DONTWAIT);
		if (len==0) return 0;
		if (len==-1) {
			if ( (errno == EINTR)||(errno==EWOULDBLOCK)||(errno==EAGAIN) ) return -1; 
			else return 0;
		}
		if (len>0) msg->len +=len;
	}
	if (msg->len<2) return -1;
	// GOOD
	int datasize = 2 + ( (msg->data[0] << 8) | msg->data[1] );
	if ( datasize > CWS_NETMSGSIZE ) return 0;
	//
	if (datasize > msg->len) {
		len = recv( handle, msg->data+msg->len, datasize-msg->len, MSG_NOSIGNAL|MSG_DONTWAIT);
		//printf(" lennn = %d\n", len);
		if (len==0) return 0;
		if (len==-1) {
			if ( (errno==EINTR)||(errno==EWOULDBLOCK)||(errno==EAGAIN) ) return -1; 
			else return 0;
		}
		if (len>0) msg->len +=len;
	}
	//printf(" chk msg '%s' (%d) datasize:%d\n", cli->user, msg->len, datasize);
	if (datasize == msg->len) {
		len = des_decrypt( msg->data, msg->len, sessionkey);
		msg->len = 0;
		if (len < 15) return 0;
		int returnLen = (((msg->data[13] & 0x0f) << 8 ) | msg->data[14]) + 3;
		if ( returnLen > (len-12) ) return 0;
		// Setup Custom Data
		if (cd) {
			cd->msgid = (msg->data[2] << 8) | msg->data[3];
			cd->sid = (msg->data[4] << 8) | msg->data[5];
			cd->caid = (msg->data[6] << 8) | msg->data[7];
			cd->provid = (msg->data[8] << 16) | (msg->data[9] << 8) | msg->data[10];
		}
		memcpy(buffer, msg->data+12, returnLen);
		return returnLen;
	}
	return -1;
}


