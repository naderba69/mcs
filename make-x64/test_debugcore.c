/*
 * TASK 3.3 -- the logging core (debugf / debug / add_dbgline), live.
 *
 * THE PROPERTY. Every line that enters the debug ring is stored, printed,
 * written to the file and mirrored to the trace socket byte-for-byte, and
 * NEVER past 512 bytes -- no matter how absurd the caller's arguments are.
 * Before 3.3 the ring entry was an unbounded strcpy of a vsprintf'd line:
 * a ~900-byte caller line (a long client name inside a normal format)
 * wrote past dbgline[..][512] in .bss. The formats were also copied
 * unbounded, and fdebugf formatted into a 4096 stack frame unbounded.
 *
 * The ring must also keep its semantics: append, wrap at MAX_DBGLINES,
 * read-back runs forward from (current-35) mod 70 exactly as the /debug
 * page walks it.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "../src/debug.h"

#define MAXLINE 512   /* the real ring line size, from debug.c */

/* the ring under test is static in debug.c: the unit builds it directly */
#include "../src/debugcore.h"

static int fails = 0, oks = 0;

static void check(int cond, const char *what)
{
	if (cond) { oks++; printf("  [ ok ] %s\n", what); }
	else { fails++; printf("  [FAIL] %s\n", what); }
}

int main(void)
{
	char out[4096];

	/* 1. short line survives whole */
	dbg_addline(out, (int)sizeof(out), "hello world");
	check(strcmp(out, "hello world") == 0, "a short line comes back whole");

	/* 2. an absurd line is clamped to the dest, terminated, nothing past */
	{
		char big[6000];
		memset(big, 'x', sizeof(big));
		big[sizeof(big)-1] = 0;
		dbg_addline(out, (int)sizeof(out), big);
		check(strlen(out) == (int)sizeof(out) - 1,
		      "a 6000-byte line into 4096 lands as exactly 4095 bytes");
	}

	/* 3. degenerates */
	{
		char small[8];
		check(dbg_addline(small, (int)sizeof(small), "abcdefghij") != -1 &&
		      strlen(small) == 7, "a small dest is filled to its bound");
		check(dbg_addline(small, 1, "x") != -1 && small[0] == 0,
		      "dest size 1 stays empty");
		check(dbg_addline(small, 0, "x") == -1, "dest size 0 refused");
		check(dbg_addline(small, 8, NULL) != -1 && small[0] == 0,
		      "NULL line treated as empty");
	}

	/* 4. the exact exploit geometry: the old bug needed a ~900-byte caller
	 *    line to walk past the 512-byte slot */
	{
		char name[900], line[1024], slot[MAXLINE];
		memset(name, '<', sizeof(name)); name[sizeof(name)-1] = 0;
		int n = snprintf(line, sizeof(line), " newcamd: client '%s' connected", name);
		int m = dbg_addline(slot, (int)sizeof(slot), line);
		check(n > MAXLINE && m == MAXLINE - 1,
		      "a 900+ byte caller line clamps at a 512-byte slot edge");
	}

	/* 5. ring semantics preserved: append, wrap, read back forward */
	{
		char ring[MAX_DBGLINES][MAXLINE];
		int cur = 0;
		int i;
		char tmp[MAXLINE];
		for (i = 0; i < 75; i++) {
			snprintf(tmp, sizeof(tmp), "line %d", i);
			dbg_store(ring, MAX_DBGLINES, &cur, tmp, MAXLINE);
		}
		check(cur == 5, "75 entries wrap to cursor 5");
		check(strcmp(ring[0], "line 70") == 0 && strcmp(ring[4], "line 74") == 0,
		      "the newest entries overwrite the oldest slots: 70..74 now in 0..4");
		check(strcmp(ring[5], "line 5") == 0 && strcmp(ring[69], "line 69") == 0,
		      "and the untouched slots keep their first pass");
		/* the /debug walk: 35 back from the cursor, forward, stop at cursor */
		{
			int j = cur - 35; if (j < 0) j += MAX_DBGLINES;
			int count = 0, first = j;
			do { count++; j++; if (j >= MAX_DBGLINES) j = 0; } while (j != cur);
			check(count == 35 && first == 40,
			      "the /debug read-back walks (cursor-35)..(cursor-1): 35 lines");
		}
	}

	/* 6. byte-for-byte identity for printable lines (what /debug shows) */
	{
		char ring[MAX_DBGLINES][MAXLINE];
		int cur = 0;
		char tmp[MAXLINE];
		memset(tmp, 'A', 400); tmp[400] = 0;
		dbg_store(ring, MAX_DBGLINES, &cur, tmp, MAXLINE);
		check(strlen(ring[0]) == 400 && ring[0][399] == 'A',
		      "a 400-byte line fits whole and comes back whole");
	}

	printf("%d ok, %d failed\n", oks, fails);
	return fails ? 1 : 0;
}
