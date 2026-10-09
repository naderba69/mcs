#include <stdio.h>
#include <string.h>

#include "../src/safe_string.h"

static int checks = 0;
static int failures = 0;

static void check(const char *name, int ok)
{
	checks++;
	if (ok) printf("  [ ok ] %s\n", name);
	else {
		failures++;
		printf("  [FAIL] %s\n", name);
	}
}

int main(void)
{
	char dst[8];
	char small[5];
	char full[3] = { 'a', 'b', 'c' };
	char out[8];
	size_t n;
	int written;

	n = mcs_strlcpy(dst, "peer", sizeof(dst));
	check("strlcpy copies a fitting string and returns its length",
	      n == 4 && strcmp(dst, "peer") == 0);

	n = mcs_strlcpy(small, "abcdef", sizeof(small));
	check("strlcpy truncates, terminates, and reports the source length",
	      n == 6 && strcmp(small, "abcd") == 0);

	small[0] = 'Q';
	n = mcs_strlcpy(small, "xy", 0);
	check("strlcpy with zero capacity writes nothing",
	      n == 2 && small[0] == 'Q');

	n = mcs_strlcat(dst, "-ok", sizeof(dst));
	check("strlcat appends when the whole result fits",
	      n == 7 && strcmp(dst, "peer-ok") == 0);

	mcs_strlcpy(small, "ab", sizeof(small));
	n = mcs_strlcat(small, "cdef", sizeof(small));
	check("strlcat truncates, terminates, and reports the attempted length",
	      n == 6 && strcmp(small, "abcd") == 0);

	n = mcs_strlcat(full, "x", sizeof(full));
	check("strlcat leaves an unterminated full destination unchanged",
	      n == 4 && full[0] == 'a' && full[1] == 'b' && full[2] == 'c');

	written = mcs_snprintf(out, sizeof(out), "peer %u", 7u);
	check("snprintf wrapper formats into a fitting buffer",
	      written == 6 && strcmp(out, "peer 7") == 0);

	written = mcs_snprintf(small, sizeof(small), "%s", "abcdef");
	check("snprintf wrapper truncates and always terminates",
	      written == 6 && strcmp(small, "abcd") == 0);

	small[0] = 'Q';
	written = mcs_snprintf(small, 0, "%s", "x");
	check("snprintf wrapper rejects zero capacity without writing",
	      written == -1 && small[0] == 'Q');

	printf("safestr: %d/%d ok\n", checks - failures, checks);
	return failures ? 1 : 0;
}
