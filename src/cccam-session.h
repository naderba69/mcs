#ifndef CCCAM_SESSION_H
#define CCCAM_SESSION_H

#include <stdint.h>

/* Each accepted CCcam connection gets a nonzero token. A CAS loop keeps the
 * zero-reserved value skipped even when accepts run concurrently at wraparound. */
static inline uint32_t cccam_session_generation_next(uint32_t *counter)
{
	uint32_t current = __atomic_load_n(counter, __ATOMIC_RELAXED);
	for (;;) {
		uint32_t next = current + 1;
		if (!next) next = 1;
		if (__atomic_compare_exchange_n(counter, &current, next, 0,
		                                __ATOMIC_RELAXED, __ATOMIC_RELAXED))
			return next;
	}
}

/* expected_sock <= 0 is used by epoll, whose token already carries the client
 * identity and generation. Poll users also bind the snapshot to its fd. */
static inline int cccam_session_matches(int current_sock, int connection_status,
                                        uint32_t current_generation,
                                        int expected_sock,
                                        uint32_t expected_generation)
{
	return expected_generation != 0 && current_sock > 0 &&
	       (expected_sock <= 0 || current_sock == expected_sock) &&
	       connection_status > 0 &&
	       current_generation == expected_generation;
}

/* Epoll reserves token zero for its control pipe. */
static inline uint64_t cccam_session_epoll_token(uint32_t client_id,
                                                 uint32_t generation)
{
	if (!client_id || !generation) return 0;
	return ((uint64_t)generation << 32) | (uint64_t)client_id;
}

static inline int cccam_session_epoll_token_decode(uint64_t token,
                                                    uint32_t *client_id,
                                                    uint32_t *generation)
{
	uint32_t id = (uint32_t)token;
	uint32_t gen = (uint32_t)(token >> 32);
	if (!token || !id || !gen || !client_id || !generation) return 0;
	*client_id = id;
	*generation = gen;
	return 1;
}

#endif
