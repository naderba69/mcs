/* scramchk.h — Scrambling Control Check (R22 candidate #5)
 *
 * PURPOSE: detect TS packets with wrong scrambling control bits.
 * A valid encrypted TS packet has transport_scrambling_control = 2 or 3
 * (even or odd key). Packets with 0 (not scrambled) or invalid values
 * indicate fake or corrupted data.
 *
 * MECHANISM: check scrambling_control bits (bits 6-7 of byte 3) in each
 * TS packet. Reject CW if sample contains non-scrambled packets where
 * scrambled ones are expected.
 *
 * CONFIG: automatic — validates every TS packet in cwverify sample.
 *
 * ROLLBACK: remove scramchk_check() calls; check is inert.
 */
#ifndef MCS_SCRAMCHK_H
#define MCS_SCRAMCHK_H

#include <stdint.h>

/* Scrambling control values (bits 6-7 of TS header byte 3) */
#define SCRAM_NOT_SCRAMBLED  0  /* 00: not scrambled */
#define SCRAM_RESERVED       1  /* 01: reserved (invalid) */
#define SCRAM_EVEN_KEY       2  /* 10: scrambled with even key */
#define SCRAM_ODD_KEY        3  /* 11: scrambled with odd key */

/* Extract scrambling control from TS packet.
 * Returns: scrambling_control (0-3), or -1 if invalid packet.
 */
static inline int scramchk_get(const uint8_t *ts_pkt)
{
    if (!ts_pkt) return -1;
    if (ts_pkt[0] != 0x47) return -1;  /* sync byte */
    return (ts_pkt[3] >> 6) & 0x03;
}

/* Check if a TS packet is properly scrambled.
 * Returns: 1 = scrambled (even or odd), 0 = not scrambled or invalid.
 */
static inline int scramchk_is_scrambled(const uint8_t *ts_pkt)
{
    int scram = scramchk_get(ts_pkt);
    return (scram == SCRAM_EVEN_KEY || scram == SCRAM_ODD_KEY);
}

/* Validate scrambling across a sample of TS packets.
 * Returns: percentage of properly scrambled packets (0-100).
 *
 * sample: array of TS packets (each 188 bytes)
 * sample_len: total bytes in sample
 * threshold: minimum percentage required (typically 80-90%)
 *
 * A CW is considered valid only if the decrypted sample has
 * scrambling percentage >= threshold.
 */
static inline int scramchk_validate(const uint8_t *sample, int sample_len,
                                     int threshold)
{
    int i, pkt_count, scrambled_count;
    int percentage;

    if (!sample || sample_len < 188) return 0;

    pkt_count = sample_len / 188;
    scrambled_count = 0;

    for (i = 0; i < pkt_count; i++) {
        const uint8_t *pkt = &sample[i * 188];
        if (scramchk_is_scrambled(pkt)) {
            scrambled_count++;
        }
    }

    if (pkt_count == 0) return 0;
    percentage = (scrambled_count * 100) / pkt_count;

    return (percentage >= threshold);
}

/* Count scrambling control distribution in a sample.
 * Fills counts[4] with: [not_scrambled, reserved, even, odd].
 */
static inline void scramchk_distribution(const uint8_t *sample,
                                          int sample_len,
                                          int counts[4])
{
    int i, pkt_count, scram;

    if (!sample || !counts) return;

    counts[0] = counts[1] = counts[2] = counts[3] = 0;
    pkt_count = sample_len / 188;

    for (i = 0; i < pkt_count; i++) {
        const uint8_t *pkt = &sample[i * 188];
        scram = scramchk_get(pkt);
        if (scram >= 0 && scram <= 3) {
            counts[scram]++;
        }
    }
}

#endif /* MCS_SCRAMCHK_H */
