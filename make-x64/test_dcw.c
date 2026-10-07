/*
 * test_dcw.c -- self-test for the MultiCS control-word acceptance filter.
 *
 * It compiles the REAL upstream ../src/dcw.c (no re-implementation) and runs
 * it against vectors that correspond to real-world bouquet behaviour.
 *
 * A CW rejected here is never forwarded to the client -> the receiver has no
 * key -> BLACK SCREEN. So every "legitimate CW rejected" row is a
 * black-screen bug.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

int checksumDCW(uint8_t *data);
int isnullDCW(uint8_t *data);
int isbadDCW(uint8_t *data);
int acceptDCW(uint8_t *data);
int similarcw(uint8_t *cw1, uint8_t *cw2);
int ishalfnulledcw(uint8_t dcw[16]);
void dcw_badlist_publish(const uint8_t *keys, int n);
int acceptDCW_for(uint8_t *data, int checksum_mode, int repeat_mode);
#define DCWFILTER_INHERIT 0
#define DCWFILTER_ON      1
#define DCWFILTER_OFF     2

static int failures = 0, total = 0;

static void check(const char *name, int got, int want)
{
	total++;
	if (got != want) {
		failures++;
		printf("  [FAIL] %-46s got=%d want=%d\n", name, got, want);
	} else {
		printf("  [ ok ] %-46s = %d\n", name, got);
	}
}

/* Build a CW whose 4-byte groups each satisfy the DVB-CSA checksum:
 * b3 = (b0+b1+b2) & 0xFF.  This is what checksumDCW() verifies. */
static void make_valid(uint8_t *cw, const uint8_t g[12])
{
	int i;
	for (i = 0; i < 4; i++) {
		cw[i*4+0] = g[i*3+0];
		cw[i*4+1] = g[i*3+1];
		cw[i*4+2] = g[i*3+2];
		cw[i*4+3] = (uint8_t)((g[i*3+0] + g[i*3+1] + g[i*3+2]) & 0xFF);
	}
}

int main(void)
{
	uint8_t cw[16], fake[16];

	printf("== MultiCS dcw.c filter self-test ==\n\n-- basic sanity --\n");

	/* 1. a well-formed CW must pass */
	make_valid(cw, (const uint8_t[]){
		0x11,0x22,0x33,  0x44,0x55,0x66,
		0x77,0x88,0x99,  0xAA,0xBB,0xCC });
	check("valid CSA-checksum CW is accepted", acceptDCW(cw), 1);

	/* 2. all-zero CW must be rejected (no key) */
	memset(cw, 0, 16);
	check("all-zero CW is rejected", acceptDCW(cw), 0);

	/* 3. broken checksum must be rejected */
	make_valid(cw, (const uint8_t[]){
		0x11,0x22,0x33,  0x44,0x55,0x66,
		0x77,0x88,0x99,  0xAA,0xBB,0xCC });
	cw[15] ^= 0x01;
	check("CW with corrupted checksum is rejected", acceptDCW(cw), 0);

	printf("\n-- the fake-CW attack described in the r107 changelog --\n");

	/* The public changelog warns: "a server ... create fakeCW by changing
	 * the last byte of CW (using XOR 0xF0) and send to all peers."
	 * Reproduce it: take a valid CW and XOR its last byte with 0xF0. */
	make_valid(cw, (const uint8_t[]){
		0x11,0x22,0x33,  0x44,0x55,0x66,
		0x77,0x88,0x99,  0xAA,0xBB,0xCC });
	memcpy(fake, cw, 16);
	fake[15] ^= 0xF0;
	check("XOR-0xF0 fake CW is rejected", acceptDCW(fake), 0);

	printf("\n-- legitimate CW shapes that the current filter may kill --\n");

	/* 4. Three identical non-zero bytes inside a 4-byte group.
	 * isbadDCW() rejects these outright. Some providers legitimately emit
	 * such keys, and when they do the bouquet goes black. */
	make_valid(cw, (const uint8_t[]){
		0x11,0x11,0x11,  0x44,0x55,0x66,
		0x77,0x88,0x99,  0xAA,0xBB,0xCC });
	printf("  info : CW with 11 11 11 group -> isbadDCW=%d acceptDCW=%d\n",
	       isbadDCW(cw), acceptDCW(cw));
	check("CW containing three equal bytes is REJECTED (documents current bug)",
	      acceptDCW(cw), 0);

	/* 5. NDS/Videoguard half-nulled CW: one 8-byte half is all zero.
	 * This is the NORMAL shape for caid 09xx. For a zeroed half
	 * 0+0+0 == 0 == b3, so checksumDCW still holds -- verify. */
	memset(cw, 0, 16);
	cw[0] = 0x11; cw[1] = 0x22; cw[2] = 0x33; cw[3] = 0x66;
	cw[4] = 0x44; cw[5] = 0x55; cw[6] = 0x66; cw[7] = 0xFF;
	printf("  info : NDS half-nulled CW -> checksum=%d ishalfnulledcw=%d acceptDCW=%d\n",
	       checksumDCW(cw), ishalfnulledcw(cw), acceptDCW(cw));
	check("NDS half-nulled CW survives acceptDCW", acceptDCW(cw), 1);

	/* 6. ishalfnulledcw() is declared in dcw.h and ecmdata.h but never
	 * called by any upstream code. Report linkage so the dead code is
	 * visible rather than silently assumed. */
	memset(cw, 0, 16);
	check("ishalfnulledcw detects a zeroed half", ishalfnulledcw(cw), 1);

	printf("\n-- similarcw() cycle detector --\n");
	{
		uint8_t a[8] = {1,2,3,4,5,6,7,8};
		uint8_t b[8] = {1,2,3,4,9,9,9,9};   /* 4 of 8 equal -> "similar" */
		check("similarcw flags 4/8 matching bytes", similarcw(a, b), 1);
	}

	printf("\n-- TASK 3.14: the BAD-DCW list --\n");
	{
		/* the key the public docs name, and a different valid key */
		static const uint8_t listed[16] = {
			0xFD,0xFF,0xFF,0xFB, 0xFD,0xFF,0xFF,0xFB,
			0xFD,0xFF,0xFF,0xFB, 0xFD,0xFF,0xFF,0xFB
		};
		uint8_t other[16];
		uint8_t many[33 * 16];
		int i;

		check("documented BAD-DCW key passes checksum", checksumDCW((uint8_t *)listed), 1);
		check("documented key accepted while the list is empty", acceptDCW((uint8_t *)listed), 1);
		dcw_badlist_publish(listed, 1);
		check("documented key rejected once listed", acceptDCW((uint8_t *)listed), 0);
		make_valid(other, (const uint8_t[]){
			0x11,0x22,0x33,  0x44,0x55,0x66,
			0x77,0x88,0x99,  0xAA,0xBB,0xCC });
		check("a key that is not listed still passes", acceptDCW(other), 1);
		dcw_badlist_publish(NULL, 0);
		check("clearing the list accepts the key again", acceptDCW((uint8_t *)listed), 1);

		/* the 33rd key is past the cap and must not be enforced */
		for (i = 0; i < 32; i++) memcpy(many + i * 16, listed, 16);
		memcpy(many + 32 * 16, other, 16);
		dcw_badlist_publish(many, 33);
		check("a key inside the cap is still rejected", acceptDCW((uint8_t *)listed), 0);
		check("the 33rd key is not enforced", acceptDCW(other), 1);
		dcw_badlist_publish(NULL, 0);
	}

	printf("\n-- TASK 3.15: a profile may override the two gates --\n");
	{
		uint8_t badsum[16];
		uint8_t rep[16];
		extern int dcw_filter_checksum;
		extern int dcw_filter_repeat;

		make_valid(badsum, (const uint8_t[]){
			0x11,0x22,0x33,  0x44,0x55,0x66,
			0x77,0x88,0x99,  0xAA,0xBB,0xCC });
		badsum[3] ^= 0x01;
		check("a broken checksum is rejected while the gate is on", acceptDCW(badsum), 0);
		check("checksum off accepts that key", acceptDCW_for(badsum, DCWFILTER_OFF, DCWFILTER_INHERIT), 1);
		check("inherit still rejects it", acceptDCW_for(badsum, DCWFILTER_INHERIT, DCWFILTER_INHERIT), 0);

		make_valid(rep, (const uint8_t[]){
			0x11,0x11,0x11,  0x44,0x55,0x66,
			0x77,0x88,0x99,  0xAA,0xBB,0xCC });
		check("three equal bytes are rejected while the gate is on", acceptDCW(rep), 0);
		check("repeat off accepts that key", acceptDCW_for(rep, DCWFILTER_INHERIT, DCWFILTER_OFF), 1);

		dcw_filter_checksum = 0;
		check("checksum forced on still rejects", acceptDCW_for(badsum, DCWFILTER_ON, DCWFILTER_OFF), 0);
		dcw_filter_checksum = 1;
		dcw_filter_repeat = 1;

		/* a null key is not rescued by turning either gate off */
		{
			uint8_t z[16];
			memset(z, 0, 16);
			check("a null key stays rejected with both gates off", acceptDCW_for(z, DCWFILTER_OFF, DCWFILTER_OFF), 0);
		}
	}

	printf("\n== %d/%d passed, %d failed ==\n", total - failures, total, failures);
	return failures ? 1 : 0;
}
