/*
 * cfgfield.h -- TASK 3.8: bounded storage for small config credential fields.
 *
 * THE PATTERN THIS KILLS. Every "USER/PASS" config branch parses a token
 * with parse_str() (clamp 254 since TASK 3.7) directly into a char[64]
 * field of a packed struct -- user[64] and pass[64] are ALWAYS adjacent
 * (cs_client_data, camd35_client_data, cc_client_data, mg_client_data,
 * config_data.telnet, config_data.http, freecccam), usually followed by
 * userhash or by the owning thread's pid/tid. A name over 63 chars
 * spilled into pass (userhash hashed over a string crossing the edge);
 * a pass over 63 chars wrote straight over userhash / pid / tid. The
 * entry silently can never log in again -- or worse, a thread handle
 * rots. TASK 3.5 fixed the newcamd branch by hand; this header is that
 * same recipe, callable from every branch.
 *
 * CONTRACT. `tok` is the caller's parse scratch (NUL-terminated, at most
 * 254 chars by 3.7). The copy is bounded at the FIELD edge (size-1 plus
 * terminator); one house-format warning names the config line and column
 * and says the truncated pair stays a working credential. Nothing else
 * changes: values that fit are byte-identical.
 *
 * Included by config.c (the branches) and by test_cfgfield.c (which
 * stubs debugf/getdbgflag and proves the bound on a canary struct).
 */

#ifndef CFGFIELD_H
#define CFGFIELD_H

/* debugf()/getdbgflag()/DBG_CONFIG come from the includer: config.c already
 * includes debug.h through common.h; the unit test provides stubs. */

/*
 * Copy `tok` into `dst` (size `dstsize`, which includes the terminator),
 * truncating at the field edge with exactly one warning per call.
 * `what` is "name" or "password" in the message; nbline/col locate the
 * config line in the house format.
 */
static void cfg_store_field(char *dst, int dstsize, char *tok,
                            int nbline, int col, const char *what)
{
	int len = 0;
	while ( tok[len] ) len++;
	if ( len >= dstsize ) {
		len = dstsize - 1;
		tok[len] = 0;
		debugf(getdbgflag(DBG_CONFIG,0,0),
		       " config(%d,%d): USER %s too long, truncated to %d chars (field limit); the truncated pair stays a working credential\n",
		       nbline, col, what, len);
	}
	memcpy( dst, tok, len+1 );
}

#endif /* CFGFIELD_H */
