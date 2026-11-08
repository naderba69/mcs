
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/select.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PIPE_WRITE_EAGAIN_TIMEOUT_MS 100u
#ifndef PIPE_BUF
#define PIPE_BUF 512
#endif

static uint64_t pipe_monotonic_ms(void)
{
	struct timespec now;
	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return UINT64_MAX;
	return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}
#include "pipe.h"

#define PIPE_SEND_LOCK_COUNT 64
static pthread_mutex_t pipe_send_locks[PIPE_SEND_LOCK_COUNT];
static pthread_once_t pipe_send_locks_once = PTHREAD_ONCE_INIT;
static int pipe_send_locks_error;

static void pipe_send_locks_init(void)
{
	int i, rc;
	for (i=0; i<PIPE_SEND_LOCK_COUNT; i++) {
		rc = pthread_mutex_init(&pipe_send_locks[i], NULL);
		if (rc != 0) {
			pipe_send_locks_error = rc;
			while (i > 0) pthread_mutex_destroy(&pipe_send_locks[--i]);
			return;
		}
	}
}

int frcc_pipe[2]; // newcamd server recv pipe

int dcwpipe[2];

// Complete the requested transfer: a successful read/write syscall may still
// report a short count (for example, when interrupted after partial progress).
int pipe_read( int fd, uint8_t *buf, int len )
{
	int total = 0;
	if (fd < 0 || (!buf && len > 0) || len < 0) return -1;
	while (total < len) {
		int readlen = read(fd, buf + total, (size_t)(len - total));
		if (readlen > 0) {
			total += readlen;
			continue;
		}
		if (readlen == 0) return total; /* EOF: let the frame parser reject short data. */
		if (errno == EINTR) continue;
		if (errno == EWOULDBLOCK || errno == EAGAIN) {
			struct pollfd pfd;
			int ready;
			pfd.fd = fd;
			pfd.events = POLLIN;
			pfd.revents = 0;
			do { ready = poll(&pfd, 1, -1); } while (ready < 0 && errno == EINTR);
			if (ready > 0 && (pfd.revents & POLLIN)) continue;
			if (ready > 0 && (pfd.revents & (POLLHUP | POLLERR | POLLNVAL))) return total;
			if (ready < 0) break;
			continue;
		}
		flag_debugfile = 1;
		debugf(getdbgflag(DBG_ERROR,0,0)," pipe(%d): read failed (errno=%d)\n", fd, errno);
		prg.restart = 1;
		return -1;
	}
	return total;
}

/* Return -1 when nothing was committed; preserve a positive short count when
 * an error follows a frame prefix so pipe_send can fail closed on corruption. */
int pipe_write( int fd, uint8_t *buf, int len )
{
	int total = 0;
	uint64_t retry_deadline = 0;
	if (fd < 0 || (!buf && len > 0) || len < 0) return -1;
	if (len == 0) return 0;
	while (total < len) {
		int writelen;
		if (retry_deadline) {
			uint64_t now = pipe_monotonic_ms();
			if (now == UINT64_MAX) { errno = EIO; prg.restart = 1; break; }
			if (now >= retry_deadline) { errno = ETIMEDOUT; break; }
		}
		writelen = write(fd, buf + total, (size_t)(len - total));
		if (writelen > 0) {
			total += writelen;
			retry_deadline = 0;
			continue;
		}
		if (writelen == 0) {
			errno = EIO;
			prg.restart = 1;
			return total > 0 ? total : -1;
		}
		if (errno == EINTR) continue;
		if (errno == EWOULDBLOCK || errno == EAGAIN) {
			struct pollfd pfd;
			uint64_t now = pipe_monotonic_ms();
			int wait_ms, ready;
			if (now == UINT64_MAX) { errno = EIO; prg.restart = 1; break; }
			if (!retry_deadline) retry_deadline = now + PIPE_WRITE_EAGAIN_TIMEOUT_MS;
			if (now >= retry_deadline) { errno = ETIMEDOUT; break; }
			wait_ms = (int)(retry_deadline - now);
			if (wait_ms > 25) wait_ms = 25;
			pfd.fd = fd;
			pfd.events = POLLOUT;
			pfd.revents = 0;
			ready = poll(&pfd, 1, wait_ms);
			if (ready < 0 && errno == EINTR) continue;
			if (ready < 0) { prg.restart = 1; break; }
			if (ready > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
				errno = EPIPE;
				prg.restart = 1;
				break;
			}
			continue;
		}
		flag_debugfile = 1;
		debugf(getdbgflag(DBG_ERROR,0,0)," pipe(%d): write failed (errno=%d)\n", fd, errno);
		prg.restart = 1;
		return total > 0 ? total : -1;
	}
	/* A zero-byte EAGAIN timeout is a failed send, not a partial frame.
	 * Preserve a positive count so pipe_send can fail closed if a prefix went out. */
	return total > 0 ? total : -1;
}

int pipe_purge( int fd )
{
	uint8_t rbuf[1024];
	while(1) {
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET( fd, &readfds);
		struct timeval timeout;
		timeout.tv_usec = 0;
		timeout.tv_sec = 0;
		int retval = select(fd+1, &readfds, NULL, NULL,&timeout);
		if ( retval>0 )	{
			retval = read( fd, rbuf, sizeof(rbuf) );
		}
		else break;
	}
	return 0;
}


// offset  size  desc
// 0       1     0xFF
// 1       1     Data CRC
// 2       2     Data Length
int pipe_recv( int fd, uint8_t *buf, size_t capacity )
{
	uint8_t rbuf[1024];
	int rlen;
	int max_payload = (int)sizeof(rbuf) - 4;

	if (max_payload > PIPE_BUF - 4) max_payload = PIPE_BUF - 4;
	if (fd < 0 || !buf || capacity == 0) return 0;
	rlen = pipe_read( fd, rbuf, 4);
	if (rlen!=4) {
		debugf(getdbgflag(DBG_ERROR,0,0)," pipe(%d): header recv error (rlen=%d)\n",fd,rlen);
		pipe_purge(fd); // wrong data --> purge
		return 0;
	}
	if (rbuf[0]!=0xFF) {
		debugf(getdbgflag(DBG_ERROR,0,0)," pipe(%d): recv error, wrong id %02X\n", fd,rbuf[0]);
		pipe_purge(fd); // wrong data --> purge
		return 0;
	}
	int len = (rbuf[2]<<8)|rbuf[3];
	if (len > max_payload) {
		debugf(getdbgflag(DBG_ERROR,0,0)," pipe(%d): payload too large (len=%d max=%d)\n", fd, len, max_payload);
		pipe_purge(fd);
		return 0;
	}
	rlen += pipe_read( fd, rbuf+4, len);
	if ( rlen!= len+4 ) {
		debugf(getdbgflag(DBG_ERROR,0,0), " pipe(%d): recv error (rlen=%d)\n",fd,rlen);
		pipe_purge(fd); // wrong data --> purge
		return 0;
	}
	/* Consume the entire frame before rejecting a too-small destination so
	 * the next read remains aligned at the following frame boundary. */
	if ((size_t)len > capacity) {
		debugf(getdbgflag(DBG_ERROR,0,0), " pipe(%d): destination too small (len=%d capacity=%zu)\n",
		       fd, len, capacity);
		return 0;
	}
	//Checksum
/*	int i;
	uint8_t sum =0;
	for(i=4; i<rlen; i++) sum ^= rbuf[i];
	if (sum!=rbuf[1]) {
		debugf(getdbgflag(DBG_ERROR,0,0)," pipe(%d): recv error, wrong checksum\n",fd);
		pipe_purge(fd); // wrong data --> purge
		return 0;
	}*/
	memcpy(buf, rbuf+4, (size_t)len);
	//debugf(getdbgflag(DBG_ERROR,0,0)," pipe(%d): recv data\n"); debughex(rbuf, rlen);
	return len;
}	

int pipe_send( int fd, uint8_t *buf, int len )
{
	uint8_t wbuf[1024];
	int wlen, written;
	unsigned lock_index;
	pthread_mutex_t *send_lock;
	/* A complete frame must fit in the platform's atomic pipe-write bound;
	 * then EAGAIN is guaranteed to mean that no frame bytes were committed. */
	if (fd < 0 || len < 0 || len > (int)sizeof(wbuf)-4 ||
	    len > PIPE_BUF - 4 || (!buf && len > 0)) {
		debugf(getdbgflag(DBG_ERROR,0,0)," pipe(%d): invalid frame length (%d)\n", fd, len);
		prg.restart = 1;
		return -1;
	}
	wlen = 4 + len;
	//ID (check for errors)
	wbuf[0] = 0xFF;
	//Checksum
/*	int i;
	uint8_t sum =0;
	for(i=0; i<len; i++) sum ^= buf[i];
	wbuf[1] = sum;*/
	//LENGTH
	wbuf[2] = (len>>8)&0x0f;
	wbuf[3] = len&0xff;
	// DATA
	if (len) memcpy(wbuf+4, buf, (size_t)len);
	//debugf(0," pipe(%d): write data\n",fd); debughex(buf, len);
	if (pthread_once(&pipe_send_locks_once, pipe_send_locks_init) != 0 || pipe_send_locks_error) {
		debugf(getdbgflag(DBG_ERROR,0,0)," pipe(%d): send-lock initialization failed\n", fd);
		prg.restart = 1;
		return -1;
	}
	lock_index = (unsigned)fd % PIPE_SEND_LOCK_COUNT;
	send_lock = &pipe_send_locks[lock_index];
	pthread_mutex_lock(send_lock);
	/* Fatal pipe errors set restart in pipe_write. A zero-byte EAGAIN timeout
	 * leaves the stream intact and is reported to the caller without treating
	 * it as a partial frame; any positive short count still fails closed below. */
	if (prg.restart) {
		pthread_mutex_unlock(send_lock);
		return -1;
	}
	written = pipe_write(fd, wbuf, wlen);
	/* Publish a committed frame prefix before releasing the lock so queued
	 * writers cannot append data to a stream that already has a partial frame. */
	if (written > 0 && written != wlen) prg.restart = 1;
	pthread_mutex_unlock(send_lock);
	if (written != wlen) {
		if (written >= 0)
			debugf(getdbgflag(DBG_ERROR,0,0)," pipe(%d): short frame write (%d/%d)\n", fd, written, wlen);
		return -1;
	}
	return written;
}

void pipe_cmd( int pfd, int cmd )
{
	uint8_t buf[2];
	buf[0] = cmd;
	buf[1] = 0;
	pipe_send( pfd, buf, 2);
}

void pipe_lock( int pfd )
{
	uint8_t buf[2];
	buf[0] = PIPE_LOCK;
	buf[1] = 0;
	pipe_send( pfd, buf, 2);
}

void pipe_wakeup( int pfd )
{
	uint8_t buf[2];
	buf[0] = PIPE_WAKEUP;
	buf[1] = 0;
	pipe_send( pfd, buf, 2);
}

void pipe_pointer( int pfd, int cmd, void *ptr )
{
	uint8_t buf[16];
	buf[0] = cmd;
	memcpy( buf+1, &ptr, sizeof(void*) );
	//debugf(0," pipe_pointer(%d): %p\n", pfd, ptr); debughex(buf, 1+sizeof(void*));
	pipe_send( pfd, buf, 1+sizeof(void*) );
}

void pipe_send_u32( int pfd, int cmd, uint32_t value )
{
	uint8_t buf[PIPE_CLIENT_ID_LEN];
	int len = pipe_encode_u32_message(buf, sizeof(buf), (uint8_t)cmd, value);
	if (len > 0) pipe_send(pfd, buf, len);
}

int pipe_send_server_connected(int pfd, uint32_t server_id,
                               uint64_t connection_generation)
{
	uint8_t buf[PIPE_SERVER_CONNECTED_LEN];
	int len = pipe_encode_server_connected(buf, sizeof(buf), server_id,
	                                      connection_generation);
	if (len <= 0) return -1;
	return pipe_send(pfd, buf, len);
}

int pipe_send_server_cscache_reply(int pfd, uint32_t server_id,
                                   uint64_t request_lifetime_id,
                                   uint16_t caid, uint32_t provid,
                                   uint16_t sid, uint8_t ecm_tag,
                                   const uint8_t cw[16])
{
	uint8_t buf[PIPE_SERVER_CSCACHE_REPLY_LEN];
	int len = pipe_encode_server_cscache_reply(buf, sizeof(buf), server_id,
	                                          request_lifetime_id, caid,
	                                          provid, sid, ecm_tag, cw);
	if (len <= 0) return -1;
	return pipe_send(pfd, buf, len);
}


