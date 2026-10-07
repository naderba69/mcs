/*
 * test_loginthrottle.c -- TASK R3 (D57): the progressive delay schedule,
 * the per-IP failure table, and the allowlist.
 *
 * The module is built with -DMCS_LT_NOGLOBALS so THIS test supplies the
 * clock (lt_now), exactly like MCS_DCWSTATS_NOGLOBALS supplies the counter
 * array for test_dcwstats. Every delay here is a returned number -- the
 * module never sleeps; the doors sleep.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#include "../src/loginthrottle.h"

static uint32_t fake_now = 1000;   /* ms */

uint32_t lt_now(void) { return fake_now; }

static int checks = 0, fails = 0;

static void check(const char *what, int ok)
{
	checks++;
	if (!ok) { fails++; printf("  [FAIL] %s\n", what); }
	else printf("  [ ok ] %s\n", what);
}

int main(void)
{
	struct login_allow lst;

	printf("-- 1. the pure schedule --\n");
	check("base 0 = off",            login_delay_ms(0, 1) == 0);
	check("fails 0 = nothing",       login_delay_ms(250, 0) == 0);
	check("1st failure = base",      login_delay_ms(250, 1) == 250);
	check("2nd = x2",                login_delay_ms(250, 2) == 500);
	check("3rd = x4",                login_delay_ms(250, 3) == 1000);
	check("7th hits the cap",        login_delay_ms(250, 7) == 8000);
	check("20th stays at the cap",   login_delay_ms(250, 20) == 8000);
	check("no overflow at fail 1000",login_delay_ms(250, 1000) == 8000);

	printf("-- 2. the allowlist --\n");
	login_allow_reset(&lst);
	check("empty list passes anyone",  login_allowed(&lst, 0x0100007F));
	check("add one",                   login_allow_add(&lst, 0x0100007F) == 1);
	check("duplicate add is harmless", login_allow_add(&lst, 0x0100007F) == 1);
	check("count stayed 1",            lst.count == 1);
	check("listed ip passes",          login_allowed(&lst, 0x0100007F));
	check("stranger refused",          !login_allowed(&lst, 0x020000A8));
	{
		int i, full_ok = 1;
		for (i=lst.count; i<LOGIN_ALLOW_MAX; i++) login_allow_add(&lst, 0x0A000000+i);
		check("list filled to max",    lst.count == LOGIN_ALLOW_MAX);
		full_ok = (login_allow_add(&lst, 0x0B000000) == 0);
		check("over the max refused",  full_ok);
		check("a listed member still passes", login_allowed(&lst, 0x0A000005));
	}

	printf("-- 3. the failure table --\n");
	{
		uint32_t a = 0x0100007F, b = 0x020000A8;
		check("first failure counts 1",        login_note_failure(a) == 1);
		fake_now += 10;
		check("second counts 2",               login_note_failure(a) == 2);
		fake_now += 10;
		check("third counts 3",                login_note_failure(a) == 3);
		check("another ip is independent",     login_note_failure(b) == 1);
		check("a's delay follows its own count", login_delay_ms(250, 3) == 1000);
		check("success forgives",              (login_note_success(a), login_note_failure(a) == 1));
		check("b untouched by a's success",    login_note_failure(b) == 2);
		/* expiry: a quiet slot forgets */
		fake_now += LOGIN_SLOT_EXPIRE + 1;
		check("a quiet slot starts from zero", login_note_failure(b) == 1);
	}

	printf("\n== %d/%d passed, %d failed ==\n", checks - fails, checks, fails);
	return fails ? 1 : 0;
}
