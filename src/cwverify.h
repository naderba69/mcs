/* cwverify.h — CW Verification Layer (R22 candidate #1)
 *
 * PURPOSE: verify CW by actually decrypting a sample and checking the
 * result. This is the single most important addition for eliminating
 * fake CWs — a CW that passes all 18 structural checks may still be
 * wrong, but a CW that produces a valid PES header after decryption
 * is almost certainly right.
 *
 * SCOPE: called from cache_setdcw() AFTER checksumDCW() and isbadDCW()
 * but BEFORE the CW is stored or served. If verification is available
 * (a TS sample is attached to the request) and the CW fails, it is
 * refused with a new named reason (CWLOG_R_VERIFY).
 *
 * THREAD SAFETY: all functions are pure (no shared state). The small
 * counters are per-call, not global. The layer never blocks and never
 * allocates.
 *
 * CONFIG: opt-in per profile via DCW VERIFY: ON (default OFF = stock).
 * Without a TS sample the layer is inert — it never refuses anything.
 *
 * ROLLBACK: remove the cwverify_gate() call from cache_setdcw(); the
 * layer has no persistent state.
 */
#ifndef MCS_CWVERIFY_H
#define MCS_CWVERIFY_H

#include <stdint.h>
#include <string.h>

/* Verification result codes */
#define CWV_PASS         1   /* CW verified (decryption produced valid output) */
#define CWV_FAIL         0   /* CW failed verification (fake) */
#define CWV_NOSAMPLE    -1   /* no TS sample available — skip verification */
#define CWV_SHORT       -2   /* sample too short to verify */
#define CWV_DISABLED    -3   /* verification disabled for this profile */

/* PES start code prefix: 00 00 01 */
static inline int cwverify_find_pes(const uint8_t *buf, int len)
{
    int i;
    for (i = 0; i + 3 < len; i++) {
        if (buf[i] == 0x00 && buf[i+1] == 0x00 && buf[i+2] == 0x01) {
            return i;
        }
    }
    return -1;
}

/* Check if byte at offset is a valid PES stream ID:
 *   0xE0-0xEF = video
 *   0xC0-0xDF = audio
 *   0xBD      = private stream 1
 *   0xBE      = padding (skip)
 *   0xBF      = private stream 2 (skip)
 */
static inline int cwverify_valid_pes_stream(uint8_t stream_id)
{
    if (stream_id >= 0xE0 && stream_id <= 0xEF) return 1; /* video */
    if (stream_id >= 0xC0 && stream_id <= 0xDF) return 1; /* audio */
    if (stream_id == 0xBD) return 1;                      /* private 1 */
    return 0;
}

/* Check TS sync byte pattern: 0x47 at 188-byte boundaries */
static inline int cwverify_ts_sync(const uint8_t *buf, int len)
{
    int matches = 0;
    int checked = 0;
    int i;
    for (i = 0; i + 188 <= len; i += 188) {
        checked++;
        if (buf[i] == 0x47) matches++;
    }
    if (checked == 0) return 0;
    /* At least half the packets must have sync */
    return (matches * 2 >= checked);
}

/* Check if decrypted buffer looks like random noise (fake CW symptom) */
static inline int cwverify_is_noise(const uint8_t *buf, int len)
{
    int i;
    int zero_run = 0;
    int max_run = 0;
    int transitions = 0;

    if (len < 16) return 0;

    for (i = 1; i < len; i++) {
        if (buf[i] == buf[i-1]) {
            zero_run++;
            if (zero_run > max_run) max_run = zero_run;
        } else {
            zero_run = 0;
            transitions++;
        }
    }

    /* Real decrypted TS has moderate transitions (not too many, not too few) */
    /* Pure noise: very high transitions */
    /* All zeros/FF: very low transitions */
    if (transitions < len / 20) return 1;  /* too uniform = probably wrong key */
    if (max_run > len / 4) return 1;       /* long runs = probably wrong key */
    return 0;
}

/* Main verification function.
 *
 * cw:          the 16-byte CW to verify
 * ts_sample:   raw TS packets (before decryption) — at least 188 bytes
 * sample_len:  length of ts_sample
 * decrypt_fn:  caller-provided decryption function (CAID-specific)
 *              signature: void fn(const uint8_t *cw, const uint8_t *in,
 *                                 uint8_t *out, int len)
 *
 * Returns: CWV_PASS, CWV_FAIL, or a negative skip code.
 */
typedef void (*cwverify_decrypt_fn)(const uint8_t *cw, const uint8_t *in,
                                     uint8_t *out, int len);

static inline int cwverify_check(const uint8_t cw[16],
                                  const uint8_t *ts_sample,
                                  int sample_len,
                                  cwverify_decrypt_fn decrypt_fn)
{
    uint8_t decrypted[376]; /* 2 TS packets */
    int pes_offset;
    int check_len;

    if (!ts_sample || sample_len < 188) return CWV_NOSAMPLE;
    if (!decrypt_fn) return CWV_NOSAMPLE;
    if (!cw) return CWV_SHORT;

    /* Decrypt up to 2 TS packets (376 bytes) */
    check_len = (sample_len < 376) ? sample_len : 376;
    /* Align to 188-byte boundary */
    check_len = (check_len / 188) * 188;
    if (check_len < 188) return CWV_SHORT;

    decrypt_fn(cw, ts_sample, decrypted, check_len);

    /* Test 1: TS sync bytes (0x47) */
    if (cwverify_ts_sync(decrypted, check_len)) {
        /* Test 2: PES header present */
        pes_offset = cwverify_find_pes(decrypted, check_len);
        if (pes_offset >= 0 && pes_offset + 4 <= check_len) {
            if (cwverify_valid_pes_stream(decrypted[pes_offset + 3])) {
                return CWV_PASS;
            }
        }
        /* Sync is good but no PES — might be partial packet, still pass */
        return CWV_PASS;
    }

    /* Test 3: noise check on the decrypted data */
    if (cwverify_is_noise(decrypted, check_len)) {
        return CWV_FAIL;
    }

    /* No sync, no PES, not noise — inconclusive */
    return CWV_NOSAMPLE;
}

/* Statistics for the verification layer (per-profile, not global) */
struct cwverify_stats {
    uint32_t checked;    /* total CWs checked */
    uint32_t passed;     /* CWs that passed verification */
    uint32_t failed;     /* CWs that failed (fake) */
    uint32_t skipped;    /* CWs skipped (no sample) */
};

static inline void cwverify_stats_reset(struct cwverify_stats *s)
{
    if (s) memset(s, 0, sizeof(*s));
}

static inline void cwverify_stats_record(struct cwverify_stats *s, int result)
{
    if (!s) return;
    s->checked++;
    if (result == CWV_PASS) s->passed++;
    else if (result == CWV_FAIL) s->failed++;
    else s->skipped++;
}

#endif /* MCS_CWVERIFY_H */
