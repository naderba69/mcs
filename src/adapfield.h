/* adapfield.h — Adaptation Field Validation (R22 candidate #3)
 *
 * PURPOSE: validate TS packets by checking adaptation field structure.
 * Detects malformed or corrupted TS packets before CW validation,
 * preventing fake CWs from being accepted due to bad input data.
 *
 * MECHANISM: parse TS adaptation field (continuity counter, PCR, flags),
 * verify structure integrity, reject packets with invalid adaptation.
 *
 * CONFIG: automatic — validates every TS packet in cwverify sample.
 *
 * ROLLBACK: remove adapfield_validate() calls; validation is inert.
 */
#ifndef MCS_ADAPFIELD_H
#define MCS_ADAPFIELD_H

#include <stdint.h>

/* TS packet adaptation field flags */
#define ADAPFIELD_DISCONTINUITY  0x80
#define ADAPFIELD_RANDOM_ACCESS  0x40
#define ADAPFIELD_PCR            0x10
#define ADAPFIELD_OPCR           0x08
#define ADAPFIELD_SPLICING       0x04
#define ADAPFIELD_PRIVATE        0x02
#define ADAPFIELD_EXTENSION      0x01

/* Validate TS adaptation field structure.
 * Returns: 1 = valid, 0 = invalid/malformed.
 *
 * ts_pkt: pointer to 188-byte TS packet (starts with 0x47)
 * adap_ctrl: adaptation_field_control (bits 4-5 of byte 3)
 *   00 = reserved, 01 = payload only, 10 = adaptation only, 11 = both
 */
static inline int adapfield_validate(const uint8_t *ts_pkt, int adap_ctrl)
{
    int adap_len, adap_flags;
    const uint8_t *adap;

    if (!ts_pkt) return 0;
    if (ts_pkt[0] != 0x47) return 0;  /* sync byte */

    /* adaptation_field_control: 01 = payload only (no adaptation) */
    if (adap_ctrl == 1) return 1;

    /* 00 = reserved (invalid) */
    if (adap_ctrl == 0) return 0;

    /* 10 or 11: adaptation field present */
    adap = &ts_pkt[4];
    adap_len = adap[0];

    /* Length 0 is valid (stuffing only) */
    if (adap_len == 0) return 1;

    /* Check if adaptation field fits in packet */
    if (4 + 1 + adap_len > 188) return 0;

    /* Parse flags */
    adap_flags = adap[1];

    /* PCR flag: requires 6 bytes after flags */
    if ((adap_flags & ADAPFIELD_PCR) && adap_len < 7) return 0;

    /* OPCR flag: requires 6 more bytes */
    if ((adap_flags & ADAPFIELD_OPCR)) {
        int needed = 2;  /* flags + length */
        if (adap_flags & ADAPFIELD_PCR) needed += 6;
        needed += 6;  /* OPCR */
        if (adap_len < needed) return 0;
    }

    /* Splicing point: requires 1 byte (splice countdown) */
    if ((adap_flags & ADAPFIELD_SPLICING)) {
        int needed = 2;
        if (adap_flags & ADAPFIELD_PCR) needed += 6;
        if (adap_flags & ADAPFIELD_OPCR) needed += 6;
        needed += 1;
        if (adap_len < needed) return 0;
    }

    return 1;
}

/* Extract continuity counter from TS packet.
 * Returns: continuity counter (0-15), or -1 if invalid.
 */
static inline int adapfield_cc(const uint8_t *ts_pkt)
{
    if (!ts_pkt || ts_pkt[0] != 0x47) return -1;
    return ts_pkt[3] & 0x0F;
}

/* Check if continuity counter is valid (increments by 1, wraps at 15).
 * Returns: 1 = valid transition, 0 = discontinuity detected.
 */
static inline int adapfield_cc_check(int prev_cc, int curr_cc)
{
    int expected;
    if (prev_cc < 0 || curr_cc < 0) return 0;
    expected = (prev_cc + 1) & 0x0F;
    return (curr_cc == expected);
}

#endif /* MCS_ADAPFIELD_H */
