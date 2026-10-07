/*
 * test_parser.c — TASK 3.7: the str[255] off-by-one in the config parsers.
 *
 * Exercises the REAL ../src/parser.c — the same translation unit the server
 * links (parser.o), included directly. All six parse_* token readers shared
 * the identical writer:
 *
 *     if (len>=255) len=255;   // "check for length"
 *     memcpy(str, iparser, len);
 *     ...
 *     str[len] = 0;            // <- len can be exactly 255
 *
 * so a token of 255+ chars made every one of them write the terminator one
 * byte PAST a char[255] buffer. The config layer has real char[255] buffers
 * (read_config's scratch at config.c:845, parse_server_data's at :487,
 * twin_read_chninfo's at :4477) and parse_boolean() owns one itself
 * (parser.c) — each got the stray zero byte on the stack.
 *
 * THE PROOF HERE IS A CANARY. With -fpack-struct the byte after buf[255] in
 * the test struct sits at offset 255 — exactly where the old code wrote. A
 * token of exactly 255 chars must parse WITHOUT touching it; can1 staying
 * put is the whole task. A 254-char token must parse complete (the clamp
 * never moved for it); canary2 proves only one byte was ever at stake.
 *
 * The fix (TASK 3.7): the clamp writes at most 254 bytes so the terminator
 * lands at str[254] — inside every caller's char[255], still NUL-terminated.
 * Tokens of 255+ chars truncate to 254; nothing else changes.
 *
 *   make -C make-x64 test
 */

#include <stdio.h>
#include <string.h>
#include "../src/parser.c"

static int passed = 0, failed = 0;

static void check(const char *what, long got, long want)
{
	if (got == want) { passed++; printf("  [ ok ] %-58s = %ld\n", what, got); }
	else { failed++; printf("  [FAIL] %-58s got=%ld want=%ld\n", what, got, want); }
}

/* packed: can1 IS offset 255, the exact byte the old writer reached. */
struct __attribute__((packed)) slot {
	char buf[255];
	unsigned char can1;
	unsigned char can2;
};

static struct slot s;

static void reset(void)
{
	memset(&s, 0, sizeof(s));
	memset(s.buf, 0x55, sizeof(s.buf));
	s.can1 = 0xAA;
	s.can2 = 0xBB;
}

static char tok[512];   /* the token iparser points at (mutable cursor source) */

static void fill_tok(int n, char c)
{
	memset(tok, c, n);
	tok[n] = ' ';   /* token terminator: space */
	tok[n+1] = 0;
}

int main(void)
{
	printf("test_parser: the str[255] terminator stays inside (TASK 3.7)\n");

	/* ---- parse_str ---- */
	reset(); fill_tok(254, 'A');
	iparser = tok;
	check("parse_str 254-char token: length", parse_str(s.buf), 254);
	check("parse_str 254-char token: content kept", strncmp(s.buf, tok, 254) == 0, 1);
	check("parse_str 254-char token: can1 intact", s.can1, 0xAA);
	check("parse_str 254-char token: can2 intact", s.can2, 0xBB);

	reset(); fill_tok(255, 'B');
	iparser = tok;
	check("parse_str 255-char token: clamped to 254", parse_str(s.buf), 254);
	check("parse_str 255-char token: content kept", strncmp(s.buf, tok, 254) == 0, 1);
	check("parse_str 255-char token: NUL inside the buffer", s.buf[254], 0);
	check("parse_str 255-char token: can1 (offset 255) intact", s.can1, 0xAA);
	check("parse_str 255-char token: can2 intact", s.can2, 0xBB);

	reset(); fill_tok(400, 'C');
	iparser = tok;
	check("parse_str 400-char token: clamped to 254", parse_str(s.buf), 254);
	check("parse_str 400-char token: can1 intact", s.can1, 0xAA);

	reset(); fill_tok(0, 'A');
	iparser = tok;
	check("parse_str empty token: length 0", parse_str(s.buf), 0);
	check("parse_str empty token: terminated", s.buf[0], 0);
	check("parse_str empty token: can1 intact", s.can1, 0xAA);

	/* ---- parse_name ---- */
	reset(); fill_tok(255, 'D');
	iparser = tok;
	check("parse_name 255-char token: clamped to 254", parse_name(s.buf), 254);
	check("parse_name 255-char token: can1 intact", s.can1, 0xAA);
	check("parse_name 255-char token: can2 intact", s.can2, 0xBB);

	reset(); fill_tok(254, 'E');
	iparser = tok;
	check("parse_name 254-char token: complete", parse_name(s.buf), 254);
	check("parse_name 254-char token: can1 intact", s.can1, 0xAA);

	/* ---- parse_value ---- */
	reset(); fill_tok(255, 'F');
	iparser = tok;
	check("parse_value 255-char token: clamped to 254", parse_value(s.buf, " \t\r\n"), 254);
	check("parse_value 255-char token: can1 intact", s.can1, 0xAA);
	check("parse_value 255-char token: can2 intact", s.can2, 0xBB);

	/* ---- parse_int ---- */
	reset(); fill_tok(255, '7');
	iparser = tok;
	check("parse_int 255-digit token: clamped to 254", parse_int(s.buf), 254);
	check("parse_int 255-digit token: can1 intact", s.can1, 0xAA);
	check("parse_int 255-digit token: can2 intact", s.can2, 0xBB);

	reset(); fill_tok(254, '3');
	iparser = tok;
	check("parse_int 254-digit token: complete", parse_int(s.buf), 254);
	check("parse_int 254-digit token: can1 intact", s.can1, 0xAA);

	/* ---- parse_hex ---- */
	reset(); fill_tok(255, 'A');   /* hex digits */
	iparser = tok;
	check("parse_hex 255-digit token: clamped to 254", parse_hex(s.buf), 254);
	check("parse_hex 255-digit token: can1 intact", s.can1, 0xAA);
	check("parse_hex 255-digit token: can2 intact", s.can2, 0xBB);

	/* ---- parse_bin ---- */
	reset(); fill_tok(255, '1');
	iparser = tok;
	check("parse_bin 255-digit token: clamped to 254", parse_bin(s.buf), 254);
	check("parse_bin 255-digit token: can1 intact", s.can1, 0xAA);
	check("parse_bin 255-digit token: can2 intact", s.can2, 0xBB);

	/* ---- parse_boolean: it owns a char[255] itself (parser.c) ---- */
	{
		static char yes[300];
		memset(yes, ' ', 40);
		strcpy(yes + 40, "YES");
		iparser = yes;
		check("parse_boolean YES: 1", parse_boolean(), 1);
		memset(yes, ' ', 40);
		strcpy(yes + 40, "OFF");
		iparser = yes;
		check("parse_boolean OFF: 0", parse_boolean(), 0);
		memset(yes, ' ', 40);
		memset(yes + 40, 'X', 252);
		strcpy(yes + 292, "ON ");   /* a 252-char garbage token: not a boolean */
		iparser = yes;
		check("parse_boolean 255-char garbage token: 0, no crash", parse_boolean(), 0);
	}

	/* ---- parse_quotes: bounded by the CALLER's buffer (TASK 3.8b) ---- */
	/* The writer was strcpy(), so the cap is on the copy length, not the
	 * token scan: a 400-char quoted value into a char[255] must truncate
	 * to 254 and never touch can1 (offset 255). The cursor must stop after
	 * the closing quote exactly as before, so the rest of the line keeps
	 * parsing. */
	{
		static char q[1400];
		reset();
		q[0] = '"'; memset(q + 1, 'G', 400); q[401] = '"'; q[402] = ' '; q[403] = 0;
		iparser = q;
		check("parse_quotes 400-char value into 255: accepted", parse_quotes('"', s.buf, sizeof(s.buf)), 1);
		check("parse_quotes 400-char value into 255: capped at 254", strlen(s.buf), 254);
		check("parse_quotes 400-char value into 255: NUL inside", s.buf[254], 0);
		check("parse_quotes 400-char value into 255: can1 intact", s.can1, 0xAA);
		check("parse_quotes cursor stops after closing quote", *iparser, ' ');

		reset();
		q[0] = '"'; memset(q + 1, 'H', 250); q[251] = '"'; q[252] = 0;
		iparser = q;
		check("parse_quotes 250-char value into 255: complete", parse_quotes('"', s.buf, sizeof(s.buf)), 1);
		check("parse_quotes 250-char value into 255: length 250", strlen(s.buf), 250);
		check("parse_quotes 250-char value into 255: can1 intact", s.can1, 0xAA);

		reset();
		memcpy(q, "\"\"", 3);
		iparser = q;
		check("parse_quotes empty value: accepted", parse_quotes('"', s.buf, sizeof(s.buf)), 1);
		check("parse_quotes empty value: str empty", s.buf[0], 0);
		check("parse_quotes empty value: can1 intact", s.can1, 0xAA);

		reset();
		memcpy(q, "\"unterminated", 14);
		iparser = q;
		check("parse_quotes unterminated: refused", parse_quotes('"', s.buf, sizeof(s.buf)), 0);
		check("parse_quotes unterminated: str empty", s.buf[0], 0);
	}

	printf("== %d/%d passed, %d failed ==\n", passed, passed + failed, failed);
	return failed ? 1 : 0;
}
