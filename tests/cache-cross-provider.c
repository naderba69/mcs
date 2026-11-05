/* Execute the production cache walkers with a captured ECM pipe. */
#define main multics_unused_main
#include "../src/main.c"
#undef main
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <poll.h>
#include <time.h>

static int sends, conflicts, source_conflicts, profile_checks, record_conflicts;
static uint32_t last_profile_provider;
static int force_pipe_poll_eintr;
static unsigned pipe_poll_eintr_calls;
static int force_send_eagain;
static int force_send_nonb_poll_eintr;
static unsigned send_nonb_poll_eintr_calls;
static struct cardserver_data *last_profile;
static struct cardserver_data cache_test_profile;
static int fail_pipe_monotonic_clock;
static int reject_profile_cw;
static unsigned pipe_clock_calls;
struct cafamily_tab cafamily_tab;
int __real_clock_gettime(clockid_t clock_id, struct timespec *value);
int __wrap_clock_gettime(clockid_t clock_id, struct timespec *value)
{
    if (clock_id == CLOCK_MONOTONIC) pipe_clock_calls++;
    if (fail_pipe_monotonic_clock && clock_id == CLOCK_MONOTONIC) {
        errno = EIO;
        return -1;
    }
    return __real_clock_gettime(clock_id, value);
}
ssize_t __real_send(int sock, const void *buf, size_t len, int flags);
ssize_t __wrap_send(int sock, const void *buf, size_t len, int flags)
{
    if (force_send_eagain) {
        force_send_eagain = 0;
        errno = EAGAIN;
        return -1;
    }
    return __real_send(sock, buf, len, flags);
}
int __real_poll(struct pollfd *fds, nfds_t nfds, int timeout);
int __wrap_poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    if (force_send_nonb_poll_eintr && nfds == 1 && (fds[0].events & POLLOUT)) {
        force_send_nonb_poll_eintr = 0;
        send_nonb_poll_eintr_calls++;
        fds[0].revents = POLLHUP; /* Undefined after EINTR; must be ignored. */
        errno = EINTR;
        return -1;
    }
    if (force_pipe_poll_eintr && nfds == 1 && (fds[0].events & POLLOUT)) {
        struct timespec delay = {0, 10000000};
        pipe_poll_eintr_calls++;
        nanosleep(&delay, NULL);
        errno = EINTR;
        return -1;
    }
    return __real_poll(fds, nfds, timeout);
}
uint32_t GetTickCount(void) { return 10000; }
uint32_t getdbgflag(int a,int b,int c) { (void)a;(void)b;(void)c;return 0; }
void debugf(uint32_t flag, char *fmt, ...) { (void)flag;(void)fmt; }
int acceptDCW_profile_ecm_trace_reason(uint8_t *cw, struct cardserver_data *cs,
                                       uint16_t caid, uint32_t provid, uint16_t sid,
                                       int *reason, int *exemption)
{ (void)cw;(void)caid;(void)sid; profile_checks++; last_profile=cs; last_profile_provider=provid;
  if (reason) *reason=reject_profile_cw ? DCW_INVALID_CHECKSUM : DCW_VALID;
  if (exemption) *exemption=0; return !reject_profile_cw; }
int checksumDCW(uint8_t *cw) { (void)cw; return 1; }
int isbadDCW(uint8_t *cw) { (void)cw; return 0; }
int dcw_profile_cycle_enabled(struct cardserver_data *cs) { (void)cs; return 0; }
int peerrep_note(uint32_t ip,uint16_t port,const char *reason) { (void)ip;(void)port;(void)reason;return 0; }
void peerrep_save(void) {}
int peerrep_stage(uint32_t ip,uint16_t port) { (void)ip;(void)port;return 0; }
int peerrep_ask_ok(uint32_t ip,uint16_t port) { (void)ip;(void)port;return 1; }
void cwconsensus_set_min_sources(int n) { (void)n; }
int cwconsensus_verdict(uint16_t caid,uint16_t sid,uint32_t provid,int provider_known,
                        uint32_t hash,uint8_t tag,const uint8_t *cw,int peerid,int weight,
                        uint32_t now,uint32_t window,const uint8_t **loser)
{ (void)caid;(void)sid;(void)provid;(void)provider_known;(void)hash;(void)tag;(void)cw;
  (void)peerid;(void)weight;(void)now;(void)window;
  if (loser) *loser=NULL; return CWC_DELIVER; }
int cwconsensus_vote_counts(uint16_t caid,uint16_t sid,uint32_t provid,int provider_known,
                            uint32_t hash,uint8_t tag,const uint8_t *cw,uint32_t now,
                            uint32_t window,int *distinct,int *weight)
{ (void)caid;(void)sid;(void)provid;(void)provider_known;(void)hash;(void)tag;
  (void)cw;(void)now;(void)window;
  if (distinct) *distinct=0; if (weight) *weight=0; return 0; }
int cwconsensus_is_held(uint16_t caid,uint16_t sid,uint32_t provid,int provider_known,
                        uint32_t hash,uint8_t tag,const uint8_t *cw)
{ (void)caid;(void)sid;(void)provid;(void)provider_known;(void)hash;(void)tag;(void)cw;return 0; }
void cwconsensus_capacity(uint32_t now,uint32_t window,struct cwconsensus_capacity *out)
{ (void)now;(void)window;memset(out,0,sizeof(*out)); }
void cwconsensus_hold_cycle(uint16_t caid,uint16_t sid,uint32_t provid,int provider_known,
                            uint32_t hash,uint8_t tag,const uint8_t *cw,int peerid,int cycle)
{ (void)caid;(void)sid;(void)provid;(void)provider_known;(void)hash;(void)tag;
  (void)cw;(void)peerid;(void)cycle; }
void user_trace_cache_observation(uint16_t a,uint32_t b,uint16_t c,uint32_t d,int e,const char *f,const uint8_t g[16])
{ (void)a;(void)b;(void)c;(void)d;(void)e;
  if (!strcmp(f,"cache forwarded CW to ECM pipe; delivery not confirmed")) sends++;
  if (!strcmp(f,"cache provider collision: different local provider; conflicting operation skipped")) { assert(g == NULL); conflicts++; }
  if (!strcmp(f,"cache provider collision: explicit cached provider differs from local request; conflicting operation skipped")) { assert(g == NULL); conflicts++; }
  if (!strcmp(f,"cache provider collision: known numeric providers differ; cross-cache relay skipped")) { assert(g == NULL); record_conflicts++; }
  if (!strcmp(f,"cache provider collision: explicit incoming provider conflicts with known cache/local provider; CW refused")) { source_conflicts++; }
}
int isnullDCW(uint8_t *cw) { uint8_t z[16]={0}; return !memcmp(cw,z,16); }
int similarcw(uint8_t *a,uint8_t *b) { int i, n=0; for(i=0;i<8;i++) if(a[i]==b[i])n++; return n>3; }

struct split_pipe_writer { int fd; };
static void *write_pipe_in_chunks(void *arg)
{
    struct split_pipe_writer *writer=(struct split_pipe_writer *)arg;
    static const uint8_t bytes[4]={0x31,0x42,0x53,0x64};
    struct timespec delay={0,10000000};
    int i;
    for (i=0; i<4; i++) {
        assert(write(writer->fd,&bytes[i],1)==1);
        nanosleep(&delay,NULL);
    }
    return NULL;
}

static void test_send_nonb_poll_eintr(void)
{
    int fds[2];
    uint8_t message[3] = {0x71, 0x82, 0x93};
    uint8_t received[sizeof(message)];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    force_send_eagain = 1;
    force_send_nonb_poll_eintr = 1;
    send_nonb_poll_eintr_calls = 0;
    assert(send_nonb(fds[0], message, sizeof(message), 100));
    assert(!force_send_eagain && !force_send_nonb_poll_eintr);
    assert(send_nonb_poll_eintr_calls == 1);
    assert(recv(fds[1], received, sizeof(received), 0) == (ssize_t)sizeof(received));
    assert(!memcmp(received, message, sizeof(message)));
    close(fds[0]);
    close(fds[1]);
}

struct pipe_io_writer {
    int fd;
    uint8_t *bytes;
    int len;
    int result;
};
static void *write_pipe_all(void *arg)
{
    struct pipe_io_writer *writer=(struct pipe_io_writer *)arg;
    writer->result=pipe_write(writer->fd,writer->bytes,writer->len);
    return NULL;
}

int main(void)
{
    test_send_nonb_poll_eintr();
    assert(pthread_mutex_init(&prg.lockcache,NULL)==0);
    struct cache_data **table = getcachetabbycaid(0x1884);
    struct cache_data target = {0}, incoming = {0};
    ECM_DATA relay_waiter={0};
    uint8_t cw[16];
    assert(table);
    assert(offsetof(ECM_DATA,lifetime_id)%8==0 && __alignof__(ECM_DATA)>=8);
    /* The serializer is a lockecm-side helper, but its public contract must
     * still reject absent input before attempting an atomic generation load. */
    {
        uint8_t wire[64];
        assert(put_ecm2cache(PIPE_CACHE_FIND, wire, sizeof(wire), NULL, 0, NULL)==0);
        assert(put_ecm2cache(PIPE_CACHE_FIND, NULL, 0, &relay_waiter, 0, NULL)==0);
    }
    /* pipe_recv must reject a header that would overrun its 1024-byte frame. */
    {
        int fds[2];
        uint8_t header[4]={0xff,0,0x03,0xfd}; /* 1021-byte payload */
        uint8_t pipe_buf[1024];
        assert(pipe(fds)==0);
        assert(write(fds[1],header,sizeof(header))==(ssize_t)sizeof(header));
        assert(pipe_recv(fds[0],pipe_buf,sizeof(pipe_buf))==0);
        close(fds[0]); close(fds[1]);
    }
    /* A pipe reader must accumulate short reads instead of dropping the tail
     * of a valid frame when the writer is scheduled between byte writes. */
    {
        int fds[2], readlen;
        pthread_t writer_thread;
        struct split_pipe_writer writer;
        uint8_t got[4], expected[4]={0x31,0x42,0x53,0x64};
        assert(pipe(fds)==0);
        writer.fd=fds[1];
        assert(pthread_create(&writer_thread,NULL,write_pipe_in_chunks,&writer)==0);
        readlen=pipe_read(fds[0],got,sizeof(got));
        assert(readlen==sizeof(got) && !memcmp(got,expected,sizeof(got)));
        assert(pthread_join(writer_thread,NULL)==0);
        close(fds[0]); close(fds[1]);
    }
    /* Exercise a nonblocking writer against a pipe whose reader drains in
     * smaller chunks, forcing EAGAIN/retry while verifying the entire payload. */
    {
        const int io_len=131072;
        int fds[2], flags, received=0, n, i;
        uint8_t *expected=(uint8_t *)malloc((size_t)io_len);
        uint8_t *actual=(uint8_t *)malloc((size_t)io_len);
        pthread_t writer_thread;
        struct pipe_io_writer writer;
        assert(expected && actual && pipe(fds)==0);
        for (i=0; i<io_len; i++) expected[i]=(uint8_t)((i*37u+11u)&0xffu);
        flags=fcntl(fds[1],F_GETFL,0);
        assert(flags>=0 && fcntl(fds[1],F_SETFL,flags|O_NONBLOCK)==0);
        writer.fd=fds[1]; writer.bytes=expected; writer.len=io_len; writer.result=-999;
        assert(pthread_create(&writer_thread,NULL,write_pipe_all,&writer)==0);
        while (received<io_len) {
            int want=io_len-received;
            if (want>333) want=333;
            n=(int)read(fds[0],actual+received,(size_t)want);
            if (n<0 && errno==EINTR) continue;
            assert(n>0);
            received+=n;
        }
        assert(pthread_join(writer_thread,NULL)==0);
        assert(writer.result==io_len && !memcmp(expected,actual,(size_t)io_len));
        close(fds[0]); close(fds[1]);
        free(expected); free(actual);
    }
    /* A full nonblocking pipe must not leave pipe_send spinning forever.
     * Exercise the cache-lock case explicitly: the caller currently holds
     * lockcache during this bounded EAGAIN wait, then takes the restart path. */
    {
        int fds[2], flags;
        uint8_t fill[4096] = {0}, message[3]={0x11,0x22,0x33};
        struct timespec start, end;
        double elapsed_ms;
        ssize_t n;
        assert(pipe(fds)==0);
        flags=fcntl(fds[1],F_GETFL,0);
        assert(flags>=0 && fcntl(fds[1],F_SETFL,flags|O_NONBLOCK)==0);
        do { n=write(fds[1],fill,sizeof(fill)); } while (n>0);
        assert(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK));
        prg.restart=0;
        pthread_mutex_lock(&prg.lockcache);
        assert(clock_gettime(CLOCK_MONOTONIC,&start)==0);
        assert(pipe_send(fds[1],message,sizeof(message))==-1);
        assert(clock_gettime(CLOCK_MONOTONIC,&end)==0);
        elapsed_ms=(end.tv_sec-start.tv_sec)*1000.0+
                   (end.tv_nsec-start.tv_nsec)/1000000.0;
        pthread_mutex_unlock(&prg.lockcache);
        assert(elapsed_ms < 500.0 && prg.restart);
        prg.restart=0;
        close(fds[0]); close(fds[1]);
    }
    /* If the monotonic source fails while a full pipe requires EAGAIN retry,
     * pipe_send must take its bounded fail-closed path and request restart. */
    {
        int fds[2], flags;
        uint8_t fill[4096] = {0}, message[3]={0x11,0x22,0x33};
        ssize_t n;
        assert(pipe(fds)==0);
        flags=fcntl(fds[1],F_GETFL,0);
        assert(flags>=0 && fcntl(fds[1],F_SETFL,flags|O_NONBLOCK)==0);
        do { n=write(fds[1],fill,sizeof(fill)); } while (n>0);
        assert(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK));
        prg.restart=0;
        errno=0;
        fail_pipe_monotonic_clock=1;
        assert(pipe_send(fds[1],message,sizeof(message))==-1);
        fail_pipe_monotonic_clock=0;
        assert(errno==EIO && prg.restart);
        /* Once restart is scheduled, do not repeat bounded waits for each
         * remaining candidate while the cache worker still owns lockcache. */
        pipe_clock_calls=0;
        assert(pipe_send(fds[1],message,sizeof(message))==-1);
        assert(pipe_clock_calls==0 && prg.restart);
        prg.restart=0;
        close(fds[0]); close(fds[1]);
    }
    /* Repeated poll interruptions after EAGAIN still have to observe the
     * monotonic retry deadline instead of extending the cache-lock hold. */
    {
        int fds[2], flags;
        uint8_t fill[4096] = {0}, message[3]={0x11,0x22,0x33};
        ssize_t n;
        assert(pipe(fds)==0);
        flags=fcntl(fds[1],F_GETFL,0);
        assert(flags>=0 && fcntl(fds[1],F_SETFL,flags|O_NONBLOCK)==0);
        do { n=write(fds[1],fill,sizeof(fill)); } while (n>0);
        assert(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK));
        prg.restart=0;
        pipe_poll_eintr_calls=0;
        force_pipe_poll_eintr=1;
        errno=0;
        assert(pipe_send(fds[1],message,sizeof(message))==-1);
        force_pipe_poll_eintr=0;
        assert(errno==ETIMEDOUT && pipe_poll_eintr_calls>0 && prg.restart);
        prg.restart=0;
        close(fds[0]); close(fds[1]);
    }
    /* The framed sender writes and the receiver consumes one complete message;
     * oversize payloads are rejected before the fixed stack buffer is touched. */
    {
        int fds[2];
        uint8_t message[3]={0xa1,0xb2,0xc3}, decoded[8], too_small[2]={0x5a,0xa5}, oversized[1021]={0};
        assert(pipe(fds)==0);
        assert(pipe_send(fds[1],message,sizeof(message))==7);
        /* A short caller buffer must not be overwritten, and the rejected
         * frame must still be consumed so the next receive stays aligned. */
        assert(pipe_recv(fds[0],too_small,sizeof(too_small))==0);
        assert(too_small[0]==0x5a && too_small[1]==0xa5);
        assert(pipe_send(fds[1],message,sizeof(message))==7);
        assert(pipe_recv(fds[0],decoded,sizeof(decoded))==sizeof(message));
        assert(!memcmp(message,decoded,sizeof(message)));
        assert(pipe_send(fds[1],oversized,sizeof(oversized))==-1);
        prg.restart=0; /* invalid test input must not contaminate later checks */
        close(fds[0]); close(fds[1]);
    }
#ifdef CACHEEX
    assert(sizeof(struct cache_data)==102);
#else
    assert(sizeof(struct cache_data)==86);
#endif
    /* A completely active snapshot registry must fail closed, never leave an
     * uninitialized slot index for the caller to write. */
    {
        ECM_DATA saturated={0};
        unsigned i;
        __atomic_store_n(&saturated.lifetime_id, UINT64_C(0xfeed0001), __ATOMIC_RELEASE);
        ecm_request_set_dcwstatus(&saturated,STAT_DCW_WAIT);
        pthread_mutex_lock(&prg.lockcache);
        for (i=0; i<CACHE_ECM_SNAPSHOT_SLOTS; i++) {
            cache_ecm_snapshots[i].ecm=(ECM_DATA *)(uintptr_t)(i+1u);
            cache_ecm_snapshots[i].lifetime_id=(uint64_t)i+1u;
            cache_ecm_snapshots[i].profile=&cache_test_profile;
            cache_ecm_snapshots[i].active=1;
            cache_ecm_snapshots[i].state=CACHE_ECM_SNAPSHOT_USED;
        }
        pthread_mutex_unlock(&prg.lockcache);
        assert(!cache_ecm_snapshot_publish(&saturated,&cache_test_profile));
        assert(!cache_sendpipe_armable(&saturated,saturated.lifetime_id));
        memset(cache_ecm_snapshots,0,sizeof(cache_ecm_snapshots));
    }
    /* With spare registry capacity, a colliding insertion must choose an
     * empty slot rather than evicting an inactive identity that may still
     * validate a queued cache-reply frame. Inactive reclaim is only fallback
     * after a full scan. */
    {
        ECM_DATA candidate={0}, active={0}, inactive={0};
        unsigned home, slot=0, inactive_slot, found;
        int reclaim=-1;
        memset(cache_ecm_snapshots,0,sizeof(cache_ecm_snapshots));
        home=cache_ecm_snapshot_hash(&candidate);
        inactive_slot=(home+1u)&(CACHE_ECM_SNAPSHOT_SLOTS-1u);
        cache_ecm_snapshots[home].ecm=&active;
        cache_ecm_snapshots[home].lifetime_id=1;
        cache_ecm_snapshots[home].active=1;
        cache_ecm_snapshots[home].state=CACHE_ECM_SNAPSHOT_USED;
        cache_ecm_snapshots[inactive_slot].ecm=&inactive;
        cache_ecm_snapshots[inactive_slot].lifetime_id=2;
        cache_ecm_snapshots[inactive_slot].active=0;
        cache_ecm_snapshots[inactive_slot].state=CACHE_ECM_SNAPSHOT_USED;
        pthread_mutex_lock(&prg.lockcache);
        found=cache_ecm_snapshot_slot_locked(&candidate,1,&slot,&reclaim);
        pthread_mutex_unlock(&prg.lockcache);
        assert(!found && slot==((home+2u)&(CACHE_ECM_SNAPSHOT_SLOTS-1u)) && !reclaim);
        assert(cache_ecm_snapshots[inactive_slot].ecm==&inactive &&
               cache_ecm_snapshots[inactive_slot].lifetime_id==2);
        memset(cache_ecm_snapshots,0,sizeof(cache_ecm_snapshots));
    }
    /* FIND_SUCCESS sender and receiver must agree on every serialized byte,
     * including the newly added CW-cycle marker. */
    {
        uint8_t wire[64], sent_cw[16], received_cw[16];
        int marker=NO_CYCLE, wire_len;
        uint32_t decoded_peer=0;
        struct cache_data sent={0}, received={0};
        int base_len=12+(int)sizeof(void*)+(int)sizeof(uint64_t);
        memset(sent_cw,0x6b,sizeof(sent_cw));
        sent.tag=0x80; sent.sid=0x1136; sent.onid=1; sent.caid=0x1884;
        sent.hash=0x12345678; sent.ecm=&relay_waiter;
        sent.ecm_lifetime_id=UINT64_C(0x1020304050607080);
        memset(wire,0x5a,sizeof(wire));
        assert(put_cache2ecm(PIPE_CACHE_FIND_SUCCESS,wire,(size_t)(base_len+4+16),
                             &sent,sent_cw,UINT32_C(0x92345678),CW1CYCLE)==0);
        assert(wire[0]==0x5a && wire[base_len+4+16-1]==0x5a);
        wire_len=put_cache2ecm(PIPE_CACHE_FIND_SUCCESS,wire,sizeof(wire),&sent,sent_cw,
                               UINT32_C(0x92345678),CW1CYCLE);
        assert(!get_cache2ecm(wire,(size_t)wire_len-1u,&received,received_cw,&marker,
                              &decoded_peer));
        assert(get_cache2ecm(wire,(size_t)wire_len,&received,received_cw,&marker,
                             &decoded_peer));
        assert(wire_len==base_len+4+16+1 &&
               wire_len==12+(int)sizeof(void*)+(int)sizeof(uint64_t)+4+16+1);
        assert(decoded_peer==UINT32_C(0x92345678) && marker==CW1CYCLE &&
               received.ecm==&relay_waiter &&
               received.ecm_lifetime_id==sent.ecm_lifetime_id &&
               !memcmp(received_cw,sent_cw,sizeof(sent_cw)));
    }
    {
        uint8_t wire[80], cache_wire[80], prevcw[16], decoded_cw[16];
        uint32_t scope_provider=99;
        int wire_len, expected_wire_len=16+(int)sizeof(void*)+(int)sizeof(uint64_t);
        struct cache_data decoded={0}, scope={0}, existing={0}, incoming={0};
        struct cache_data cache_record={0}, cache_decoded={0};
        struct cardserver_data local_profile={0};
        int cache_len, cache_mark=NO_CYCLE;
        ECM_DATA local={0};

#ifdef CACHEEX
        expected_wire_len += 16;
#endif
        local.ecm[0]=0x80; local.sid=0x1136; local.caid=0x1884;
        local.hash=0x12345678; local.provid=0; local.cw1cycle=0x80;
        local.provider_known=1;
        __atomic_store_n(&local.lifetime_id, UINT64_C(0x1122334455667788), __ATOMIC_RELEASE);
        ecm_request_set_dcwstatus(&local,STAT_DCW_WAIT);
        assert(cache_ecm_snapshot_publish(&local,&local_profile));
        assert(cache_sendpipe_armable(&local,local.lifetime_id));
        assert(cache_ecm_snapshot_validate(&local,local.lifetime_id));
        assert(!cache_ecm_snapshot_validate((ECM_DATA *)(uintptr_t)1u,
                                            local.lifetime_id));
        ecm_request_set_dcwstatus(&local,STAT_DCW_FAILED);
        assert(!cache_sendpipe_armable(&local,local.lifetime_id));
        ecm_request_set_dcwstatus(&local,STAT_DCW_WAIT);
        assert(!cache_sendpipe_armable(&local,local.lifetime_id));
        assert(cache_ecm_snapshot_publish(&local,&local_profile));
        assert(cache_sendpipe_armable(&local,local.lifetime_id));
        memset(wire,0x5a,sizeof(wire));
        assert(put_ecm2cache(PIPE_CACHE_FIND,wire,(size_t)expected_wire_len-1u,
                             &local,0,NULL)==0);
        assert(wire[0]==0x5a && wire[expected_wire_len-1]==0x5a);
        wire_len=put_ecm2cache(PIPE_CACHE_FIND,wire,sizeof(wire),&local,0,NULL);
        assert(get_ecm2cache(wire,wire_len-1,&decoded,NULL)<0);
        assert(get_ecm2cache(wire,wire_len,&decoded,NULL)>=0);
        assert(wire_len==expected_wire_len && decoded.provider_known &&
               decoded.provid==0 && decoded.cwcycle==CW1CYCLE &&
               decoded.ecm==&local && decoded.ecm_lifetime_id==local.lifetime_id &&
               ecm_request_lifetime_matches(decoded.ecm,&local,decoded.ecm_lifetime_id));

        memset(prevcw,0x6d,sizeof(prevcw));
        wire_len=put_ecm2cache(PIPE_CACHE_FIND,wire,sizeof(wire),&local,0,prevcw);
        assert(get_ecm2cache(wire,wire_len-1,&decoded,decoded.prevcw)<0);
        get_ecm2cache(wire,wire_len,&decoded,decoded.prevcw);
        assert(wire_len==expected_wire_len+16 &&
               !memcmp(decoded.prevcw,prevcw,sizeof(prevcw)) &&
               decoded.ecm_lifetime_id==local.lifetime_id);
        __atomic_store_n(&local.lifetime_id, UINT64_C(0x1122334455667789), __ATOMIC_RELEASE);
        assert(!ecm_request_lifetime_matches(decoded.ecm,&local,decoded.ecm_lifetime_id));
        __atomic_store_n(&local.lifetime_id, UINT64_C(0x1122334455667788), __ATOMIC_RELEASE);

        cache_record.tag=0x80; cache_record.sid=local.sid; cache_record.caid=local.caid;
        cache_record.hash=local.hash; cache_record.ecm=&local;
        cache_record.ecm_lifetime_id=local.lifetime_id;
        cache_len=put_cache2ecm(PIPE_CACHE_FIND_FAILED,cache_wire,sizeof(cache_wire),&cache_record,NULL,0,0);
        assert(cache_len==12+(int)sizeof(void*)+(int)sizeof(uint64_t));
        assert(!get_cache2ecm(cache_wire,(size_t)cache_len-1u,&cache_decoded,NULL,NULL,NULL));
        assert(get_cache2ecm(cache_wire,(size_t)cache_len,&cache_decoded,NULL,NULL,NULL));
        assert(cache_decoded.ecm==&local &&
               cache_decoded.ecm_lifetime_id==local.lifetime_id &&
               ecm_request_lifetime_matches(cache_decoded.ecm,&local,
                                            cache_decoded.ecm_lifetime_id));

        memset(local.cw,0x27,sizeof(local.cw));
        cache_len=put_cache2ecm(PIPE_CACHE_FIND_SUCCESS,cache_wire,sizeof(cache_wire),&cache_record,
                                local.cw,17,CW1CYCLE);
        memset(decoded_cw,0,sizeof(decoded_cw));
        assert(cache_len==12+(int)sizeof(void*)+(int)sizeof(uint64_t)+4+16+1);
        assert(get_cache2ecm(cache_wire,(size_t)cache_len,&cache_decoded,decoded_cw,&cache_mark,NULL));
        assert(cache_mark==CW1CYCLE && !memcmp(decoded_cw,local.cw,16));

        local.provider_known=0; local.cw1cycle=0;
        wire_len=put_ecm2cache(PIPE_CACHE_FIND,wire,sizeof(wire),&local,0,NULL);
        get_ecm2cache(wire,wire_len,&decoded,NULL);
        assert(!decoded.provider_known && decoded.provid==0 && decoded.cwcycle==NO_CYCLE);

        /* A non-null pointer decoded from a pipe must be checked by address
         * and generation in the side table before any dereference. */
        pthread_mutex_lock(&prg.lockcache);
        assert(!cache_ecm_snapshot_read_locked((ECM_DATA *)(uintptr_t)1u,
                                                local.lifetime_id,NULL,NULL));
        assert(cache_ecm_snapshot_read_locked(&local,local.lifetime_id,NULL,NULL));
        pthread_mutex_unlock(&prg.lockcache);
        cache_ecm_snapshot_retire(&local);
        pthread_mutex_lock(&prg.lockcache);
        assert(!cache_ecm_snapshot_read_locked(&local,local.lifetime_id,NULL,NULL));
        pthread_mutex_unlock(&prg.lockcache);

        assert(!cache_consensus_provider_scope(&scope,&scope_provider) && scope_provider==0);
        scope.provider_known=1; scope.provid=0;
        assert(cache_consensus_provider_scope(&scope,&scope_provider) && scope_provider==0);
        scope.local_provider_set=1; scope.local_provider=0x123456;
        assert(cache_consensus_provider_scope(&scope,&scope_provider) && scope_provider==0x123456);

        existing.provider_known=1; existing.provid=5;
        incoming.provid=0; incoming.provider_known=0;
        assert(!cache_local_provider_collision(&existing,&incoming));
        cache_attach_local_provider(&existing,&incoming);
        assert(existing.provider_known && existing.provid==5 && !existing.local_provider_set);
        incoming.provider_known=1;
        assert(cache_local_provider_collision(&existing,&incoming));
        conflicts=0; /* Ignore the helper's intentional trace in later totals. */
    }
    { int fds[2]; assert(pipe(fds) == 0); prg.pipe.ecm[0]=fds[0]; prg.pipe.ecm[1]=fds[1]; }
    cfg.cache.alivetime=45000;
    cfg.cache.threshold=2;
    cfg.cache.filter=0;
    cfg.cache.consensus=0;
    cfg.cache.gate_localonly=0;
    cfg.cache.gate_blockfake=0;
    cfg.cache.gate_cwcheck=0;
#ifdef CACHEEX
    /* Cache offers rejected before the ECM Set-DCW pipe still enter the
     * candidate ledger only when an exact live request identity is available.
     * CSP borrows the saved local snapshot; CacheEX must also match its on-wire
     * ECMD5. Neither offer can be accepted or prove anything by itself. */
    {
        struct cache_data entry={0}, source={0};
        ECM_DATA waiting={0};
        struct cardserver_data profile={0};
        struct ledger_identity identity, offered_identity;
        struct ledger_entry *ledger_entry;
        struct ledger_event ledger_event;
        uint8_t offered[16];
        uint64_t lifetime=UINT64_C(0x2a01);
        int index;

        waiting.ecm[0]=0x80; waiting.ecmlen=64;
        waiting.caid=0x1884; waiting.sid=0x3041;
        waiting.hash=0x91375321; waiting.provid=5; waiting.provider_known=1;
        memset(waiting.ecmd5,0x42,sizeof(waiting.ecmd5));
        __atomic_store_n(&waiting.lifetime_id,lifetime,__ATOMIC_RELEASE);
        ecm_request_set_dcwstatus(&waiting,STAT_DCW_WAIT);
        assert(cache_ecm_snapshot_publish(&waiting,&profile));
        assert(ledger_identity_from_ecm(&identity,&waiting));
        memset(offered,0x73,sizeof(offered));

        entry.tag=waiting.ecm[0]; entry.sid=waiting.sid; entry.caid=waiting.caid;
        entry.hash=waiting.hash; entry.provid=waiting.provid; entry.provider_known=1;
        entry.local_provider_set=1; entry.local_provider=waiting.provid;
        entry.flags=CACHE_FLAG_SENDPIPE; entry.ecm=&waiting;
        entry.ecm_lifetime_id=lifetime; entry.recvtime=9000;
        source.tag=entry.tag; source.sid=entry.sid; source.caid=entry.caid;
        source.hash=entry.hash;
        index=entry.sid & MAX_CACHE_INDEX;
        table[index]=&entry;
        reject_profile_cw=1;
        profile_checks=0;
        ledger_init(&ledger_tab);
        pthread_mutex_lock(&prg.lockcache);
        assert(cache_setdcw(&source,offered,NO_CYCLE,PEER_CSP|31)&DCW_ERROR);
        pthread_mutex_unlock(&prg.lockcache);
        ledger_entry=ledger_find(&ledger_tab,&identity,10000);
        assert(profile_checks==1 && ledger_entry && ledger_entry->nslot==1);
        assert(ledger_entry->candidate[0].srctype==DCW_SOURCE_CACHE &&
               ledger_entry->candidate[0].srcid==(PEER_CSP|31) &&
               !ledger_entry->candidate[0].accepted &&
               !ledger_entry->candidate[0].delivered);
        assert(ledger_next_proof(&ledger_tab,&identity,10000,&ledger_event)==LEDGER_NONE);

        /* A CacheEX provider collision is still observed under the distinct
         * provider identity when its ECMD5 matches the local ECM. */
        source.provider_known=1; source.provid=6;
        memcpy(source.ecmd5,waiting.ecmd5,sizeof(source.ecmd5));
        ledger_init(&ledger_tab);
        pthread_mutex_lock(&prg.lockcache);
        assert(cache_setdcw(&source,offered,NO_CYCLE,PEER_CACHEEX_SERVER|31)&DCW_ERROR);
        pthread_mutex_unlock(&prg.lockcache);
        offered_identity=identity; offered_identity.provid=source.provid;
        ledger_entry=ledger_find(&ledger_tab,&offered_identity,10000);
        assert(ledger_entry && ledger_entry->nslot==1 &&
               !ledger_entry->candidate[0].accepted &&
               !ledger_entry->candidate[0].delivered);
        assert(ledger_next_proof(&ledger_tab,&offered_identity,10000,&ledger_event)==LEDGER_NONE);

        /* A different CacheEX ECMD5 is not attributed to this active waiter,
         * even though its 32-bit cache key collides. */
        source.provid=5; memset(source.ecmd5,0x19,sizeof(source.ecmd5));
        reject_profile_cw=1;
        ledger_init(&ledger_tab);
        pthread_mutex_lock(&prg.lockcache);
        assert(cache_setdcw(&source,offered,NO_CYCLE,PEER_CACHEEX_SERVER|31)&DCW_ERROR);
        pthread_mutex_unlock(&prg.lockcache);
        assert(ledger_find(&ledger_tab,&identity,10000)==NULL);

        /* A stale pointer with no armed live waiter cannot supply identity. */
        source.provider_known=0; source.provid=0;
        entry.flags=0;
        ledger_init(&ledger_tab);
        pthread_mutex_lock(&prg.lockcache);
        assert(cache_setdcw(&source,offered,NO_CYCLE,PEER_CSP|31)&DCW_ERROR);
        pthread_mutex_unlock(&prg.lockcache);
        assert(ledger_find(&ledger_tab,&identity,10000)==NULL);
        reject_profile_cw=0;
        source_conflicts=0; profile_checks=0;
        table[index]=NULL;
        cache_ecm_snapshot_retire(&waiting);
    }
    /* A successful transport records the exact pre-send ECM/CW snapshot even
     * if the pointed-to pool object changes before the post-send hook runs. */
    {
        ECM_DATA sent={0};
        struct ledger_delivery_snapshot delivery;
        struct ledger_identity original, replacement;
        struct ledger_entry *entry;
        struct {
            uint16_t caid;
            uint32_t prov;
            uint8_t provider_known;
            uint16_t sid;
            uint32_t hash;
            uint8_t tag;
            uint8_t ecmd5[16];
            uint16_t ecmlen;
            uint8_t dcw[16];
            int dcwsrctype;
            int dcwsrcid;
        } saved_delivery;
        struct ledger_event event;
        uint8_t offered[16], accepted[16];
        memset(offered,0x31,sizeof(offered));
        memset(accepted,0x52,sizeof(accepted));
        sent.ecm[0]=0x80; sent.ecmlen=64; sent.caid=0x1884; sent.sid=0x3210;
        sent.hash=0x76543210; sent.provid=0x12345; sent.provider_known=1;
        memset(sent.ecmd5,0x64,sizeof(sent.ecmd5));
        memcpy(sent.cw,accepted,sizeof(sent.cw));
        sent.dcwsrctype=DCW_SOURCE_SERVER; sent.dcwsrcid=77;
        assert(ledger_identity_from_ecm(&original,&sent));
        ledger_init(&ledger_tab);
        assert(ledger_candidate(&ledger_tab,&original,offered,DCW_SOURCE_SERVER,77,10000,&event)==LEDGER_NONE);
        assert(ledger_accept(&ledger_tab,&original,offered,accepted,
                             DCW_SOURCE_SERVER,77,10000,&event)==LEDGER_ACCEPTED);
        assert(ledger_delivery_snapshot_capture(&delivery,&sent));
        memset(&saved_delivery,0,sizeof(saved_delivery));
        LEDGER_DELIVERY_TO_LASTECM(saved_delivery,delivery);
        sent.ecm[0]=0x81; sent.caid=0x0500; sent.sid=0x4321;
        sent.hash=0x10203040; sent.provid=0x6789; sent.provider_known=1;
        memset(sent.ecmd5,0x96,sizeof(sent.ecmd5));
        memset(sent.cw,0xa3,sizeof(sent.cw));
        sent.dcwsrctype=DCW_SOURCE_CACHE; sent.dcwsrcid=PEER_CSP|99;
        assert(ledger_identity_from_ecm(&replacement,&sent));
        assert(saved_delivery.caid==original.caid && saved_delivery.prov==original.provid &&
               saved_delivery.provider_known==original.provider_known &&
               saved_delivery.sid==original.sid && saved_delivery.hash==original.hash &&
               saved_delivery.tag==original.tag && saved_delivery.ecmlen==original.ecmlen &&
               saved_delivery.dcwsrctype==DCW_SOURCE_SERVER && saved_delivery.dcwsrcid==77 &&
               !memcmp(saved_delivery.ecmd5,original.ecmd5,sizeof(original.ecmd5)) &&
               !memcmp(saved_delivery.dcw,accepted,sizeof(accepted)));
        ledger_note_delivered(&delivery,10001);
        entry=ledger_find(&ledger_tab,&original,10001);
        assert(entry && entry->candidate[0].accepted &&
               entry->candidate[0].delivered &&
               entry->candidate[0].srctype==DCW_SOURCE_SERVER &&
               entry->candidate[0].srcid==77 &&
               !memcmp(entry->candidate[0].accepted_cw,accepted,sizeof(accepted)));
        assert(ledger_find(&ledger_tab,&replacement,10001)==NULL);
    }
#endif
    /* Exercise the production ECM->cache receiver, not just the serializers:
     * an old pipe message must not create or replace an entry after its pool
     * slot has been reused for a different lifetime. */
    {
        int fds[2];
        ECM_DATA piped={0};
        struct cardserver_data profile={0};
        struct cache_data lookup={0}, *entry;
        uint64_t first_id=UINT64_C(0x201);
        uint64_t second_id=UINT64_C(0x202);
        int index;

        assert(pipe(fds)==0);
        prg.pipe.cache[0]=fds[0]; prg.pipe.cache[1]=fds[1];
        cfg.cache.server=NULL;
        piped.ecm[0]=0x80; piped.caid=0x1884; piped.sid=0x2a01;
        piped.hash=0x55667788;
        ecm_request_set_dcwstatus(&piped,STAT_DCW_WAIT);
        __atomic_store_n(&piped.lifetime_id,first_id,__ATOMIC_RELEASE);
        lookup.tag=piped.ecm[0]; lookup.caid=piped.caid;
        lookup.sid=piped.sid; lookup.hash=piped.hash;
        index=lookup.sid & MAX_CACHE_INDEX;

        pipe_cache_request(&piped,&profile); /* publish and queue generation 1 */
        cache_ecm_snapshot_retire(&piped); /* production reuse retires before clear */
        __atomic_store_n(&piped.lifetime_id,second_id,__ATOMIC_RELEASE);
        pthread_mutex_lock(&prg.lockcache);
        cache_pipe_recvmsg();
        pthread_mutex_unlock(&prg.lockcache);
        assert(cache_fetch(&lookup)==NULL); /* stale request did not allocate */

        pipe_cache_request(&piped,&profile); /* publish and queue generation 2 */
        pthread_mutex_lock(&prg.lockcache);
        cache_pipe_recvmsg();
        pthread_mutex_unlock(&prg.lockcache);
        entry=cache_fetch(&lookup);
        assert(entry && entry->ecm==&piped &&
               entry->ecm_lifetime_id==second_id && cache_entry_sendpipe_active(entry));

        /* A queued current-generation request becomes stale when the pool
         * slot is retired for its next request, even before that new request
         * publishes a replacement snapshot. */
        pipe_cache_request(&piped,&profile); /* queued generation 2 */
        cache_ecm_snapshot_retire(&piped);
        __atomic_store_n(&piped.lifetime_id,UINT64_C(0x203),__ATOMIC_RELEASE);
        pthread_mutex_lock(&prg.lockcache);
        cache_pipe_recvmsg();
        pthread_mutex_unlock(&prg.lockcache);
        assert(entry->ecm==&piped && entry->ecm_lifetime_id==second_id);
        assert(!cache_entry_sendpipe_active(entry));

        table[index]=NULL;
        free(entry);
        close(fds[0]); close(fds[1]);
    }
    /* A successful reply is serialized under the ECM lock and remains only in
     * the caller's buffer until the explicit post-unlock send helper runs. */
    {
        int fds[2], flags, read_result;
        ECM_DATA reply={0};
        struct cache_data decoded={0};
        struct cardserver_data profile={0};
        uint8_t cache_reply_buf[80], undersized_reply_buf[80], frame[80], decoded_cw[16], byte;
        int reply_len, decoded_len;
        uint64_t reply_lifetime=UINT64_C(0x2f01);

        assert(pipe(fds)==0);
        flags=fcntl(fds[0],F_GETFL,0);
        assert(flags>=0 && fcntl(fds[0],F_SETFL,flags|O_NONBLOCK)==0);
        prg.pipe.cache[0]=fds[0]; prg.pipe.cache[1]=fds[1];
        profile.option.fallowcache=1;
        profile.option.cachesendrep=1;
        reply.ecm[0]=0x80; reply.caid=0x1884; reply.provid=0x123456;
        reply.sid=0x1136; reply.hash=0x76543210; reply.provider_known=1;
        reply.dcwsrctype=DCW_SOURCE_CACHE;
        memset(reply.cw,0x4d,sizeof(reply.cw));
        __atomic_store_n(&reply.lifetime_id,reply_lifetime,__ATOMIC_RELEASE);
        ecm_request_set_dcwstatus(&reply,STAT_DCW_SUCCESS);
        memset(undersized_reply_buf,0xa7,sizeof(undersized_reply_buf));
        assert(pipe_cache_reply(&reply,&profile,undersized_reply_buf,
                                (size_t)cache_ecm_pipe_base_len()+16u)==0);
        assert(undersized_reply_buf[0]==0xa7 && !(reply.cachestatus&ECM_CACHE_REP));
        reply_len=pipe_cache_reply(&reply,&profile,cache_reply_buf,sizeof(cache_reply_buf));
        assert(reply_len==cache_ecm_pipe_base_len()+17);
        assert(reply.cachestatus&ECM_CACHE_REP);
        assert(pipe_cache_reply(&reply,&profile,cache_reply_buf,sizeof(cache_reply_buf))==0);
        read_result=(int)read(fds[0],&byte,1);
        assert(read_result==-1 && (errno==EAGAIN || errno==EWOULDBLOCK));

        pipe_cache_reply_send(cache_reply_buf,reply_len);
        assert(pipe_recv(fds[0],frame,sizeof(frame))==reply_len);
        assert(get_ecm2cache(frame,reply_len-1,&decoded,decoded_cw)<0);
        decoded_len=get_ecm2cache(frame,reply_len,&decoded,decoded_cw);
        assert(decoded_len+1==reply_len && frame[decoded_len]==1);
        assert(decoded.ecm==&reply && decoded.ecm_lifetime_id==reply_lifetime &&
               !memcmp(decoded_cw,reply.cw,sizeof(decoded_cw)));
        cache_ecm_snapshot_retire(&reply);
        prg.pipe.cache[0]=prg.pipe.cache[1]=-1;
        close(fds[0]); close(fds[1]);
    }
    /* A CacheEX source with a provider that conflicts with the waiting local
     * request must be refused before profile validation or cache mutation. */
    {
        struct cache_data entry={0}, source={0};
        ECM_DATA waiting={0};
        struct cardserver_data profile={0};
        uint8_t offered[16];
        memset(offered,0x31,sizeof(offered));
        entry.caid=source.caid=0x1884; entry.sid=source.sid=0x1136;
        entry.tag=source.tag=0x80; entry.hash=source.hash=0x1234;
        entry.recvtime=9000; entry.provid=1; entry.local_provider_set=1;
        entry.local_provider=1; entry.flags=CACHE_FLAG_SENDPIPE;
        __atomic_store_n(&waiting.lifetime_id,UINT64_C(0x301),__ATOMIC_RELEASE);
        ecm_request_set_dcwstatus(&waiting,STAT_DCW_WAIT);
        assert(cache_ecm_snapshot_publish(&waiting,&profile));
        entry.ecm=&waiting;
        entry.ecm_lifetime_id=UINT64_C(0x301);
        source.provid=2;
        table[entry.sid & MAX_CACHE_INDEX]=&entry;
        profile_checks=0;
        assert(cache_setdcw(&source,offered,NO_CYCLE,PEER_CACHEEX_SERVER|7)&DCW_ERROR);
        assert(source_conflicts==1 && profile_checks==0 && entry.cwdata==NULL);
        source.provid=0; /* zero is explicit in CacheEX, unlike providerless CSP */
        assert(cache_setdcw(&source,offered,NO_CYCLE,PEER_CACHEEX_SERVER|7)&DCW_ERROR);
        assert(source_conflicts==2 && profile_checks==0 && entry.cwdata==NULL);

        /* An explicitly-known provider zero in a cached entry must also be
         * distinguished from a missing provider field. */
        entry.local_provider_set=0; entry.local_provider=0; entry.flags=0;
        entry.ecm=NULL; entry.ecm_lifetime_id=0;
        entry.provid=0; entry.provider_known=1; source.provid=2;
        assert(cache_setdcw(&source,offered,NO_CYCLE,PEER_CACHEEX_SERVER|7)&DCW_ERROR);
        assert(source_conflicts==3 && profile_checks==0 && entry.cwdata==NULL);

        /* Even without a local waiter, two explicitly different nonzero
         * CacheEX provider values may not merge into one cache entry. */
        entry.provider_known=0; entry.provid=1; source.provid=2;
        assert(cache_setdcw(&source,offered,NO_CYCLE,PEER_CACHEEX_SERVER|7)&DCW_ERROR);
        assert(source_conflicts==4 && profile_checks==0 && entry.cwdata==NULL);

        /* CSP has no provider field. With a live local ECM, use that request's
         * provider for profile validation; after SENDPIPE is cleared, do not
         * trust the stale ECM pointer as a live profile association. */
        entry.local_provider_set=1; entry.local_provider=5; entry.provid=5;
        entry.flags=CACHE_FLAG_SENDPIPE; entry.ecm=&waiting;
        entry.ecm_lifetime_id=UINT64_C(0x301); source.provid=0;
        source_conflicts=4; profile_checks=0;
        assert(cache_setdcw(&source,offered,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(profile_checks==1 && last_profile==&profile && last_profile_provider==5);
        free(entry.cwdata); entry.cwdata=NULL;
        entry.flags=0; profile_checks=0; last_profile=(struct cardserver_data *)1;
        assert(cache_setdcw(&source,offered,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(profile_checks==1 && last_profile==NULL && last_profile_provider==0);
        free(entry.cwdata); entry.cwdata=NULL;
    }
    /* The CSP wire identity is provider-less (numeric zero), but this live
     * waiter is already attached to provider 5. Evidence from known provider
     * zero must not be applied to it, and evidence for provider 5 must not be
     * missed just because the CSP packet itself carries no provider. */
    {
        struct cache_data entry={0}, source={0};
        ECM_DATA waiting={0};
        struct cardserver_data profile={0};
        struct cwneg_ev nev;
        struct cwattest_ev aev;
        uint8_t offered0[16], offered5[16];
        unsigned long before;
        memset(offered0,0x41,sizeof(offered0));
        memset(offered5,0x42,sizeof(offered5));
        entry.caid=source.caid=0x1884; entry.sid=source.sid=0x1136;
        entry.tag=source.tag=0x80; entry.hash=source.hash=0x7171;
        entry.recvtime=9000; entry.provid=5; entry.provider_known=1;
        entry.local_provider_set=1; entry.local_provider=5;
        entry.flags=CACHE_FLAG_SENDPIPE; entry.ecm=&waiting;
        __atomic_store_n(&waiting.lifetime_id,UINT64_C(0x302),__ATOMIC_RELEASE);
        entry.ecm_lifetime_id=UINT64_C(0x302);
        ecm_request_set_dcwstatus(&waiting,STAT_DCW_WAIT);
        assert(cache_ecm_snapshot_publish(&waiting,&profile));
        source.provid=0; source.provider_known=0;
        table[entry.sid & MAX_CACHE_INDEX]=&entry;
        cfg.cache.threshold=2; cfg.cache.attest=0;

        cwneg_init(&cwneg_tab);
        cwneg_mark(&cwneg_tab,offered0,entry.caid,0, 1,entry.sid,
                   DCW_SOURCE_CACHE,1,1000);
        before=cwneg_hits;
        assert(cache_setdcw(&source,offered0,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(cwneg_hits==before); /* provider-0 proof cannot cross to known 5 */
        free(entry.cwdata); entry.cwdata=NULL;

        entry.hash=source.hash=0x7172;
        cwneg_init(&cwneg_tab);
        cwneg_mark(&cwneg_tab,offered5,entry.caid,5, 1,entry.sid,
                   DCW_SOURCE_CACHE,1,1000);
        before=cwneg_hits;
        assert(cache_setdcw(&source,offered5,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(cwneg_hits==before+1); /* provider-5 proof is retained for CSP */
        free(entry.cwdata); entry.cwdata=NULL;

        cwneg_init(&cwneg_tab); /* isolate attestation from earlier hard proof marks */
        cfg.cache.attest=2;
        entry.hash=source.hash=0x7173;
        cwattest_init(&cwattest_tab);
        cwattest_note(&cwattest_tab,offered0,entry.caid,0, 1,entry.sid,
                      DCW_SOURCE_CACHE,1,101,2,1000,CWATTEST_TTL_MS,&aev);
        cwattest_note(&cwattest_tab,offered0,entry.caid,0, 1,entry.sid,
                      DCW_SOURCE_CACHE,1,102,2,1100,CWATTEST_TTL_MS,&aev);
        before=cwattest_hits;
        assert(cache_setdcw(&source,offered0,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(cwattest_hits==before); /* provider-0 filing cannot cross to 5 */
        free(entry.cwdata); entry.cwdata=NULL;

        entry.hash=source.hash=0x7174;
        cwattest_init(&cwattest_tab);
        cwattest_note(&cwattest_tab,offered5,entry.caid,5, 1,entry.sid,
                      DCW_SOURCE_CACHE,1,101,2,1000,CWATTEST_TTL_MS,&aev);
        cwattest_note(&cwattest_tab,offered5,entry.caid,5, 1,entry.sid,
                      DCW_SOURCE_CACHE,1,102,2,1100,CWATTEST_TTL_MS,&aev);
        before=cwattest_hits;
        assert(cache_setdcw(&source,offered5,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(cwattest_hits==before+1); /* provider-5 filing is retained */
        free(entry.cwdata); entry.cwdata=NULL;
        cfg.cache.attest=0;

        /* Gate scope must distinguish an explicitly-known provider zero from
         * no provider context, for both hard proof memory and expiring
         * attestation. */
        entry.hash=source.hash=0x7175;
        entry.local_provider_set=1; entry.local_provider=0;
        entry.provid=0; entry.provider_known=1; entry.flags=0; entry.ecm=NULL;
        source.provid=0; source.provider_known=0;
        cwneg_init(&cwneg_tab);
        cwneg_mark(&cwneg_tab,offered0,entry.caid,0, 0,entry.sid,
                   DCW_SOURCE_CACHE,1,1000);
        before=cwneg_hits;
        assert(cache_setdcw(&source,offered0,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(cwneg_hits==before); /* unknown proof cannot become known zero */
        free(entry.cwdata); entry.cwdata=NULL;

        entry.hash=source.hash=0x7176;
        cwneg_init(&cwneg_tab);
        cwneg_mark(&cwneg_tab,offered0,entry.caid,0, 1,entry.sid,
                   DCW_SOURCE_CACHE,1,1000);
        before=cwneg_hits;
        assert(cache_setdcw(&source,offered0,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(cwneg_hits==before+1); /* known zero matches known zero */
        free(entry.cwdata); entry.cwdata=NULL;

        cwneg_init(&cwneg_tab); /* isolate attestation from provider-zero proof marks */
        cfg.cache.attest=2;
        entry.hash=source.hash=0x7177;
        cwattest_init(&cwattest_tab);
        cwattest_note(&cwattest_tab,offered0,entry.caid,0, 0,entry.sid,
                      DCW_SOURCE_CACHE,1,101,2,1000,CWATTEST_TTL_MS,&aev);
        cwattest_note(&cwattest_tab,offered0,entry.caid,0, 0,entry.sid,
                      DCW_SOURCE_CACHE,1,102,2,1100,CWATTEST_TTL_MS,&aev);
        before=cwattest_hits;
        assert(cache_setdcw(&source,offered0,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(cwattest_hits==before); /* unknown filing cannot become known zero */
        free(entry.cwdata); entry.cwdata=NULL;

        entry.hash=source.hash=0x7178;
        cwattest_init(&cwattest_tab);
        cwattest_note(&cwattest_tab,offered0,entry.caid,0, 1,entry.sid,
                      DCW_SOURCE_CACHE,1,101,2,1000,CWATTEST_TTL_MS,&aev);
        cwattest_note(&cwattest_tab,offered0,entry.caid,0, 1,entry.sid,
                      DCW_SOURCE_CACHE,1,102,2,1100,CWATTEST_TTL_MS,&aev);
        before=cwattest_hits;
        assert(cache_setdcw(&source,offered0,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(cwattest_hits==before+1); /* known-zero filing matches known zero */
        free(entry.cwdata); entry.cwdata=NULL;
        cfg.cache.attest=0;

        /* Without a local/metadata provider identity, CSP remains unknown:
         * it matches unknown evidence but cannot consume known-zero evidence. */
        entry.hash=source.hash=0x7179;
        entry.local_provider_set=0; entry.local_provider=0;
        entry.provid=0; entry.provider_known=0; entry.flags=0; entry.ecm=NULL;
        cwneg_init(&cwneg_tab);
        cwneg_mark(&cwneg_tab,offered0,entry.caid,0, 1,entry.sid,
                   DCW_SOURCE_CACHE,1,1000);
        before=cwneg_hits;
        assert(cache_setdcw(&source,offered0,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(cwneg_hits==before);
        free(entry.cwdata); entry.cwdata=NULL;
        entry.hash=source.hash=0x717a;
        cwneg_init(&cwneg_tab);
        cwneg_mark(&cwneg_tab,offered0,entry.caid,0, 0,entry.sid,
                   DCW_SOURCE_CACHE,1,1000);
        before=cwneg_hits;
        assert(cache_setdcw(&source,offered0,NO_CYCLE,PEER_CSP|7)&DCW_ERROR);
        assert(cwneg_hits==before+1);
        free(entry.cwdata); entry.cwdata=NULL;
    }
    /* The CSP reuse-proof branch has no provider field on the wire. Its
     * negative-memory mark must therefore use the cache entry's established
     * local identity, not the CSP request's numeric zero. */
    {
        struct cwreuse_table reuse={0};
        struct cache_data csp_context={0};
        uint8_t key[16], ecm_a[16]={1}, ecm_b[16]={2};
        uint32_t provider=0;
        int provider_known;
        memset(key,0x53,sizeof(key));
        csp_context.local_provider_set=1; csp_context.local_provider=5;
        provider_known=cache_consensus_provider_scope(&csp_context,&provider);
        assert(provider_known && provider==5);
        assert(cwreuse_offer_scoped(&reuse,key,ecm_a,1,0x1884,0x1001,
                                    provider,provider_known,1000)==CWREUSE_NONE);
        assert(cwreuse_offer_scoped(&reuse,key,ecm_b,1,0x1884,0x1002,
                                    provider,provider_known,2000)==CWREUSE_PROOF);
        cwreuse_reset(&reuse);
        assert(cwreuse_offer_scoped(&reuse,key,ecm_a,1,0x1884,0x1001,4,1,1000)==CWREUSE_NONE);
        assert(cwreuse_offer_scoped(&reuse,key,ecm_b,1,0x1884,0x1002,
                                    provider,provider_known,2000)==CWREUSE_NONE);
        cwneg_init(&cwneg_tab);
        cwn_note(DCW_SOURCE_CACHE,PEER_CSP|9,key,0x1884,provider,provider_known,0x1002,2000);
        assert(cwneg_hit(&cwneg_tab,key,0x1884,5,1,0x1003,2100,NULL)==1);
        assert(cwneg_hit(&cwneg_tab,key,0x1884,0,1,0x1003,2100,NULL)==0);

        /* CSP's wire provid is zero, but the stored cache record is attached
         * to provider 5. The reuse proof must sweep that effective local
         * scope; sweeping numeric wire zero would leave the proven candidate
         * available to later cache lookups. */
        {
            struct cache_data stored={0};
            struct cw_cache_data candidate={0};
            struct purge_table purge={0};
            stored.caid=0x1884; stored.sid=0x1002; stored.provid=provider;
            stored.provider_known=1; stored.local_provider_set=1;
            stored.local_provider=provider; stored.next=stored.prev=&stored;
            stored.cwdata=&candidate; candidate.peerid=PEER_CSP|9;
            table[stored.sid & MAX_CACHE_INDEX]=&stored;
            assert(cache_purge_mark_scoped(&purge,DCW_SOURCE_CACHE,PEER_CSP|9,
                                           stored.caid,stored.sid,provider,1,3000)==1);
            assert(cache_purge_sweep_scoped(&purge,DCW_SOURCE_CACHE,PEER_CSP|9,
                                            stored.caid,stored.sid,provider,1,DCW_ERROR)==1);
            assert(candidate.status & DCW_ERROR);
            table[stored.sid & MAX_CACHE_INDEX]=NULL;
        }
    }
#ifdef CACHEEX
    /* Exercise the production CSP reuse-proof helper with a providerless wire
     * request matched to a cache record attached to provider 5. All proof
     * effects, including trust lookup scope and the purge sweep, must use 5. */
    {
        struct cache_data stored={0}, req={0};
        struct cachepeer_data peer={0};
        struct host_data host={0};
        struct cw_cache_data *candidate;
        uint8_t key[16], ecm_a[16]={0xa1}, ecm_b[16]={0xb2};
        int idx=0x1002 & MAX_CACHE_INDEX;
        int status;

        memset(key,0x53,sizeof(key));
        stored.caid=req.caid=0x1884; stored.sid=req.sid=0x1002;
        stored.tag=req.tag=0x80; stored.hash=req.hash=0x7a01;
        stored.recvtime=9000; stored.provid=5; stored.provider_known=1;
        stored.local_provider_set=1; stored.local_provider=5;
        memcpy(stored.ecmd5,ecm_b,sizeof(ecm_b));
        stored.next=stored.prev=&stored;
        req.provid=0; req.provider_known=0; /* absent on CSP wire */
        table[idx]=&stored;

        peer.id=9; peer.host=&host; peer.port=12345;
        cfg.cache.alivetime=45000; cfg.cache.threshold=1;
        cfg.cache.filter=0; cfg.cache.consensus=0; cfg.cache.attest=0;
        cfg.cache.gate_localonly=0; cfg.cache.gate_blockfake=0;
        cfg.cache.gate_cwcheck=0; cfg.cache.dcwcheck2=0;
        cfg.cache.dcwcheck3=0; cfg.cache.forward=0;
        cwneg_init(&cwneg_tab); cache_purge_init(&purge_tab);
        cwreuse_reset(&cwreuse_tab); cwreuse_proofs=0;
        trust_reset(&trust_tab); trustagg_reset(&trustagg_tab);
        assert(cwreuse_offer_scoped(&cwreuse_tab,key,ecm_a,1,0x1884,0x1001,
                                    5,1,9000)==CWREUSE_NONE);

        /* The included production code runs synchronously here; no cache
         * worker is started in this isolated test process. */
        status=cache_setdcw(&req,key,NO_CYCLE,PEER_CSP|9);
        assert(!(status&DCW_ERROR) && stored.cwdata!=NULL);
        cache_csp_reuse_check(&peer,&req,key);

        assert(cwreuse_proofs==1);
        assert(trust_score_lk_scoped(&trust_tab,DCW_SOURCE_CACHE,PEER_CSP|9,
                                     0x1884,5,1,0x1002)==TRUST_START-TRUST_PROOF_STEP);
        assert(trust_score_lk_scoped(&trust_tab,DCW_SOURCE_CACHE,PEER_CSP|9,
                                     0x1884,0,0,0x1002)==-1);
        assert(cwneg_hit(&cwneg_tab,key,0x1884,5,1,0x1003,10001,NULL)==1);
        assert(cwneg_hit(&cwneg_tab,key,0x1884,0,1,0x1003,10001,NULL)==0);
        assert(cache_purge_should_withhold_scoped(&purge_tab,DCW_SOURCE_CACHE,
                    PEER_CSP|9,0x1884,0x1002,5,1)==1);
        assert(cache_purge_should_withhold_scoped(&purge_tab,DCW_SOURCE_CACHE,
                    PEER_CSP|9,0x1884,0x1002,0,0)==0);
        candidate=stored.cwdata;
        assert(candidate && (candidate->status&DCW_ERROR));
        table[idx]=NULL;
        free(candidate);
    }
#endif /* CACHEEX */

    /* The live cachepref_gate() must not turn provider-scope evidence into a
     * cross-scope defer. Exercise both directions with the real trust wrappers
     * and a providerless CSP cache hit versus a known numeric-zero hit. */
    {
        struct cache_data known_zero={0}, unknown={0};
        struct cw_cache_data candidate={0};
        int old_enabled=cachepref_enabled;
        const int peerid=PEER_CSP|9;

        known_zero.caid=unknown.caid=0x1884;
        known_zero.sid=unknown.sid=0x1002;
        known_zero.provider_known=1; known_zero.provid=0;
        unknown.provider_known=0; unknown.provid=0;
        candidate.peerid=peerid;
        cachepref_enabled=1;

        trust_reset(&trust_tab);
        trust_record_lk_scoped(&trust_tab,TRUST_EV_PROOF,DCW_SOURCE_CACHE,
                               peerid,0x1884,0,0,0x1002,1000);
        trust_record_lk_scoped(&trust_tab,TRUST_EV_PROOF,DCW_SOURCE_CACHE,
                               peerid,0x1884,0,0,0x1002,2000);
        assert(cachepref_gate(&known_zero,&candidate)==CACHEPREF_ALLOW);
        assert(cachepref_gate(&unknown,&candidate)==CACHEPREF_DEFER);

        trust_reset(&trust_tab);
        trust_record_lk_scoped(&trust_tab,TRUST_EV_PROOF,DCW_SOURCE_CACHE,
                               peerid,0x1884,0,1,0x1002,3000);
        trust_record_lk_scoped(&trust_tab,TRUST_EV_PROOF,DCW_SOURCE_CACHE,
                               peerid,0x1884,0,1,0x1002,4000);
        assert(cachepref_gate(&known_zero,&candidate)==CACHEPREF_DEFER);
        assert(cachepref_gate(&unknown,&candidate)==CACHEPREF_ALLOW);
        cachepref_enabled=old_enabled;
    }

    /* Scoped purge walks must not let an unknown-provider proof mark the
     * same numeric-zero channel entry whose provider is explicitly known, or
     * vice versa. Both records share the cache bucket and origin. */
    {
        struct cache_data known_zero={0}, unknown={0};
        struct cw_cache_data known_cw={0}, unknown_cw={0};
        struct purge_table purge={0};
        uint16_t sid=0x1003;
        int idx=sid & MAX_CACHE_INDEX;
        known_zero.caid=unknown.caid=0x1884;
        known_zero.sid=unknown.sid=sid;
        known_zero.provider_known=1;
        known_zero.local_provider_set=1; known_zero.local_provider=0;
        unknown.provid=0; unknown.provider_known=0;
        known_zero.next=&unknown; known_zero.prev=&unknown;
        unknown.next=&known_zero; unknown.prev=&known_zero;
        known_zero.cwdata=&known_cw; unknown.cwdata=&unknown_cw;
        known_cw.peerid=unknown_cw.peerid=PEER_CSP|11;
        table[idx]=&known_zero;

        assert(cache_purge_mark_scoped(&purge,DCW_SOURCE_CACHE,PEER_CSP|11,
                                       0x1884,sid,0,0,4000)==1);
        assert(cache_purge_sweep_scoped(&purge,DCW_SOURCE_CACHE,PEER_CSP|11,
                                        0x1884,sid,0,0,DCW_ERROR)==1);
        assert((unknown_cw.status & DCW_ERROR) && !(known_cw.status & DCW_ERROR));

        cache_purge_init(&purge);
        known_cw.status=unknown_cw.status=0;
        assert(cache_purge_mark_scoped(&purge,DCW_SOURCE_CACHE,PEER_CSP|11,
                                       0x1884,sid,0,1,4100)==1);
        assert(cache_purge_sweep_scoped(&purge,DCW_SOURCE_CACHE,PEER_CSP|11,
                                        0x1884,sid,0,1,DCW_ERROR)==1);
        assert((known_cw.status & DCW_ERROR) && !(unknown_cw.status & DCW_ERROR));
        table[idx]=NULL;
    }

    cfg.cache.threshold=1;
    target.caid=incoming.caid=0x1884; target.sid=incoming.sid=0x1136;
    target.recvtime=incoming.recvtime=9000;
    target.tag=incoming.tag=0x80; target.hash=1; incoming.hash=2;
    target.flags=CACHE_FLAG_SENDPIPE; target.cwcycle=CW1CYCLE;
    __atomic_store_n(&relay_waiter.lifetime_id,UINT64_C(0x303),__ATOMIC_RELEASE);
    ecm_request_set_dcwstatus(&relay_waiter,STAT_DCW_WAIT);
    assert(cache_ecm_snapshot_publish(&relay_waiter,&cache_test_profile));
    target.ecm=&relay_waiter; target.ecm_lifetime_id=UINT64_C(0x303);
    memset(target.prevcw, 0x11, 8); memset(target.prevcw+8, 0x22, 8);
    memset(cw, 0x11, 8); memset(cw+8, 0x33, 8);
    target.local_provider_set=incoming.local_provider_set=1;
    target.local_provider=0; incoming.local_provider=1;
    table[target.sid & MAX_CACHE_INDEX]=&target;
    cache_fetch_goodcw2(&incoming,cw,7);
    assert(sends==0); /* must not relay a locally known different provider */
    incoming.local_provider=0; cache_fetch_goodcw2(&incoming,cw,7); assert(sends==1);
    incoming.local_provider_set=0; cache_fetch_goodcw2(&incoming,cw,7); assert(sends==2);
    incoming.local_provider_set=1; incoming.local_provider=0; target.local_provider=1;
    cache_fetch_goodcw2(&incoming,cw,7); assert(sends==2); /* reverse, including zero */
    target.local_provider_set=0; cache_fetch_goodcw2(&incoming,cw,7); assert(sends==3);
    target.local_provider_set=1; target.local_provider=0;
    target.sid=0x1536; target.hash=incoming.hash;
    incoming.local_provider_set=1; incoming.local_provider=1;
    cache_fetch_goodcw3(&incoming,cw,7); assert(sends==3);
    incoming.local_provider=0; cache_fetch_goodcw3(&incoming,cw,7); assert(sends==4);
    incoming.local_provider_set=0; cache_fetch_goodcw3(&incoming,cw,7); assert(sends==5);
    incoming.local_provider_set=1; target.local_provider=1;
    cache_fetch_goodcw3(&incoming,cw,7); assert(sends==5); /* reverse */
    target.local_provider_set=0; cache_fetch_goodcw3(&incoming,cw,7); assert(sends==6);
    /* Explicit CacheEX metadata must be compared even without local waiter
     * associations; a known zero is not an unknown-provider wildcard. */
    target.local_provider_set=incoming.local_provider_set=0;
    target.provider_known=incoming.provider_known=1;
    target.provid=0; incoming.provid=1;
    target.sid=incoming.sid=0x1136; target.hash=1; incoming.hash=2;
    target.tag=incoming.tag=0x80; target.cwdata=NULL;
    cache_fetch_goodcw2(&incoming,cw,7);
    assert(sends==6 && record_conflicts==1);
    incoming.provid=0; incoming.provider_known=0;
    cache_fetch_goodcw2(&incoming,cw,7); assert(sends==7); /* unknown remains ambiguous */
    incoming.provider_known=1;
    cache_fetch_goodcw2(&incoming,cw,7); assert(sends==8); /* explicit zero matches */

    target.sid=0x1536; incoming.sid=0x1136;
    target.hash=incoming.hash=2; target.provid=1; incoming.provid=0;
    cache_fetch_goodcw3(&incoming,cw,7);
    assert(sends==8 && record_conflicts==2);
    incoming.provid=1; cache_fetch_goodcw3(&incoming,cw,7); assert(sends==9);

    /* The older-cycle walker must not reject based on a known other provider. */
    {
        struct cw_cache_data stored = {0}; cwcycle_t cycle = NO_CYCLE;
        memcpy(stored.cw, cw, sizeof(cw));
        target.sid=incoming.sid=0x1136; target.hash=1; incoming.hash=2;
        target.tag=0x80; incoming.tag=0x81; target.recvtime=1000;
        incoming.recvtime=9000; cfg.cache.filtertime=1000; target.cwdata=&stored;
        target.provider_known=incoming.provider_known=0;
        target.provid=incoming.provid=0;
        target.local_provider=0; target.local_provider_set=1;
        incoming.local_provider_set=1; incoming.local_provider=1;
        assert(cache_fetch_goodcw(&incoming,cw,&cycle)==0);
        incoming.local_provider=0;
        assert(cache_fetch_goodcw(&incoming,cw,&cycle)==DCW_ERROR);
        incoming.local_provider_set=0;
        assert(cache_fetch_goodcw(&incoming,cw,&cycle)==DCW_ERROR);

        /* The old-cycle walker must also keep distinct explicit CacheEX
         * providers separate, while preserving unknown CSP compatibility. */
        target.local_provider_set=0; target.provider_known=1; target.provid=0;
        incoming.provider_known=1; incoming.provid=1;
        assert(cache_fetch_goodcw(&incoming,cw,&cycle)==0);
        incoming.provid=0;
        assert(cache_fetch_goodcw(&incoming,cw,&cycle)==DCW_ERROR);
        incoming.provider_known=0;
        assert(cache_fetch_goodcw(&incoming,cw,&cycle)==DCW_ERROR);
    }
    assert(conflicts == 4);
    assert(record_conflicts == 2);
    puts("PASS: CSP/CacheEX provider scope is isolated across cache preference, trust, negative-memory, attestation, purge and reuse");
    return 0;
}
