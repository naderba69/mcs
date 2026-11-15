/* cwdup.c -- Band 56 regression for CW_DUP (constant CW across cycles).
 *
 * The fault: one CW keeps being delivered for NEW ECM cycles while the
 * channel demonstrably rolls (a different CW was accepted recently).
 * Drives the real supersmartfilter_check() with every other SSF check
 * disabled:
 *   - same-CW repeats only count across different ecm_hash values
 *     (same-cycle resends never count);
 *   - a constant key with NO competitor never flags;
 *   - a normal two-key roll (A,B,A,B) never flags;
 *   - with a competitor, the flag fires at REP new-cycle repeats:
 *     log mode accepts + counts, mode 2 rejects with reason 11;
 *   - per-profile override beats the global mode in both directions;
 *   - without ecm_hash nothing is counted (fail-open).
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

struct ssf_config;
#include "../src/supersmartfilter.h"
struct ssf_config ssf_cfg; /* definition for the extern in the header */

static int fails = 0;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static const uint8_t CW_A[16] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88, 0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x10};
static const uint8_t CW_B[16] = {0x21,0x32,0x43,0x54,0x65,0x76,0x87,0x98, 0x41,0x52,0x63,0x74,0x85,0x96,0xA7,0xB8};

static void hash_of(uint8_t out[16], unsigned seed)
{
    int i;
    for (i = 0; i < 16; i++) out[i] = (uint8_t)(seed * 37u + (unsigned)i * 11u + 5u);
}

static void setup(int global_mode, int rep)
{
    supersmartfilter_init();
    ssf_cfg.filter_level = 2;
    ssf_cfg.blockfakecws = 0;
    ssf_cfg.sf_entropy = 0;
    ssf_cfg.sf_pattern = 0;
    ssf_cfg.sf_dupseen = 0;
    ssf_cfg.sf_multicheck = 0;
    ssf_cfg.sf_marchcheck = 0;
    ssf_cfg.sf_cwfmt_auto = 0;
    ssf_cfg.sf_cwfmt_enforce = 0;
    ssf_cfg.sf_cwdup = global_mode;
    ssf_cfg.sf_cwdup_rep = rep;
}

static int check(const uint8_t *cw, const uint8_t *hash, uint16_t sid,
                 uint32_t now_ms, int prof_dup, int *reason)
{
    return supersmartfilter_check(cw, hash, 0x183D, 0, sid, 0, now_ms, reason, 0, 0, prof_dup, 0);
}

int main(void)
{
    int reason = 0;
    struct ssf_stats st;
    uint8_t h[16];
    uint32_t t;

    /* --- Constant key, no competitor: never flags (even mode 2) --- */
    setup(2, 3);
    for (t = 0; t < 6; t++) {
        hash_of(h, 100 + t);
        EXPECT(check(CW_A, h, 0x2001, 100000 + t * 10000, 0, &reason) == 0,
               "constant: accepted");
    }
    supersmartfilter_get_stats(&st);
    EXPECT(st.cwdup_flagged == 0, "constant: no competitor, no flag");

    /* --- Same-cycle resends never count --- */
    setup(2, 2);
    hash_of(h, 7);
    EXPECT(check(CW_A, h, 0x2002, 100000, 0, &reason) == 0, "resend: competitor B first next");
    hash_of(h, 8);
    EXPECT(check(CW_B, h, 0x2002, 110000, 0, &reason) == 0, "resend: B accepted");
    hash_of(h, 9);
    EXPECT(check(CW_A, h, 0x2002, 120000, 0, &reason) == 0, "resend: A cycle 1");
    EXPECT(check(CW_A, h, 0x2002, 121000, 0, &reason) == 0, "resend: A same cycle again");
    EXPECT(check(CW_A, h, 0x2002, 122000, 0, &reason) == 0, "resend: A same cycle thrice");
    supersmartfilter_get_stats(&st);
    EXPECT(st.cwdup_flagged == 0, "resend: same ecm_hash never counts");

    /* --- Normal two-key roll never flags --- */
    setup(2, 2);
    for (t = 0; t < 6; t++) {
        hash_of(h, 200 + t);
        EXPECT(check((t % 2) ? CW_B : CW_A, h, 0x2003, 100000 + t * 10000, 0, &reason) == 0,
               "roll: accepted");
    }
    supersmartfilter_get_stats(&st);
    EXPECT(st.cwdup_flagged == 0, "roll: alternating keys never flag");

    /* --- Stuck CW with competitor: log mode flags but accepts --- */
    setup(1, 3);
    hash_of(h, 1); EXPECT(check(CW_A, h, 0x2004, 100000, 0, &reason) == 0, "log: A");
    hash_of(h, 2); EXPECT(check(CW_B, h, 0x2004, 110000, 0, &reason) == 0, "log: B competitor");
    hash_of(h, 3); EXPECT(check(CW_A, h, 0x2004, 120000, 0, &reason) == 0, "log: A back");
    hash_of(h, 4); EXPECT(check(CW_A, h, 0x2004, 130000, 0, &reason) == 0, "log: A rep1");
    hash_of(h, 5); EXPECT(check(CW_A, h, 0x2004, 140000, 0, &reason) == 0, "log: A rep2");
    hash_of(h, 6); EXPECT(check(CW_A, h, 0x2004, 150000, 0, &reason) == 0, "log: A rep3 accepted (log)");
    supersmartfilter_get_stats(&st);
    EXPECT(st.cwdup_flagged >= 1, "log: flagged at REP");
    EXPECT(st.rejected_cwdup == 0, "log: nothing rejected");

    /* --- Same sequence, mode 2: rejects with reason 11 at REP --- */
    setup(2, 3);
    hash_of(h, 1); EXPECT(check(CW_A, h, 0x2005, 100000, 0, &reason) == 0, "rej: A");
    hash_of(h, 2); EXPECT(check(CW_B, h, 0x2005, 110000, 0, &reason) == 0, "rej: B competitor");
    hash_of(h, 3); EXPECT(check(CW_A, h, 0x2005, 120000, 0, &reason) == 0, "rej: A back");
    hash_of(h, 4); EXPECT(check(CW_A, h, 0x2005, 130000, 0, &reason) == 0, "rej: A rep1");
    hash_of(h, 5); EXPECT(check(CW_A, h, 0x2005, 140000, 0, &reason) == 0, "rej: A rep2");
    hash_of(h, 6); reason = 0;
    EXPECT(check(CW_A, h, 0x2005, 150000, 0, &reason) == 1, "rej: A rep3 rejected");
    EXPECT(reason == 11, "rej: reason code 11");
    /* A fresh legitimate CW still passes afterwards. */
    hash_of(h, 7); EXPECT(check(CW_B, h, 0x2005, 160000, 0, &reason) == 0, "rej: B after reject accepted");
    supersmartfilter_get_stats(&st);
    EXPECT(st.rejected_cwdup == 1, "rej: exactly one cwdup rejection");

    /* --- Profile override in both directions --- */
    setup(0, 2); /* global off, profile reject */
    hash_of(h, 1); EXPECT(check(CW_A, h, 0x2006, 100000, 3, &reason) == 0, "prof: A");
    hash_of(h, 2); EXPECT(check(CW_B, h, 0x2006, 110000, 3, &reason) == 0, "prof: B");
    hash_of(h, 3); EXPECT(check(CW_A, h, 0x2006, 120000, 3, &reason) == 0, "prof: A back");
    hash_of(h, 4); EXPECT(check(CW_A, h, 0x2006, 130000, 3, &reason) == 0, "prof: A rep1");
    hash_of(h, 5); EXPECT(check(CW_A, h, 0x2006, 140000, 3, &reason) == 1, "prof: override 3 rejects, global off");
    setup(2, 2); /* global reject, profile off */
    hash_of(h, 1); EXPECT(check(CW_A, h, 0x2007, 100000, 1, &reason) == 0, "prof-off: A");
    hash_of(h, 2); EXPECT(check(CW_B, h, 0x2007, 110000, 1, &reason) == 0, "prof-off: B");
    hash_of(h, 3); EXPECT(check(CW_A, h, 0x2007, 120000, 1, &reason) == 0, "prof-off: A back");
    hash_of(h, 4); EXPECT(check(CW_A, h, 0x2007, 130000, 1, &reason) == 0, "prof-off: A rep1");
    hash_of(h, 5); EXPECT(check(CW_A, h, 0x2007, 140000, 1, &reason) == 0, "prof-off: override 1 disables");
    supersmartfilter_get_stats(&st);
    EXPECT(st.cwdup_flagged == 0, "prof-off: no flags when profile forces off");

    /* --- No ecm_hash: fail-open, nothing counted --- */
    setup(2, 2);
    EXPECT(check(CW_A, NULL, 0x2008, 100000, 0, &reason) == 0, "nohash: A");
    EXPECT(check(CW_B, NULL, 0x2008, 110000, 0, &reason) == 0, "nohash: B");
    EXPECT(check(CW_A, NULL, 0x2008, 120000, 0, &reason) == 0, "nohash: A again");
    EXPECT(check(CW_A, NULL, 0x2008, 130000, 0, &reason) == 0, "nohash: A thrice");
    supersmartfilter_get_stats(&st);
    EXPECT(st.cwdup_flagged == 0, "nohash: unprovable cycle boundary never flags");

    if (fails) { printf("cwdup: %d FAILURES\n", fails); return 1; }
    printf("cwdup: PASS (constant/roll/resend safe, stuck flags+rejects, override, fail-open)\n");
    return 0;
}
