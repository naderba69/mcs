#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../src/udpfast.h"

static int passed, failed;

#define CHECK(cond, what) do { \
    if (cond) { passed++; printf("  PASS: %s\n", what); } \
    else { failed++; printf("  FAIL: %s\n", what); } \
} while (0)

static int contains_seq(const struct udpfast_table *t, uint32_t seq)
{
    int i;
    for (i = 0; i < UDPFAST_PENDING_SIZE; i++) {
        if (t->pending[i].used && t->pending[i].seq == seq) return 1;
    }
    return 0;
}

int main(void)
{
    struct udpfast_table table;
    uint8_t packet[1] = {0x5a};
    uint8_t *datas[2] = {0};
    uint32_t seqs[2] = {0}, ips[2] = {0};
    uint16_t ports[2] = {0};
    int lens[2] = {0};
    uint32_t seq, old_seq = 0, recent_seq = 0;
    int i, count;

    printf("== UDP fast-path modulo tick tests ==\n");
    udpfast_init(&table, 1);
    seq = udpfast_send(&table, packet, sizeof(packet), 0x01020304, 9000,
                       UINT32_MAX - 99u);
    count = udpfast_check_retx(&table, 99u, seqs, datas, lens, ips, ports, 2);
    CHECK(seq != 0 && count == 0,
          "retransmit deadline remains pending 1 ms before rollover expiry");
    count = udpfast_check_retx(&table, 100u, seqs, datas, lens, ips, ports, 2);
    CHECK(count == 1 && seqs[0] == seq && lens[0] == 1 && datas[0][0] == packet[0],
          "200 ms retransmit deadline is measured directly in modulo millisecond ticks");

    udpfast_record_retx(&table, seq, 100u);
    CHECK(udpfast_check_retx(&table, 499u, seqs, datas, lens, ips, ports, 2) == 0 &&
          udpfast_check_retx(&table, 500u, seqs, datas, lens, ips, ports, 2) == 1,
          "second retransmit interval doubles exactly across the wrap");
    udpfast_record_retx(&table, seq, 500u);
    CHECK(udpfast_check_retx(&table, 1099u, seqs, datas, lens, ips, ports, 2) == 0 &&
          udpfast_check_retx(&table, 1100u, seqs, datas, lens, ips, ports, 2) == 1,
          "third retransmit interval uses the next bounded backoff step");
    udpfast_record_retx(&table, seq, 1100u);
    CHECK(udpfast_check_retx(&table, 1899u, seqs, datas, lens, ips, ports, 2) == 0 &&
          udpfast_check_retx(&table, 1900u, seqs, datas, lens, ips, ports, 2) == 0 &&
          table.total_timeouts == 1 && !contains_seq(&table, seq),
          "timeout occurs after the final retry interval and is counted only once");
    udpfast_check_retx(&table, 3000u, seqs, datas, lens, ips, ports, 2);
    CHECK(table.total_timeouts == 1,
          "repeated polling does not inflate timeout statistics");

    udpfast_init(&table, 1);
    seq = udpfast_send(&table, packet, sizeof(packet), 1, 1, UINT32_MAX - 5u);
    udpfast_expire(&table, 4u, 10u);
    CHECK(contains_seq(&table, seq),
          "entry remains through the exact maximum age across rollover");
    udpfast_expire(&table, 5u, 10u);
    CHECK(!contains_seq(&table, seq),
          "entry expires one tick beyond the maximum age across rollover");

    udpfast_init(&table, 1);
    for (i = 0; i < UDPFAST_PENDING_SIZE; i++) {
        uint32_t age = (i == 0) ? 50000u : (uint32_t)(UDPFAST_PENDING_SIZE - i);
        uint32_t added = udpfast_send(&table, packet, sizeof(packet),
                                      (uint32_t)i + 1u, 9000,
                                      100u - age);
        if (i == 0) old_seq = added;
        if (i == 1) recent_seq = added;
    }
    seq = udpfast_send(&table, packet, sizeof(packet), 1000, 9000, 100u);
    CHECK(seq != 0 && !contains_seq(&table, old_seq) &&
          contains_seq(&table, recent_seq) && contains_seq(&table, seq),
          "full pending table evicts greatest unsigned elapsed age across rollover");

    printf("%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
