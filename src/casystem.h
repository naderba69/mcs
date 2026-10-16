/*
 * casystem.h -- TASK R26 (D77): the modern Conditional-Access registry.
 *
 * WHY THIS EXISTS. The tree knew modern CA systems only as scattered
 * hard-coded family numbers:
 *   - ecmdata.c extracted a provider id for three families only
 *     (Seca 01 / Viaccess 05 / CryptoWorks 0D) with UNBOUNDED reads --
 *     a short or malformed ECM was read past its end;
 *   - ecm_getchid() knew Irdeto (06) only;
 *   - the ECM identity hash skipped 13 bytes for family 17 only
 *     (the BetaCrypt tunnel header), open-coded at the call site;
 *   - acceptDCW() applied the DVB-CSA checksum test to EVERY key.
 *     That test is correct for classic CSA keys, whose byte 3/7/11/15
 *     really is the sum of the previous three bytes. It is wrong for
 *     the systems that now dominate new deployments -- AES-128 /
 *     CSA3 scrambling and BISS2, whose 16-byte control word is an AES
 *     key (or an AES session word) with NO checksum bytes: the test
 *     rejects such keys with probability ~65535/65536. On those
 *     bouquets the stock server drops every genuine key and shows a
 *     black screen while reporting nothing (the TASK 1.10b scene, one
 *     CA generation later).
 *
 * WHAT THIS IS. One pure table (header-only, gnu89, no allocation, no
 * globals, no threads -- the cafamily.h / chanhealth.h shape) that
 * answers, for any CAID:
 *   - which CA system is this?            ca_name() / ca_find()
 *   - does its control word carry the     ca_cw_checksum_expected()
 *     DVB-CSA checksum bytes?
 *   - is it an iCam-generation            ca_is_icam()
 *     VideoGuard CAID?
 *   - is it a constant-CW system          ca_is_biss() family
 *     (BISS1 2600 / BISS2 2610)?
 *   - where does the ECM identity         ca_ecm_hash_offset()
 *     hash start (tunnel header)?
 *   - which provider id / channel id      ca_ecm_provid() /
 *     does the ECM itself carry?          ca_ecm_chid()
 *     (bounds-checked: never reads past ecmlen, returns 0 = "not
 *     carried in the ECM / not extractable" on any degenerate input)
 *
 * DOCTRINE (D77, the R16/R17 rhythm). This module DECIDES NOTHING on
 * the hot path by itself. The CW-format column is advisory until the
 * operator arms it per profile with `CA MODERN: ON` (config.c); with
 * the flag off -- the default -- acceptDCW_ca() is bit-for-bit the
 * stock acceptDCW_for() decision for every CAID, known or not. An
 * unknown CAID is treated as classic CSA (checksum expected), which
 * is the stock behaviour: modern handling is something a bouquet
 * opts into, never something imposed on it.
 *
 * THE TABLE. Family rows key on the high byte (caid >> 8), as in
 * cafamily.h. Exact rows (checked first) exist only where a single
 * CAID inside a family behaves differently from its family:
 *   - the iCam-generation VideoGuard CAIDs (0963, 098C, 098D, 098E,
 *     09C4, 09CD): cards of this generation return control words
 *     without the classic CSA checksum bytes, so an armed profile
 *     must not apply the checksum test to them;
 *   - BISS1 (2600, constant 8-byte key repeated) vs BISS2 (2610,
 *     128-bit AES session word -- no checksum bytes, and the DCW is
 *     the full 16 bytes, never a repeated half);
 *   - DRE-Crypt (4AE0/4AE1) and Verimatrix (5601) exact spellings.
 * Everything else is a family row. Deliberately NOT in the table:
 * systems this tree has never seen on the wire and whose ECM layout
 * we cannot verify on the tree (the D1 golden rule: no folklore).
 */
#ifndef MCS_CASYSTEM_H
#define MCS_CASYSTEM_H

#include <stdint.h>
#include <string.h>

/* ---- control-word formats ------------------------------------------- */
#define CA_CW_CSA     0   /* classic DVB CSA: checksum bytes present   */
#define CA_CW_AES     1   /* AES-128 / CSA3 / BISS2: no checksum bytes */
#define CA_CW_CONST   2   /* constant CW (BISS1): no checksum bytes    */
#define CA_CW_UNKNOWN 3   /* unlisted CAID: treated as CSA (stock)     */

/* ---- flags ------------------------------------------------------------ */
#define CA_F_ICAM   0x01  /* iCam-generation VideoGuard CAID           */
#define CA_F_TUNNEL 0x02  /* ECM carries a tunnel header (BetaCrypt)   */
#define CA_F_CHID   0x04  /* ECM carries a channel id                  */

struct ca_info {
	uint16_t caid;      /* exact CAID, or the family base (xx00)      */
	uint8_t  exact;     /* 1: match the full CAID; 0: match the family */
	uint8_t  cwformat;  /* CA_CW_*                                     */
	uint8_t  flags;     /* CA_F_*                                      */
	const char *name;
};

static const struct ca_info ca_table[] = {
	/* exact rows first in lookup order (ca_find checks exact first) */
	{ 0x0963, 1, CA_CW_AES,   CA_F_ICAM,   "VideoGuard iCam (Sky UK)" },
	{ 0x098C, 1, CA_CW_AES,   CA_F_ICAM,   "VideoGuard (Sky DE, no-CRC CW)" },
	{ 0x098D, 1, CA_CW_AES,   CA_F_ICAM,   "VideoGuard iCam (Sky DE)" },
	{ 0x098E, 1, CA_CW_AES,   CA_F_ICAM,   "VideoGuard iCam" },
	{ 0x09C4, 1, CA_CW_AES,   CA_F_ICAM,   "VideoGuard (Sky DE, no-CRC CW)" },
	{ 0x09CD, 1, CA_CW_AES,   CA_F_ICAM,   "VideoGuard iCam (Sky IT)" },
	{ 0x2600, 1, CA_CW_CONST, 0,           "BISS1" },
	{ 0x2610, 1, CA_CW_AES,   0,           "BISS2" },
	{ 0x4AE0, 1, CA_CW_CSA,   0,           "DRE-Crypt" },
	{ 0x4AE1, 1, CA_CW_CSA,   0,           "DRE-Crypt" },
	{ 0x5601, 1, CA_CW_AES,   0,           "Verimatrix" },
	/* family rows: caid is the family base (high byte << 8) */
	{ 0x0100, 0, CA_CW_CSA,   0,           "Seca Mediaguard" },
	{ 0x0500, 0, CA_CW_CSA,   0,           "Viaccess" },
	{ 0x0600, 0, CA_CW_CSA,   CA_F_CHID,   "Irdeto" },
	{ 0x0900, 0, CA_CW_CSA,   0,           "VideoGuard" },
	{ 0x0B00, 0, CA_CW_CSA,   0,           "Conax" },
	{ 0x0D00, 0, CA_CW_CSA,   0,           "CryptoWorks" },
	{ 0x0E00, 0, CA_CW_CSA,   0,           "PowerVu" },
	{ 0x1700, 0, CA_CW_CSA,   CA_F_TUNNEL, "BetaCrypt" },
	{ 0x1800, 0, CA_CW_CSA,   0,           "Nagravision" },
	{ 0x2600, 0, CA_CW_CONST, 0,           "BISS" },
	{ 0x4A00, 0, CA_CW_CSA,   0,           "DRE-Crypt" },
	{ 0x5600, 0, CA_CW_AES,   0,           "Verimatrix" },
	{ 0x0000, 0, CA_CW_UNKNOWN, 0,         "Unknown" }
};

#define CA_TABLE_N ((int)(sizeof(ca_table) / sizeof(ca_table[0])))

/* The registry lookup: an exact row wins over the family row, so an
 * iCam CAID is never shadowed by its (checksum-carrying) family. A
 * CAID no row claims returns the Unknown row (never NULL). */
static const struct ca_info *ca_find(uint16_t caid)
{
	int i;
	for (i = 0; i < CA_TABLE_N; i++) {
		if (ca_table[i].exact && ca_table[i].caid == caid)
			return &ca_table[i];
	}
	for (i = 0; i < CA_TABLE_N; i++) {
		if (!ca_table[i].exact && ca_table[i].caid != 0 &&
		    ca_table[i].caid == (uint16_t)(caid & 0xFF00))
			return &ca_table[i];
	}
	return &ca_table[CA_TABLE_N - 1]; /* Unknown */
}

static const char *ca_name(uint16_t caid)
{
	return ca_find(caid)->name;
}

static int ca_cwformat(uint16_t caid)
{
	return ca_find(caid)->cwformat;
}

/* Does a genuine control word of this system carry the DVB-CSA
 * checksum bytes (byte 3 = sum of bytes 0..2, etc.)? Unknown CAIDs
 * answer 1: stock behaviour is the safe default, and only an armed
 * `CA MODERN: ON` profile ever consults this (acceptDCW_ca). */
static int ca_cw_checksum_expected(uint16_t caid)
{
	int f = ca_cwformat(caid);
	return (f == CA_CW_CSA || f == CA_CW_UNKNOWN) ? 1 : 0;
}

static int ca_is_icam(uint16_t caid)
{
	return (ca_find(caid)->flags & CA_F_ICAM) ? 1 : 0;
}

/* BISS family: 2600 = BISS1 (constant key), 2610 = BISS2 (AES). */
static int ca_is_biss(uint16_t caid)
{
	return (caid >> 8) == 0x26;
}

static int ca_is_biss2(uint16_t caid)
{
	return caid == 0x2610;
}

/* Where the ECM identity hash starts. Stock r82a hashed from byte 3,
 * except family 17 (BetaCrypt), whose ECM wraps a Nagravision ECM in
 * a 13-byte tunnel header: hashing the wrapper would give every
 * tunnelled ECM of a bouquet a near-identical identity. The number
 * moves here so it is a table fact, not an open-coded call-site one. */
static int ca_ecm_hash_offset(uint16_t caid)
{
	return ((caid >> 8) == 0x17) ? 13 : 3;
}

/*
 * Bounds-checked provider-id extraction -- the three families whose
 * ECM carries the provider id in the ECM itself, byte-for-byte the
 * r82a rules in ecm_getprovid(), with every read guarded by ecmlen.
 * For every other family (and on any degenerate input) returns 0 and
 * leaves *out untouched: the provider of a modern CA (VideoGuard,
 * Nagravision, Irdeto, Conax, BISS...) is a property of the card /
 * profile, not of the ECM -- 0 there is the correct stock answer,
 * now also the SAFE one (a 4-byte "ECM" can no longer be read at
 * ecm[10]).
 */
static int ca_ecm_provid(const uint8_t *ecm, int ecmlen, uint16_t caid,
			 uint32_t *out)
{
	if (!ecm || !out || ecmlen < 5) return 0;

	switch (caid >> 8) {
	case 0x01: /* Seca */
		*out = (uint32_t)((ecm[3] << 8) | ecm[4]);
		return 1;

	case 0x05: { /* Viaccess: the two r82a nano positions, guarded */
		int i, done = 0;
		uint32_t provid = 0;

		i = (ecm[4] == 0xD2) ? ((ecmlen > 5) ? ecm[5] + 2 : -1) : 0;
		if (i >= 0 && i + 8 < ecmlen &&
		    ecm[5 + i] == 3 && (ecm[4 + i] == 0x90 || ecm[4 + i] == 0x40)) {
			provid = ((uint32_t)ecm[i + 6] << 16) |
				 ((uint32_t)ecm[i + 7] << 8) |
				 (uint32_t)(ecm[i + 8] & 0xF0);
			done = 1;
		}
		if (ecmlen > 6) {
			i = (ecm[6] == 0xD2) ? ((ecmlen > 7) ? ecm[7] + 2 : -1) : 0;
			if (i >= 0 && i + 10 < ecmlen &&
			    ecm[7 + i] == 7 && (ecm[6 + i] == 0x90 || ecm[6 + i] == 0x40)) {
				provid = ((uint32_t)ecm[i + 8] << 16) |
					 ((uint32_t)ecm[i + 9] << 8) |
					 (uint32_t)(ecm[i + 10] & 0xF0);
				done = 1;
			}
		}
		if (done) { *out = provid; return 1; }
		return 0;
	}

	case 0x0D: { /* CryptoWorks: descriptor walk for tag 0x83 */
		int len, i, descriptor_length = 0;
		if (ecmlen < 3) return 0;
		len = (((ecm[1] & 0x0F) << 8) | ecm[2]) + 3;
		if (len > ecmlen) len = ecmlen;
		for (i = 8; i + 2 < len; i += descriptor_length + 2) {
			descriptor_length = ecm[i + 1];
			if (ecm[i] == 0x83) {
				*out = (uint32_t)ecm[i + 2] & 0xFE;
				return 1;
			}
		}
		return 0;
	}

	default:
		return 0; /* provider is the card's, not the ECM's */
	}
}

/* Bounds-checked channel-id extraction (Irdeto, the r82a rule). */
static uint16_t ca_ecm_chid(const uint8_t *ecm, int ecmlen, uint16_t caid)
{
	if (!ecm) return 0;
	if ((caid >> 8) == 0x06 && ecmlen >= 8)
		return (uint16_t)((ecm[6] << 8) | ecm[7]);
	return 0;
}

/* Human-readable CW format, for config logs and the guide. */
static const char *ca_cwformat_name(int format)
{
	switch (format) {
	case CA_CW_CSA:   return "CSA (checksum CW)";
	case CA_CW_AES:   return "AES/no-checksum CW";
	case CA_CW_CONST: return "constant CW (BISS)";
	default:          return "unknown (treated as CSA)";
	}
}

#endif /* MCS_CASYSTEM_H */
