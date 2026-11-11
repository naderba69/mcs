#ifndef MCS_CACHE_PIPE_CODEC_H
#define MCS_CACHE_PIPE_CODEC_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "pipe.h"


#define CACHEEX_PIPE_PUSH_LOCAL_LEN  45u
#define CACHEEX_PIPE_PUSH_REMOTE_LEN 53u
#define CACHEEX_PIPE_PUSH_IN_LEN     48u
#define CACHEEX_PIPE_WAKEUP_LEN       2u

/* PIPE_CACHE_FIND_SUCCESS is private cache-worker -> ECM-worker IPC. Its
 * source mask and an optional line-verification request follow the existing
 * peer ID, CW, and declared cycle marker. These bytes never cross a peer link. */
#define CACHE_PIPE_FIND_SUCCESS_PEERID_LEN       4u
#define CACHE_PIPE_FIND_SUCCESS_CW_LEN          16u
#define CACHE_PIPE_FIND_SUCCESS_CYCLE_LEN        1u
#define CACHE_PIPE_FIND_SUCCESS_ORIGIN_MASK_LEN  1u
#define CACHE_PIPE_FIND_SUCCESS_VERIFY_LEN       1u
#define CACHE_PIPE_FIND_SUCCESS_EXTRA_LEN        \
	(CACHE_PIPE_FIND_SUCCESS_PEERID_LEN + \
	 CACHE_PIPE_FIND_SUCCESS_CW_LEN + \
	 CACHE_PIPE_FIND_SUCCESS_CYCLE_LEN + \
	 CACHE_PIPE_FIND_SUCCESS_ORIGIN_MASK_LEN + \
	 CACHE_PIPE_FIND_SUCCESS_VERIFY_LEN)

/* Internal PIPE_CACHE_REPLY suffix: legacy cache-origin behavior byte, then
 * the additive local source-class mask. Neither is a peer-protocol field. */
#define CACHE_PIPE_REPLY_CW_LEN                  16u
#define CACHE_PIPE_REPLY_TRAILER_LEN              2u
#define CACHE_PIPE_REPLY_FROM_CACHE_OFFSET       0u
#define CACHE_PIPE_REPLY_ORIGIN_MASK_OFFSET      1u

/* CACHEEX queue payload sizes are an internal wire contract. Validate the
 * command and exact frame length before consumers touch any field offsets. */
static inline int cacheex_pipe_frame_length_valid(uint8_t command,
                                                   size_t frame_len)
{
    switch (command) {
    case PIPE_WAKEUP:
        return frame_len == CACHEEX_PIPE_WAKEUP_LEN;
    case PIPE_CACHEEX_PUSH_LOCAL:
        return frame_len == CACHEEX_PIPE_PUSH_LOCAL_LEN;
    case PIPE_CACHEEX_PUSH_REMOTE:
        return frame_len == CACHEEX_PIPE_PUSH_REMOTE_LEN;
    case PIPE_CACHEEX_PUSH_IN:
        return frame_len == CACHEEX_PIPE_PUSH_IN_LEN;
    default:
        return 0;
    }
}

/*
 * Copy the optional 16-byte CW suffix from a bounded cache-pipe payload.
 * If the suffix is absent or truncated, leave `out` zeroed and do not read
 * beyond the frame. Returns 1 when a complete CW suffix was present.
 */
static inline int cache_pipe_copy_optional_cw(const uint8_t *frame,
                                               size_t frame_len,
                                               size_t cw_offset,
                                               uint8_t out[16])
{
    if (!out) return 0;
    memset(out, 0, 16);
    if (!frame || cw_offset > frame_len || frame_len - cw_offset < 16u)
        return 0;
    memcpy(out, frame + cw_offset, 16);
    return 1;
}

#endif /* MCS_CACHE_PIPE_CODEC_H */
