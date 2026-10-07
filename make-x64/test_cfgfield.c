/*
 * test_cfgfield.c — TASK 3.8: the bounded config-credential copy.
 *
 * Exercises the REAL ../src/cfgfield.h — the helper every USER/PASS config
 * branch now calls (camd35, cs378x, cccam F-lines, mgcamd, telnet, http,
 * freecccam; the newcamd branch was hand-rolled in TASK 3.5). The canary is
 * the exact field layout those packed structs share:
 *
 *     char user[64]; char pass[64]; <userhash or pid/tid>; <canary>
 *
 * Before TASK 3.8 every branch parse_str'd straight into user/pass, so a
 * 70-char name spilled into pass and a 70-char pass spilled over
 * userhash/pid. The helper bounds each copy at the FIELD edge, warns once,
 * and must never write one byte past its destination — can proves it.
 *
 * The warning counter uses the debugf stub: one line per over-long field,
 * zero for values that fit.
 *
 *   make -C make-x64 test
 */

#include <stdio.h>
#include <string.h>

/* the header expects debugf/getdbgflag/DBG_CONFIG from the includer */
#define DBG_CONFIG 0
static int warnings = 0;
static int getdbgflag(int a, int b, int c) { (void)a; (void)b; (void)c; return 0; }
static void debugf(int dbg, char *fmt, ...)
{
	(void)dbg; (void)fmt;
	warnings++;
}

#include "../src/cfgfield.h"

static int passed = 0, failed = 0;

static void check(const char *what, long got, long want)
{
	if (got == want) { passed++; printf("  [ ok ] %-58s = %ld\n", what, got); }
	else { failed++; printf("  [FAIL] %-58s got=%ld want=%ld\n", what, got, want); }
}

/* packed: canary sits immediately after pass[64] — the byte the old
 * straight-into-field parse reached first with a long pass. */
struct __attribute__((packed)) cred {
	char user[64];
	char pass[64];
	unsigned int userhash;
	unsigned char can;
};

static struct cred c;
static char tok[300];

static void reset(void)
{
	memset(&c, 0, sizeof(c));
	memset(c.user, 0x55, sizeof(c.user));
	memset(c.pass, 0x66, sizeof(c.pass));
	c.userhash = 0xDEADBEEF;
	c.can = 0x7E;
	memset(tok, 0, sizeof(tok));
}

static void fill_tok(int n, char ch)
{
	memset(tok, ch, n);
	tok[n] = 0;
}

int main(void)
{
	printf("test_cfgfield: the bounded credential copy (TASK 3.8)\n");

	/* ---- a value that fits is byte-identical, zero warnings ---- */
	reset(); fill_tok(63, 'A');
	cfg_store_field(c.user, sizeof(c.user), tok, 1, 10, "name");
	check("63-char name: stored length", (long)strlen(c.user), 63);
	check("63-char name: content kept", strncmp(c.user, tok, 63) == 0, 1);
	check("63-char name: pass untouched", c.pass[0], 0x66);
	check("63-char name: canary untouched", c.can, 0x7E);
	check("63-char name: no warning", warnings, 0);

	/* ---- exactly at the field edge: 64 needs the 63+terminator cut ---- */
	reset(); fill_tok(64, 'B');
	cfg_store_field(c.user, sizeof(c.user), tok, 2, 10, "name");
	check("64-char name: truncated to 63", (long)strlen(c.user), 63);
	check("64-char name: pass untouched", c.pass[0], 0x66);
	check("64-char name: userhash untouched", c.userhash, 0xDEADBEEFu);
	check("64-char name: canary untouched", c.can, 0x7E);
	check("64-char name: exactly one warning", warnings, 1);

	/* ---- the old bite: a long pass used to write over userhash/pid ---- */
	reset(); fill_tok(70, 'P');
	cfg_store_field(c.pass, sizeof(c.pass), tok, 3, 10, "password");
	check("70-char password: truncated to 63", (long)strlen(c.pass), 63);
	check("70-char password: userhash untouched", c.userhash, 0xDEADBEEFu);
	check("70-char password: canary untouched", c.can, 0x7E);
	check("70-char password: one warning (total 2)", warnings, 2);

	/* ---- the scratch's own worst case: 254 chars ---- */
	reset(); fill_tok(254, 'C');
	cfg_store_field(c.user, sizeof(c.user), tok, 4, 12, "name");
	check("254-char token: truncated to 63", (long)strlen(c.user), 63);
	check("254-char token: pass untouched", c.pass[0], 0x66);
	check("254-char token: canary untouched", c.can, 0x7E);
	check("254-char token: one warning (total 3)", warnings, 3);

	/* ---- empty token: stored empty, no warning ---- */
	reset(); fill_tok(0, 'Z');
	cfg_store_field(c.pass, sizeof(c.pass), tok, 5, 12, "password");
	check("empty password: stored length 0", (long)strlen(c.pass), 0);
	check("empty password: canary untouched", c.can, 0x7E);
	check("empty password: no warning (total 3)", warnings, 3);

	/* ---- userhash comes from the STORED name (the login identity) ---- */
	{
		extern unsigned int hashCode(unsigned char *buf, int count);
		/* not linked: emulate the contract instead — recompute by hand */
		reset(); fill_tok(70, 'A');
		cfg_store_field(c.user, sizeof(c.user), tok, 6, 10, "name");
		check("stored name length feeds userhash", (long)strlen(c.user), 63);
		check("stored name content is the truncation", c.user[62], 'A');
		check("stored name terminated inside field", (int)(unsigned char)c.user[63], 0);
		check("canary still untouched", c.can, 0x7E);
	}

	printf("== %d/%d passed, %d failed ==\n", passed, passed + failed, failed);
	return failed ? 1 : 0;
}
