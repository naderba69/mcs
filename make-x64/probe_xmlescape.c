/*
 * probe_xmlescape.c — TASK 3.9: call the REAL xmlescape with a cell-sized
 * input whose escaped form cannot fit any frame, under ASan.
 *
 * The Makefile rule compiles the production httpserver.c (the one TU that
 * defines xmlescape) with -fsanitize=address and -Dxmlescape=real_xmlescape,
 * links it with the normal objects, and hands main() to this file. So what
 * runs below is the byte-for-byte production function — no copy, no stub.
 *
 * Input: one cell[2048] full of '&' (what a config or peer name could
 * degenerate to). Escaping multiplies it by 5 — 10,235 bytes — far past any
 * frame. Pre-fix (exml[5000], unbounded dest walk) ASan traps on the frame
 * write before anything else runs. Post-fix the function truncates at the
 * cell edge, the page text stays well-formed, and this probe exits 0.
 *
 *   make -C make-x64 x64/probe_xmlescape && ./x64/probe_xmlescape
 */

#include <stdio.h>
#include <string.h>

extern char *real_xmlescape(char *str);

int main(void)
{
	static char cell[2048];	/* the callers' cell width — grep-verified */
	memset(cell, '&', sizeof(cell) - 1);
	cell[sizeof(cell) - 1] = 0;

	char *r = real_xmlescape(cell);
	int n = (int)strlen(r);
	printf("escaped length=%d\n", n);
	if (n > 2047) {
		printf("[FAIL] escaped output exceeds the 2048-byte cell frame\n");
		return 1;
	}
	/* the truncated text must still be well-formed: whole &amp; entities */
	{ int i, bad = 0;
	  if (n % 5 != 0) bad = 1;
	  else for (i = 0; i < n; i += 5) if (memcmp(r + i, "&amp;", 5)) { bad = 1; break; }
	  if (bad) { printf("[FAIL] escaped text is not whole &amp; entities\n"); return 1; }
	}
	printf("[ ok ] production xmlescape stayed inside its frame\n");
	return 0;
}
