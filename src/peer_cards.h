/*
 * peer_cards.h — TASK 3.17
 *
 * Membership test for a cache peer's card list, extracted so the 1024-card
 * probe can call the same function the server calls.
 *
 * cards[] is 1024 slots (config.h, struct cachepeer_data). The next field in
 * that packed struct is nbcards, so cards[1024] is not a card: it is the
 * count. The r82a binary search assigned
 *
 *     yl = cards[xl = xm + 1]
 *     yh = cards[xh = xm - 1]
 *
 * with no check that the new index was still inside the array. On a list of
 * exactly 1024 that index is the one past the end. The search below stays
 * in bounds and uses the same unsigned ordering as the received card list.
 *
 * A header rather than a function in clustredcache.c because that file is not
 * a translation unit — main.c #includes it — and a header can be tested alone.
 */
#ifndef MCS_PEER_CARDS_H
#define MCS_PEER_CARDS_H

#include <stdint.h>
#include <string.h>

/*
 * cards[] sits in a packed struct, so its address is not a safe uint32_t *.
 * Copy the slot. Native endian, alignment-safe, and it does not warn.
 */
static inline uint32_t peer_card_at(const void *base, int i)
{
	uint32_t v;
	memcpy(&v, (const unsigned char *)base + (size_t)i * 4u, 4);
	return v;
}

/* Must match `uint32_t cards[1024]` in config.h. */
#define PEER_CARDS_MAX 1024

/*
 * Is (caid, provid) in this peer's list?
 *
 * cards is sorted ascending as unsigned by the TYPE_CARD_LIST writer. The
 * search uses the same ordering, including CAIDs at or above 0x8000. The
 * 32-bit packed key preserves the existing wire encoding; it cannot represent
 * CAID and a full 24-bit provider independently when their bits overlap.
 *
 * Returns 1 if the card is in the first min(nbcards, 1024) slots, else 0.
 * nbcards <= 0 is an empty list. A count above 1024 is clamped: the writer
 * never stores more, and a corrupt count must not walk off the array.
 */
static inline int peer_cards_find(const void *cards, int nbcards,
				  uint16_t caid, uint32_t provid)
{
	int n, lo, hi;
	uint32_t key;

	if (!cards || nbcards <= 0) return 0;
	n = nbcards > PEER_CARDS_MAX ? PEER_CARDS_MAX : nbcards;
	key = ((uint32_t)caid << 16) | provid;
	lo = 0;
	hi = n - 1;
	while (lo <= hi) {
		int mid = lo + (hi - lo) / 2;
		uint32_t value = peer_card_at(cards, mid);
		if (value < key) lo = mid + 1;
		else if (value > key) hi = mid - 1;
		else return 1;
	}
	return 0;
}

/*
 * Linear membership, used by peer_acceptcard (no callers in this tree).
 *
 * The old loop walked i < 1024 and treated a missing zero as "keep going".
 * A full list has no zero sentinel, so a miss fell off the end and the
 * caller accepted the card. Stop at nbcards. A miss is 0. Index n is never
 * read. An unusable count (0 or above 1024) keeps the old 1024-slot walk,
 * still inside the array.
 *
 * Returns 1 if found, 0 if not.
 */
static inline int peer_cards_listed(const void *cards, int nbcards, uint32_t caprov)
{
	int i;
	int n;

	if (!cards) return 0;
	n = nbcards;
	if (n < 1 || n > PEER_CARDS_MAX) n = PEER_CARDS_MAX;
	for (i = 0; i < n; i++) {
		uint32_t slot = peer_card_at(cards, i);
		if (!slot) return 0;
		if (slot == caprov) return 1;
	}
	return 0;
}

#endif /* MCS_PEER_CARDS_H */
