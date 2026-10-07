/* TASK 3.8b: direct ASan probe for parse_quotes' bounded copy.
 *
 * Links the REAL ../src/parser.c (self-contained, same as test_parser)
 * and mimics the two dangerous call-site shapes:
 *   - read_config's stack scratch:  char str[255], a 400-char quoted value
 *   - http_file_data's heap fields: malloc'd char[512], a 600-char value
 * Before the fix both were strcpy()'d without a limit; the pre-fix run of
 * this probe died with "WRITE of size 401" (and the real server died in
 * read_config:2017 on the same shape). After the fix both values are
 * truncated at size-1, NUL-terminated, and ASan stays quiet.
 *
 * The empty-value and unterminated-value cases pin the stock behaviour
 * that must not change: "" leaves str empty and returns 1; a missing
 * closing quote returns 0 with str empty.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern char *iparser;
int parse_quotes( char quote, char *str, int size );

static int fails = 0;
static void ok( const char *what, int cond )
{
	printf("  %s %s\n", cond ? "[ ok ]" : "[FAIL]", what);
	if (!cond) fails++;
}

int main(void)
{
	static char line[1400];

	/* stack scratch, read_config shape: 400 chars in a char[255] */
	memset(line, 'A', sizeof(line));
	line[0] = '"'; line[401] = '"'; line[402] = 0;
	char str[255] = {0};
	iparser = line;
	ok("400-char value into char[255]: accepted", parse_quotes('"', str, sizeof(str)) == 1);
	ok("400-char value into char[255]: capped at 254", strlen(str) == 254);
	ok("400-char value into char[255]: still NUL-terminated", str[254] == 0 && str[255-1] == 0);

	/* heap field, http_file_data url shape: 600 chars into malloc'd 512 */
	memset(line, 'U', sizeof(line));
	line[0] = '"'; line[601] = '"'; line[602] = 0;
	char *url = malloc(512);
	memset(url, 0, 512);
	iparser = line;
	ok("600-char value into char[512]: accepted", parse_quotes('"', url, 512) == 1);
	ok("600-char value into char[512]: capped at 511", strlen(url) == 511);
	ok("600-char value into char[512]: still NUL-terminated", url[511] == 0);
	free(url);

	/* a value that fits stays complete */
	memcpy(line, "\"hello world\"", 13); line[13] = 0;
	iparser = line;
	char small[8];
	ok("short value into char[8]: accepted", parse_quotes('"', small, sizeof(small)) == 1);
	ok("short value into char[8]: intact (7 chars max)", strcmp(small, "hello w") == 0);

	/* empty value: stock behaviour unchanged */
	memcpy(line, "\"\" rest", 8); line[8] = 0;
	iparser = line;
	char e[16] = {0};
	ok("empty value returns 1", parse_quotes('"', e, sizeof(e)) == 1);
	ok("empty value leaves str empty", e[0] == 0);

	/* unterminated value: stock behaviour unchanged */
	memcpy(line, "\"no closing quote", 18); line[18] = 0;
	iparser = line;
	char u[16] = {0};
	ok("unterminated value returns 0", parse_quotes('"', u, sizeof(u)) == 0);
	ok("unterminated value leaves str empty", u[0] == 0);

	printf("== %s ==\n", fails ? "PROBE FAILED" : "probe clean (7/7)");
	return fails ? 1 : 0;
}
