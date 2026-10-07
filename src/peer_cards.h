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
 * exactly 1024 that index is the one past the end. The comparisons below are
 * the same signed ones r82a used, so a card the old search accepted is still
 * accepted. What changed is that an index outside [0, n) is never evaluated.
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
 * cards is sorted ascending as unsigned (the TYPE_CARD_LIST writer does that
 * before it publishes nbcards). The search compares those values as signed
 * ints, which is what r82a did. Do not "fix" that here: a high CAID
 * (caid >= 0x8000) would start matching differently, and that is a separate
 * change.
 *
 * Returns 1 if the card is in the first min(nbcards, 1024) slots, else 0.
 * nbcards <= 0 is an empty list. A count above 1024 is clamped: the writer
 * never stores more, and a corrupt count must not walk off the array.
 */
static inline int peer_cards_find(const void *cards, int nbcards,
				  uint16_t caid, uint32_t provid)
{
	int n;
	int xl, xh, xm;
	int yl, yh, ym;
	int caprov;

	if (!cards || nbcards <= 0) return 0;
	n = nbcards;
	if (n > PEER_CARDS_MAX) n = PEER_CARDS_MAX;
	/* Same key expression as r82a. caid is uint16_t, so it promotes to int
	 * before the shift. That shift is undefined for caid >= 0x8000; leaving
	 * the expression alone keeps the key identical to the old search. */
	caprov = (caid << 16) | provid;
	xl = 0;
	xh = n - 1;
	yl = (int)peer_card_at(cards, xl);
	yh = (int)peer_card_at(cards, xh);
	while (yl <= caprov && yh >= caprov) {
		xm = (xl + xh) / 2;
		if (xm < 0 || xm >= n) return 0;
		ym = (int)peer_card_at(cards, xm);
		if (ym < caprov) {
			xl = xm + 1;
			if (xl >= n) return 0;
			yl = (int)peer_card_at(cards, xl);
		} else if (ym > caprov) {
			xh = xm - 1;
			if (xh < 0) return 0;
			yh = (int)peer_card_at(cards, xh);
		} else {
			return 1;
		}
	}
	if (xl >= 0 && xl < n && peer_card_at(cards, xl) == (uint32_t)caprov) return 1;
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
static inline int peer_cards_listed(const void *cards, int nbcards, int caprov)
{
	int i;
	int n;

	if (!cards) return 0;
	n = nbcards;
	if (n < 1 || n > PEER_CARDS_MAX) n = PEER_CARDS_MAX;
	for (i = 0; i < n; i++) {
		uint32_t slot = peer_card_at(cards, i);
		if (!slot) return 0;
		if (slot == (uint32_t)caprov) return 1;
	}
	return 0;
}

#endif /* MCS_PEER_CARDS_H */
