#ifndef MCS_DEBUGCORE_H
#define MCS_DEBUGCORE_H

/*
 * TASK 3.3 -- the bounded pieces of the logging core, as pure functions the
 * unit can drive. debug.c uses these; the semantics are upstream's ring
 * (append, wrap at MAX_DBGLINES, readers walk (cursor-35)..forward), with
 * one change: no line ever enters a slot unbounded.
 */

#define DBG_MAXLINE 512   /* one ring slot; debug.c's dbgline uses this      */

/*
 * Clamp-copy one log line into `outsz` bytes. Returns the stored length.
 * Always terminates. This replaces the raw strcpy(dbgline[idbgline], line):
 * before 3.3 a caller line longer than the slot wrote past it in .bss.
 */
static int dbg_addline(char *out, int outsz, const char *line)
{
	int o = 0;

	if (!out || outsz <= 0) return -1;
	if (!line) line = "";
	while (line[o] && o < outsz - 1) { out[o] = line[o]; o++; }
	out[o] = '\0';
	return o;
}

/*
 * Append `line` to a ring of `nslots` slots of `slotsize` bytes, advancing
 * *cursor with the upstream wrap. Returns the stored length.
 */
static int dbg_store(char ring[][DBG_MAXLINE], int nslots, int *cursor,
                     const char *line, int slotsize)
{
	int n = dbg_addline(ring[*cursor], slotsize, line);
	*cursor += 1;
	if (*cursor >= nslots) *cursor = 0;
	return n;
}

#endif /* MCS_DEBUGCORE_H */
