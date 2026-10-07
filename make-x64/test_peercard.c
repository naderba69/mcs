/*
 * test_peercard.c — TASK 3.17
 *
 * Calls the REAL ../src/peer_cards.h, the same function
 * peer_card_binarysearch() calls. A 1024-card list is searched for the
 * first slot, the last slot, and a card that is not in the list. Built
 * with ASan against a buffer of exactly 1024 slots, a read of cards[1024]
 * is a heap-buffer-overflow and the process dies. That is the acceptance
 * probe: the read stays inside the array.
 *
 *   make -C make-x64 test
 *   cc -O1 -fsanitize=address -fno-omit-frame-pointer -std=gnu89 \
 *      -o /tmp/test_peercard_asan test_peercard.c && /tmp/test_peercard_asan
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../src/peer_cards.h"

static int passed = 0, failed = 0;

static void check(const char *what, int got, int want)
{
	if (got == want) { passed++; printf("  [ ok ] %-56s = %d\n", what, got); }
	else { failed++; printf("  [FAIL] %-56s got=%d want=%d\n", what, got, want); }
}

/*
 * The r82a search, copied from the line this task replaced. The buffer the
 * test hands it is one slot longer than n, so the tail read — if it happens
 * — lands on a poison value instead of unmapped memory. Used only to show
 * the new function still accepts the same in-range cards.
 */
static int r82a_find(const uint32_t *cards, int nbcards, uint16_t caid, uint32_t provid)
{
	int caprov, xl, xh, yl, yh, xm, ym;

	if (nbcards == 0) return 0;
	caprov = (caid << 16) | provid;
	xl = 0;
	xh = nbcards - 1;
	yl = (int)cards[xl];
	yh = (int)cards[xh];
	while (yl <= caprov && yh >= caprov) {
		xm = (xl + xh) / 2;
		ym = (int)cards[xm];
		if (ym < caprov) yl = (int)cards[xl = xm + 1];
		else if (ym > caprov) yh = (int)cards[xh = xm - 1];
		else return 1;
	}
	if (cards[xl] == (uint32_t)caprov) return 1;
	return 0;
}

static uint32_t *alloc_cards(int n)
{
	uint32_t *p = (uint32_t *)malloc((size_t)n * sizeof(uint32_t));
	if (!p) { fprintf(stderr, "malloc failed\n"); exit(2); }
	memset(p, 0, (size_t)n * sizeof(uint32_t));
	return p;
}

int main(void)
{
	uint32_t *cards;
	int i, disagree;
	uint16_t caid;
	uint32_t provid;

	printf("== peer card list, 1024-slot bound ==\n");

	check("empty list is a miss", peer_cards_find(NULL, 0, 0x1884, 0), 0);
	check("negative count is a miss", peer_cards_find(NULL, -1, 0x1884, 0), 0);

	cards = alloc_cards(1);
	cards[0] = 0x18840000u;
	check("one card, hit", peer_cards_find(cards, 1, 0x1884, 0), 1);
	check("one card, other caid", peer_cards_find(cards, 1, 0x1801, 0), 0);
	check("one card, other provid", peer_cards_find(cards, 1, 0x1884, 1), 0);
	free(cards);

	/* Exactly 1024 slots. ASan dies if the search reads index 1024. */
	cards = alloc_cards(PEER_CARDS_MAX);
	for (i = 0; i < PEER_CARDS_MAX; i++)
		cards[i] = 0x18840000u + (uint32_t)i;
	check("1024 cards, first slot", peer_cards_find(cards, 1024, 0x1884, 0), 1);
	check("1024 cards, last slot", peer_cards_find(cards, 1024, 0x1884, 1023), 1);
	check("1024 cards, middle", peer_cards_find(cards, 1024, 0x1884, 512), 1);
	check("1024 cards, below the list", peer_cards_find(cards, 1024, 0x1801, 0), 0);
	check("1024 cards, above the list", peer_cards_find(cards, 1024, 0x1884, 1024), 0);
	check("1024 cards, other caid", peer_cards_find(cards, 1024, 0x1885, 0), 0);

	disagree = 0;
	for (i = 0; i < PEER_CARDS_MAX; i++) {
		caid = 0x1884;
		provid = (uint32_t)i;
		if (peer_cards_find(cards, 1024, caid, provid) != 1) disagree++;
	}
	check("every one of the 1024 cards is found", disagree, 0);

	/*
	 * A count above 1024 must be clamped. The buffer is exactly 1024
	 * slots, so reading cards[1024] here is the bug the task names.
	 * The 1025th logical card is not stored; it must be a miss.
	 */
	check("count 1025 does not read past the array",
	      peer_cards_find(cards, 1025, 0x1884, 0), 1);
	check("the unstored 1025th card is a miss",
	      peer_cards_find(cards, 1025, 0x1884, 1024), 0);

	/* Same cards, same answers as r82a, on a buffer with a poison slot
	 * so the old tail — if it fires — cannot leave this allocation. */
	{
		uint32_t *wide = alloc_cards(PEER_CARDS_MAX + 1);
		memcpy(wide, cards, PEER_CARDS_MAX * sizeof(uint32_t));
		wide[PEER_CARDS_MAX] = 0xDEADBEEFu;
		disagree = 0;
		for (i = 0; i < PEER_CARDS_MAX; i++) {
			if (peer_cards_find(wide, 1024, 0x1884, (uint32_t)i)
			    != r82a_find(wide, 1024, 0x1884, (uint32_t)i))
				disagree++;
		}
		if (peer_cards_find(wide, 1024, 0x1801, 0) != r82a_find(wide, 1024, 0x1801, 0))
			disagree++;
		if (peer_cards_find(wide, 1024, 0x1884, 1024) != 0)
			disagree++;
		check("1024-list agrees with r82a, poison slot not a hit", disagree, 0);
		free(wide);
	}
	free(cards);

	/* High CAIDs: agreement only. The signed comparison is not changed. */
	{
		uint32_t hi[8];
		uint16_t samples[4];
		int s, k;
		hi[0] = 0x00010000u;
		hi[1] = 0x18010000u;
		hi[2] = 0x7FFF0001u;
		hi[3] = 0x80000000u;
		hi[4] = 0x80010002u;
		hi[5] = 0xFFFF0000u;
		hi[6] = 0xFFFF0003u;
		hi[7] = 0xFFFFFFFFu;
		samples[0] = 0x0001;
		samples[1] = 0x1801;
		samples[2] = 0x8000;
		samples[3] = 0xFFFF;
		disagree = 0;
		for (s = 0; s < 4; s++) {
			for (k = 0; k < 4; k++) {
				if (peer_cards_find(hi, 8, samples[s], (uint32_t)k)
				    != r82a_find(hi, 8, samples[s], (uint32_t)k))
					disagree++;
			}
		}
		check("high-CAID answers still match r82a", disagree, 0);
	}

	/* The named linear scan: a full list with no zero, miss is 0,
	 * and index 1024 is not read. */
	cards = alloc_cards(PEER_CARDS_MAX);
	for (i = 0; i < PEER_CARDS_MAX; i++)
		cards[i] = 0x18840000u + (uint32_t)i;
	check("linear full list, last slot", peer_cards_listed(cards, 1024, (int)0x188403FFu), 1);
	check("linear full list, miss is not accept", peer_cards_listed(cards, 1024, (int)0x18010000u), 0);
	check("linear count 1025 stays in the array", peer_cards_listed(cards, 1025, (int)0x18840000u), 1);
	cards[3] = 0;
	check("linear zero sentinel still stops the walk", peer_cards_listed(cards, 1024, (int)0x1884000Au), 0);
	free(cards);

	printf("== %d/%d passed, %d failed ==\n", passed, passed + failed, failed);
	return failed ? 1 : 0;
}
