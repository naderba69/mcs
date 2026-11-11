#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/r48-source-reputation.h"

static void query_score(struct r48_source_rep_table *table,
                        int srctype, int srcid, uint16_t caid,
                        uint32_t provid, int provider_known,
                        uint32_t now, uint8_t expected)
{
    struct r48_source_rep_snapshot snapshot;
    assert(r48_source_rep_query(table, srctype, srcid, caid, provid,
                                provider_known, now, &snapshot));
    assert(snapshot.score == expected);
}

int main(void)
{
    struct r48_source_rep_table table;
    struct r48_source_rep_snapshot snapshot;
    struct r48_source_rep_entry *entry;
    uint32_t now = 100u;
    unsigned i;

    r48_source_rep_init(&table);
    assert(!r48_source_rep_query(&table, 1, 7, 0x1814, 0x123456u,
                                 1, now, &snapshot));

    /* Positive evidence is only meaningful after the caller has a D-1
     * reference-match event. The table keeps the provider scope, not SID. */
    assert(r48_source_rep_note(&table, 1, 7, 0x1814, 0x123456u,
                               1, R48_SOURCE_REP_REFERENCE_MATCH, now));
    query_score(&table, 1, 7, 0x1814, 0x123456u, 1, now, 56);
    assert(r48_source_rep_note(&table, 1, 7, 0x1814, 0x123456u,
                               1, R48_SOURCE_REP_REFERENCE_MATCH, now + 1u));
    query_score(&table, 1, 7, 0x1814, 0x123456u, 1, now + 1u, 62);

    /* An explicit known provider zero is separate from an unknown provider. */
    assert(!r48_source_rep_query(&table, 1, 7, 0x1814, 0, 1,
                                 now + 1u, &snapshot));
    assert(r48_source_rep_note(&table, 1, 7, 0x1814, 0,
                               1, R48_SOURCE_REP_REFERENCE_MATCH, now + 2u));
    assert(r48_source_rep_query(&table, 1, 7, 0x1814, 0,
                                1, now + 2u, &snapshot));
    assert(snapshot.provider_known && snapshot.provid == 0);
    assert(!r48_source_rep_query(&table, 1, 7, 0x1814, 0,
                                 0, now + 2u, &snapshot));

    /* A delivered failure proved against a different accepted line reference
     * is negative evidence; unsupported/non-reference events cannot update it. */
    assert(r48_source_rep_note(&table, 1, 7, 0x1814, 0x123456u,
                               1, R48_SOURCE_REP_REFERENCE_PROOF, now + 3u));
    query_score(&table, 1, 7, 0x1814, 0x123456u, 1, now + 3u, 54);
    assert(r48_source_rep_rank_q8(&table, 1, 7, 0x1814, 0x123456u,
                                  1, now + 3u) == 0u);
    assert(r48_source_rep_query(&table, 1, 7, 0x1814, 0x123456u,
                                1, now + 3u, &snapshot));
    assert(snapshot.rank_delayed && snapshot.last_proof_ms == now + 3u);
    assert(!r48_source_rep_note(&table, 1, 7, 0x1814, 0x123456u,
                                1, R48_SOURCE_REP_NONE, now + 4u));
    assert(table.invalid_events == 1u);
    assert(r48_source_rep_note(&table, 1, 7, 0x1814, 0x123456u,
                               1, R48_SOURCE_REP_REFERENCE_MATCH, now + 5u));
    assert(r48_source_rep_rank_q8(&table, 1, 7, 0x1814, 0x123456u,
          1, now + 3u + R48_SOURCE_REP_RANK_DELAY_MS - 1u) == 0u);
    assert(r48_source_rep_rank_q8(&table, 1, 7, 0x1814, 0x123456u,
          1, now + 3u + R48_SOURCE_REP_RANK_DELAY_MS) > 0u);

    /* A clean sample is mandatory as well as elapsed time; it can arrive
     * after the 15-minute window without ever hard-blocking the source. */
    r48_source_rep_init(&table);
    assert(r48_source_rep_note(&table, 1, 8, 0x1814, 0x123456u,
                               1, R48_SOURCE_REP_REFERENCE_PROOF, 200u));
    assert(r48_source_rep_rank_q8(&table, 1, 8, 0x1814, 0x123456u,
          1, 200u + R48_SOURCE_REP_RANK_DELAY_MS) == 0u);
    assert(r48_source_rep_note(&table, 1, 8, 0x1814, 0x123456u,
                               1, R48_SOURCE_REP_REFERENCE_MATCH,
                               201u + R48_SOURCE_REP_RANK_DELAY_MS));
    assert(r48_source_rep_rank_q8(&table, 1, 8, 0x1814, 0x123456u,
          1, 201u + R48_SOURCE_REP_RANK_DELAY_MS) > 0u);

    /* A delayed clean event with an older tick cannot release a later proof. */
    r48_source_rep_init(&table);
    assert(r48_source_rep_note(&table, 1, 8, 0x1814, 0x123456u,
                               1, R48_SOURCE_REP_REFERENCE_PROOF, 500u));
    assert(r48_source_rep_note(&table, 1, 8, 0x1814, 0x123456u,
                               1, R48_SOURCE_REP_REFERENCE_PROOF, 700u));
    assert(r48_source_rep_note(&table, 1, 8, 0x1814, 0x123456u,
                               1, R48_SOURCE_REP_REFERENCE_MATCH, 600u));
    assert(r48_source_rep_rank_q8(&table, 1, 8, 0x1814, 0x123456u,
          1, 700u + R48_SOURCE_REP_RANK_DELAY_MS) == 0u);
    assert(r48_source_rep_note(&table, 1, 8, 0x1814, 0x123456u,
                               1, R48_SOURCE_REP_REFERENCE_MATCH,
                               701u + R48_SOURCE_REP_RANK_DELAY_MS));
    assert(r48_source_rep_rank_q8(&table, 1, 8, 0x1814, 0x123456u,
          1, 701u + R48_SOURCE_REP_RANK_DELAY_MS) > 0u);

    /* A same-tick renewed proof conservatively resets the clean exit count. */
    r48_source_rep_init(&table);
    assert(r48_source_rep_note(&table, 1, 8, 0x1814, 0x123456u,
                               1, R48_SOURCE_REP_REFERENCE_PROOF, 800u));
    assert(r48_source_rep_note(&table, 1, 8, 0x1814, 0x123456u,
                               1, R48_SOURCE_REP_REFERENCE_MATCH, 801u));
    assert(r48_source_rep_note(&table, 1, 8, 0x1814, 0x123456u,
                               1, R48_SOURCE_REP_REFERENCE_PROOF, 800u));
    assert(r48_source_rep_rank_q8(&table, 1, 8, 0x1814, 0x123456u,
          1, 800u + R48_SOURCE_REP_RANK_DELAY_MS) == 0u);
    assert(r48_source_rep_note(&table, 1, 8, 0x1814, 0x123456u,
                               1, R48_SOURCE_REP_REFERENCE_MATCH,
                               802u + R48_SOURCE_REP_RANK_DELAY_MS));
    assert(r48_source_rep_rank_q8(&table, 1, 8, 0x1814, 0x123456u,
          1, 802u + R48_SOURCE_REP_RANK_DELAY_MS) > 0u);

    /* The 15-minute rank-delay clock is also wrap-safe. */
    r48_source_rep_init(&table);
    now = UINT32_MAX - 2000u;
    assert(r48_source_rep_note(&table, 1, 8, 0x1814, 0x123456u,
                               1, R48_SOURCE_REP_REFERENCE_PROOF, now));
    assert(r48_source_rep_note(&table, 1, 8, 0x1814, 0x123456u,
                               1, R48_SOURCE_REP_REFERENCE_MATCH, now + 10u));
    assert(r48_source_rep_rank_q8(&table, 1, 8, 0x1814, 0x123456u,
          1, now + R48_SOURCE_REP_RANK_DELAY_MS - 1u) == 0u);
    assert(r48_source_rep_rank_q8(&table, 1, 8, 0x1814, 0x123456u,
          1, now + R48_SOURCE_REP_RANK_DELAY_MS) > 0u);

    /* The decay cadence is monotonic and does not fire one millisecond early. */
    r48_source_rep_init(&table);
    assert(r48_source_rep_note(&table, 1, 9, 0x0500, 1,
                               1, R48_SOURCE_REP_REFERENCE_MATCH, 1000u));
    query_score(&table, 1, 9, 0x0500, 1, 1, 60999u, 56);
    query_score(&table, 1, 9, 0x0500, 1, 1, 61000u, 55);

    /* 32-bit GetTickCount wrap keeps the one-minute decay interval intact. */
    r48_source_rep_init(&table);
    now = UINT32_MAX - 30000u;
    assert(r48_source_rep_note(&table, 1, 9, 0x0500, 1,
                               1, R48_SOURCE_REP_REFERENCE_MATCH, now));
    query_score(&table, 1, 9, 0x0500, 1, 1, now + 59999u, 56);
    query_score(&table, 1, 9, 0x0500, 1, 1, now + 60000u, 55);

    /* Long-idle evidence returns to neutral without looping once per uptime
     * minute; raw match/proof counts remain available for diagnostics. */
    r48_source_rep_init(&table);
    assert(r48_source_rep_note(&table, 1, 11, 0x0500, 1,
                               1, R48_SOURCE_REP_REFERENCE_PROOF, 0u));
    query_score(&table, 1, 11, 0x0500, 1, 1,
                65u * R48_SOURCE_REP_DECAY_MS, 50);
    assert(r48_source_rep_query(&table, 1, 11, 0x0500, 1,
                                1, 65u * R48_SOURCE_REP_DECAY_MS,
                                &snapshot));
    assert(snapshot.proofs == 1u && snapshot.matches == 0u);

    /* Out-of-order timestamps committed after a proof cannot clear its short
     * delay, even when a later clean reference was committed first. */
    r48_source_rep_init(&table);
    assert(r48_source_rep_note(&table, 1, 12, 0x1814, 0x123456u,
                               1, R48_SOURCE_REP_REFERENCE_PROOF, 1001u));
    assert(r48_source_rep_note(&table, 1, 12, 0x1814, 0x123456u,
                               1, R48_SOURCE_REP_REFERENCE_MATCH, 1002u));
    assert(r48_source_rep_rank_q8(&table, 1, 12, 0x1814, 0x123456u,
                                  1, 1000u) == 0u);

    /* Signed tick deltas stop working after INT32_MAX ms. A 30-day idle gap
     * is still a valid forward interval within one GetTickCount wrap and must
     * decay the score to neutral, rather than preserving stale evidence. */
    r48_source_rep_init(&table);
    assert(r48_source_rep_note(&table, 1, 13, 0x0500, 1,
                               1, R48_SOURCE_REP_REFERENCE_MATCH, 1000u));
    query_score(&table, 1, 13, 0x0500, 1, 1,
                1000u + 30u * 86400000u, 50);

    /* A stale timestamp from a concurrent producer does not make a newer row
     * look oldest during bounded-table eviction. */
    r48_source_rep_init(&table);
    for (i = 0; i < R48_SOURCE_REP_SLOTS; i++)
        assert(r48_source_rep_note(&table, 1, (int)i, 0x1814, i + 1u,
                                   1, R48_SOURCE_REP_REFERENCE_MATCH,
                                   1000u + i));
    assert(r48_source_rep_note(&table, 1, 999, 0x1814, 999u,
                               1, R48_SOURCE_REP_REFERENCE_MATCH, 1000u));
    assert(!r48_source_rep_query(&table, 1, 0, 0x1814, 1, 1,
                                 1000u, &snapshot));
    assert(r48_source_rep_query(&table, 1, 1, 0x1814, 2, 1,
                                1000u, &snapshot));

    /* Origin flags are part of the source key and evidence counts saturate. */
    r48_source_rep_init(&table);
    assert(r48_source_rep_note(&table, 2, (int)0x40000007u, 0x1814, 1,
                               1, R48_SOURCE_REP_REFERENCE_MATCH, 10u));
    entry = r48_source_rep_find(&table, 2, (int)0x40000007u,
                                0x1814, 1, 1);
    assert(entry && entry->matches == 1u);
    entry->matches = UINT32_MAX;
    assert(r48_source_rep_note(&table, 2, (int)0x40000007u, 0x1814, 1,
                               1, R48_SOURCE_REP_REFERENCE_MATCH, 11u));
    assert(entry->matches == UINT32_MAX);
    assert(!r48_source_rep_find(&table, 2, 7, 0x1814, 1, 1));

    /* Fixed-size LRU eviction is bounded and does not make an unknown source
     * look bad or preserve a row past its slot replacement. */
    r48_source_rep_init(&table);
    for (i = 0; i < R48_SOURCE_REP_SLOTS; i++)
        assert(r48_source_rep_note(&table, 1, (int)i, 0x1814, i + 1u,
                                   1, R48_SOURCE_REP_REFERENCE_MATCH,
                                   1000u + i));
    assert(r48_source_rep_note(&table, 1, 999, 0x1814, 999u,
                               1, R48_SOURCE_REP_REFERENCE_MATCH, 2000u));
    assert(table.evictions == 1u);
    assert(!r48_source_rep_query(&table, 1, 0, 0x1814, 1, 1,
                                 2000u, &snapshot));
    assert(r48_source_rep_query(&table, 1, 1, 0x1814, 2, 1,
                                2000u, &snapshot));

    puts("PASS: R48 source reputation is provider-scoped, reference-gated, decaying, bounded, and rank-delayed without exclusion");
    return 0;
}
