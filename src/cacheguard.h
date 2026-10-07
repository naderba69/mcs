/*
 * cacheguard.h -- TASK 2.9, the cache protocol guard (ingress validation)
 *
 * THE DEFECTS THIS CLOSES, ALL VERIFIED AGAINST THE TREE BEFORE A LINE WAS
 * WRITTEN. cache_recvmsg() dispatches on buf[0] and the handlers read fixed
 * offsets. Five of them read offsets the datagram they received may not
 * carry:
 *
 *   TYPE_REQUEST  reads buf[1..11]  -- no length check at all
 *   TYPE_REPLY    reads buf[12]     -- no length check before the integrity test
 *   TYPE_PINGREQ  reads buf[11..12] -- no length check at all
 *   TYPE_PINGRPL  reads buf[4..5]   -- no length check at all
 *   TYPE_HELLO_ACK reads buf[4..8]  -- no length check at all
 *
 * `received` can be as low as 2 (the only global floor is (received<2)), and
 * everything past it in the 2048-byte stack buffer is UNINITIALISED MEMORY.
 * The handlers then decide on that garbage: a REQUEST is keyed by a garbage
 * hash and creates a cache entry that answers to nothing, a PINGREQ anchors a
 * peer at a garbage port (and under CACHE AUTOADD may CREATE one there), a
 * PINGRPL moves a real peer's receive port to garbage. Not exploitable into
 * memory unsafety -- the buffer is big and the reads stay inside it -- but it
 * is state corruption by datagram, and it is invisible: nothing is counted,
 * nothing is logged.
 *
 * THE DOCTRINE. Uninterpretable is not decidable: a datagram shorter than its
 * type's minimum is dropped BEFORE any parse, and the drop is counted and
 * logged (bounded: one line per kind per 60 s window, counters always).
 * This is not a shape decision and GR3 is not implicated -- nothing here
 * looks at the content of a well-formed control word, and every sender that
 * was already speaking the protocol correctly sees exactly the same behaviour
 * as before. The mins below are satisfied by this server's own senders
 * (REQUEST 12, REPLY 29/30, PINGREQ 13, PINGRPL 9, RESENDREQ 16, HELLO_ACK 9)
 * and by the harness peers; the >=29 CW check on TYPE_REPLY and >=16 on
 * TYPE_RESENDREQ stay where upstream put them.
 *
 * THE SECOND HOLE IS SILENCE, NOT CORRUPTION. Packets from senders that are
 * not configured peers are dropped at `if (!peer) break;` with no counter and
 * no line (one debug-level ping alert aside). Most of the time that sender is
 * a MISCONFIGURED legitimate peer -- wrong port, NAT rebinding -- and the
 * operator's "why is my peer not sharing?" question had no answer. The guard
 * counts those packets and says who is sending them, once per window. Their
 * treatment is unchanged: stock dropped them before any layer, and still does.
 *
 * SINGLE-THREADED BY CONSTRUCTION. Every hook runs in cache_recvmsg(), on the
 * cache thread; the stats snapshot that reads the counters runs on the same
 * thread's tick. No lock is taken and none is needed.
 */
#ifndef MCS_CACHEGUARD_H
#define MCS_CACHEGUARD_H

#include <stdint.h>
#include <stdio.h>

#define CG_WINDOW_MS 60000   /* one line per kind per window */

/*
 * Minimum datagram length per packet type, from the offsets each handler in
 * cache_recvmsg() actually reads. Unknown types keep the stock global floor
 * of 2 -- the default handler only reads buf[0], and inventing a minimum for
 * packet shapes this server does not speak would be a policy, not a fact.
 */
static inline int cg_minlen(uint8_t type)
{
	switch (type) {
	case 1:    return 12;   /* TYPE_REQUEST   -- reads buf[1..11]            */
	case 2:    return 13;   /* TYPE_REPLY     -- reads buf[1..12]            */
	case 3:    return 13;   /* TYPE_PINGREQ   -- reads buf[1..3], buf[11..12] */
	case 4:    return 6;    /* TYPE_PINGRPL   -- reads buf[4..5]             */
	case 5:    return 16;   /* TYPE_RESENDREQ -- upstream gates <16 itself   */
	case 0x10: return 9;    /* TYPE_HELLO_ACK -- reads buf[4..8]             */
	default:   return 2;    /* everything else: the stock global floor      */
	}
}

/* Names for the line; unknown types are printed numerically by the caller. */
static inline const char *cg_typename(uint8_t type)
{
	switch (type) {
	case 1:    return "TYPE_REQUEST";
	case 2:    return "TYPE_REPLY";
	case 3:    return "TYPE_PINGREQ";
	case 4:    return "TYPE_PINGRPL";
	case 5:    return "TYPE_RESENDREQ";
	case 0x10: return "TYPE_HELLO_ACK";
	default:   return "UNKNOWN-TYPE";
	}
}

/*
 * The once-per-window throttle. Returns 1 on the event that opens (or reopens
 * -- the window ended) a window: that event writes the line. Every other
 * event in the window is counted in w->count and stays silent; the caller
 * passes w->count into the sentence so the suppression itself is stated.
 * `opened` uses tick 1 for an event at tick 0 so a boot-time packet cannot
 * hold the window open for a wrap.
 */
struct cg_window {
	uint32_t opened;
	uint32_t count;
};

static inline int cg_window_event(struct cg_window *w, uint32_t now)
{
	if (w->opened == 0 || (int32_t)(now - w->opened) >= (int32_t)CG_WINDOW_MS) {
		w->opened = now ? now : 1;
		w->count = 1;
		return 1;
	}
	w->count++;
	return 0;
}

/* The ip is printed the way iptoa() prints it everywhere else in this server
 * (least-significant octet first): recv_ip comes straight out of sin_addr,
 * so 127.0.0.1 arrives as 0x0100007f and prints as 127.0.0.1. */
static inline int cg_format_short(char *line, int sz, int type, int received,
                                  int minlen, uint32_t ip, int port,
                                  uint32_t window_count)
{
	if (!line || sz <= 0 || received < 0) return -1;
	return snprintf(line, (size_t)sz,
		"CACHE GUARD: dropped a short %s (type %d) from %u.%u.%u.%u:%d -- "
		"%d bytes, this type needs %d; nothing was parsed, the datagram "
		"cannot be decoded (%lu in this window, counted in the stats only)",
		cg_typename((uint8_t)type), type,
		ip & 0xff, (ip >> 8) & 0xff, (ip >> 16) & 0xff, (ip >> 24) & 0xff,
		port, received, minlen, (unsigned long)window_count);
}

static inline int cg_format_unknown(char *line, int sz, int type,
                                    uint32_t ip, int port,
                                    uint32_t window_count)
{
	if (!line || sz <= 0) return -1;
	return snprintf(line, (size_t)sz,
		"CACHE GUARD: %lu packet(s) in this window from unconfigured "
		"senders -- last: a %s (%d) from %u.%u.%u.%u:%d; the cache "
		"exchange runs between configured peers only, and stock dropped "
		"this before any layer saw it",
		(unsigned long)window_count, cg_typename((uint8_t)type), type,
		ip & 0xff, (ip >> 8) & 0xff, (ip >> 16) & 0xff, (ip >> 24) & 0xff,
		port);
}

#endif /* MCS_CACHEGUARD_H */
