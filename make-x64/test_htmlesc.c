/*
 * TASK 3.2 -- the HTML escape used by the web interface (../src/htmlesc.h).
 * The five metacharacters, honesty of plain names, bounds, degenerates, and
 * one end-to-end cell build proving a <script> username cannot reach the
 * page raw.
 */
#include <stdio.h>
#include <string.h>
#include "../src/htmlesc.h"

static int fails = 0, oks = 0;

static void check(int cond, const char *what)
{
	if (cond) { oks++; printf("  [ ok ] %s\n", what); }
	else { fails++; printf("  [FAIL] %s\n", what); }
}

int main(void)
{
	char out[2048];

	/* the five metacharacters, each alone */
	html_esc(out, (int)sizeof(out), "<script>alert(1)</script>");
	check(strcmp(out, "&lt;script&gt;alert(1)&lt;/script&gt;") == 0,
	      "a <script> username comes out inert");
	html_esc(out, (int)sizeof(out), "a&b");
	check(strcmp(out, "a&amp;b") == 0, "ampersand becomes &amp;");
	html_esc(out, (int)sizeof(out), "s\"t");
	check(strcmp(out, "s&quot;t") == 0, "double quote becomes &quot;");
	html_esc(out, (int)sizeof(out), "s't");
	check(strcmp(out, "s&#39;t") == 0, "single quote becomes &#39;");

	/* all five at once, order matters for & */
	html_esc(out, (int)sizeof(out), "<&>\"'");
	check(strcmp(out, "&lt;&amp;&gt;&quot;&#39;") == 0,
	      "all five metacharacters in one string");

	/* honesty: plain config names render byte-for-byte */
	html_esc(out, (int)sizeof(out), "user1.p1 - (x86) [0123]");
	check(strcmp(out, "user1.p1 - (x86) [0123]") == 0,
	      "a plain name is untouched");

	/* degenerates */
	check(html_esc(out, (int)sizeof(out), "") == 0 && out[0] == 0,
	      "empty name, empty output");
	html_esc(out, (int)sizeof(out), NULL);
	check(out[0] == 0, "NULL name, empty output");
	check(html_esc(NULL, 100, "x") == -1, "NULL buffer refused");
	check(html_esc(out, 0, "x") == -1, "zero size refused");
	check(html_esc(out, -5, "x") == -1, "negative size refused");

	/* bounds: truncate, always terminate */
	html_esc(out, 4, "abcdef");
	check(strcmp(out, "abc") == 0, "truncation leaves room for the terminator");
	html_esc(out, 3, "abcdef");
	check(strcmp(out, "ab") == 0, "size 3 keeps two chars and terminates");
	html_esc(out, 1, "abcdef");
	check(out[0] == 0, "size 1 is an empty string");
	html_esc(out, 6, "a<b");
	check(strcmp(out, "a&lt;") == 0, "an entity never lands half-written");
	html_esc(out, 7, "a<b");
	check(strcmp(out, "a&lt;b") == 0, "the boundary size takes the whole escape");
	{
		/* Guard past the size we hand the function. The old check read
		 * tiny[7], which html_esc never writes when the terminator lands
		 * earlier, so valgrind reported an uninitialised read in the test
		 * itself (TASK 4.1). The contract is: terminator inside the given
		 * size, and no write past that size. */
		char tiny[9];
		memset(tiny, 0xA5, sizeof(tiny));
		int n = html_esc(tiny, 8, "<<<<<<<");
		check(n >= 0 && n < 8 && tiny[n] == 0 && tiny[8] == (char)0xA5,
		      "a flood of metacharacters stays inside a 8-byte buffer");
	}

	/* length bookkeeping */
	{
		int n = html_esc(out, (int)sizeof(out), "a&b<c>");
		check(n == (int)strlen(out), "the returned length is the real length");
	}

	/* end-to-end: the exact cell the index page builds */
	{
		char cell[2048], esc[512];
		const char *user = "<script>alert(1)</script>";
		html_esc(esc, (int)sizeof(esc), user);
		sprintf(cell, "<a href='/newcamdclient?id=%d'>%s</a>", 7, esc);
		check(strstr(cell, "<script>") == NULL,
		      "the built cell carries no raw <script>");
		check(strstr(cell, "&lt;script&gt;") != NULL,
		      "the built cell carries the escaped name");
		/* and the honest path is unchanged end-to-end */
		html_esc(esc, (int)sizeof(esc), "u1");
		sprintf(cell, "<a href='/newcamdclient?id=%d'>%s</a>", 7, esc);
		check(strcmp(cell, "<a href='/newcamdclient?id=7'>u1</a>") == 0,
		      "an honest name builds the exact stock cell");
	}

	/* a 300-char worst case fits the real buffers */
	{
		char name[301], esc[2048];
		memset(name, '<', 300); name[300] = 0;
		int n = html_esc(esc, (int)sizeof(esc), name);
		check(n == 1200 && (int)strlen(esc) == 1200,
		      "300 worst-case chars escape to 1200 and fit the cells");
	}

	printf("%d ok, %d failed\n", oks, fails);
	return fails ? 1 : 0;
}
