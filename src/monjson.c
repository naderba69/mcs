/*
 * monjson.c -- TASK R5 (D59): the /json document, hand-rolled.
 *
 * Why pure: the endpoint is one more route in gererClient()'s chain,
 * behind the SAME Basic-auth gate as every page (the gate runs before
 * the router, and the R3 doors sit in front of the gate), so the module
 * itself must stay free of cfg/prg/socket knowledge to stay unit-testable.
 * The stock HTML pages read the peer lists unlocked; this readout reads
 * the same fields the same way -- a torn diagnostic number is not a
 * correctness problem here (dcwstats.h made the same call).
 *
 * Shape (key order is fixed, the tests pin it):
 * {"version":"r82","uptime":s,"now":epoch,
 *  "dcwstats":{"on":0|1,"accepted":n,...one key per dcwstats_reason_name...},
 *  "peerrep":{"on":0|1,"thresholds":{"distrust":n,"isolate":n,"ban":n},
 *             "peers":[{"peer":"a.b.c.d:p","stage":n,"stage_name":"...",
 *                       "events":n,"last":"..."}]},
 *  "cache_peers":[{"name":"...","ip":"a.b.c.d","port":n,"ping":n,
 *                  "cards":n,"ecm":n,"ecm_ok":n,"hits":n,"disabled":0|1,
 *                  "rep_stage":n,"rep_stage_name":"...","rep_events":n,
 *                  "rep_last":"..."}]}
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "common.h"
#include "monjson.h"
#include "dcwstats.h"     /* DCW_REJ_COUNT + dcwstats_reason_name() */
#include "cwlog.h"        /* TASK R11 (D65): verdict/reason names   */

/* stage names -- the same words the ladder logs, telnet PEERREP prints
 * and the reputation file stores. */
static const char *const mj_stagename[] = { "monitor", "distrust", "isolate", "ban" };

int monjson_escape(char *dst, int dstcap, const char *src)
{
	if (!dst || dstcap < 1) return -1;
	char *p = dst;
	char *end = dst + dstcap - 1;          /* room for the NUL */
	if (!src) src = "";
	while (*src) {
		unsigned char c = (unsigned char)*src++;
		const char *esc = NULL;
		char ubuf[8];
		int len;
		switch (c) {
		case '"':  esc = "\\\""; len = 2; break;
		case '\\': esc = "\\\\"; len = 2; break;
		case '\b': esc = "\\b";  len = 2; break;
		case '\f': esc = "\\f";  len = 2; break;
		case '\n': esc = "\\n";  len = 2; break;
		case '\r': esc = "\\r";  len = 2; break;
		case '\t': esc = "\\t";  len = 2; break;
		default:
			if (c < 0x20) {
				snprintf(ubuf, sizeof(ubuf), "\\u%04X", c);
				esc = ubuf; len = 6;
			} else {
				ubuf[0] = (char)c; ubuf[1] = 0;
				esc = ubuf; len = 1;
			}
		}
		if (p + len > end) return -1;   /* dst stays NUL-terminated below */
		memcpy(p, esc, len);
		p += len;
	}
	*p = 0;
	return 0;
}

/* output cursor: append-only, overflow latches once and the whole
 * document is then abandoned -- a monitoring feed must never serve a
 * truncated (invalid) JSON body. */
struct mjout {
	char *p, *end;
	int   over;
};

static void mjput(struct mjout *o, const char *s)
{
	if (o->over) return;
	if (!s) { o->over = 1; return; }
	int len = (int)strlen(s);
	if (o->p + len > o->end) { o->over = 1; return; }
	memcpy(o->p, s, len);
	o->p += len;
}

static void mjfmt(struct mjout *o, const char *fmt, ...)
{
	char tmp[512];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	if (n < 0) { o->over = 1; return; }
	if (n >= (int)sizeof(tmp)) n = (int)sizeof(tmp) - 1;
	mjput(o, tmp);
}

/* "key":"escaped value", */
static void mjkv_strq(struct mjout *o, const char *key, const char *val, int comma)
{
	char esc[MONJSON_NAME_MAX*6 + 8];
	mjfmt(o, "\"%s\":\"", key);
	if (!o->over) {
		if (monjson_escape(esc, sizeof(esc), val) != 0) { o->over = 1; return; }
		mjput(o, esc);
	}
	mjput(o, "\"");
	if (comma) mjput(o, ",");
}

static void mjkv_int(struct mjout *o, const char *key, long val, int comma)
{
	mjfmt(o, "\"%s\":%ld", key, val);
	if (comma) mjput(o, ",");
}

static void mjdotted(struct mjout *o, uint32_t ip)
{
	mjfmt(o, "%u.%u.%u.%u", 0xFFu&(ip), 0xFFu&(ip>>8), 0xFFu&(ip>>16), 0xFFu&(ip>>24));
}

int monjson_build(char *buf, int cap, const struct monjson_in *in)
{
	if (!buf || cap < 2 || !in) return -1;
	struct mjout o;
	o.p = buf; o.end = buf + cap - 1; o.over = 0;

	mjput(&o, "{\"version\":\"r" REVISION_STR "\",");
	mjkv_int(&o, "uptime", (long)in->uptime, 1);
	mjkv_int(&o, "now", in->now, 0);
	mjput(&o, ",");

	/* -- dcwstats: index 0 is the acceptance counter, 1.. the named
	 *    rejections; the keys ARE the telnet/HTML names. */
	mjput(&o, "\"dcwstats\":{");
	mjkv_int(&o, "on", in->dcwstats_on ? 1 : 0, 1);
	int i;
	for (i = 0; i < DCW_REJ_COUNT; i++) {
		char esc[64];
		if (monjson_escape(esc, sizeof(esc), dcwstats_reason_name(i)) != 0) { o.over = 1; break; }
		mjfmt(&o, "\"%s\":%lu", esc, in->dcwstats ? in->dcwstats[i] : 0UL);
		if (i < DCW_REJ_COUNT-1) mjput(&o, ",");
	}
	mjput(&o, "},");

	/* -- TASK R11 (D65): the per-CW verdict ring -- the last keys the
	 *    cache judged, newest first. Stock builds emit the same shape
	 *    with on=0, capacity 0 and an empty rows array; the name arrays
	 *    are always present so a reader never guesses. */
	mjput(&o, "\"cwlog\":{");
	mjkv_int(&o, "on", in->cwlog_on ? 1 : 0, 1);
	mjkv_int(&o, "capacity", in->cwlog_cap, 1);
	mjkv_int(&o, "count", in->nlogs, 1);
	mjput(&o, "\"verdicts\":[");
	for (i = 0; i <= CWLOG_STORED; i++) {
		char esc[64];
		if (monjson_escape(esc, sizeof(esc), cwlog_verdict_name(i)) != 0) { o.over = 1; break; }
		mjput(&o, "\""); mjput(&o, esc); mjput(&o, "\"");
		if (i < CWLOG_STORED) mjput(&o, ",");
	}
	mjput(&o, "],\"reasons\":[");
	for (i = 0; i <= CWLOG_R_PROFILE; i++) {
		char esc[64];
		if (monjson_escape(esc, sizeof(esc), cwlog_reason_name(i)) != 0) { o.over = 1; break; }
		mjput(&o, "\""); mjput(&o, esc); mjput(&o, "\"");
		if (i < CWLOG_R_PROFILE) mjput(&o, ",");
	}
	mjput(&o, "],\"rows\":[");
	for (i = 0; i < in->nlogs; i++) {
		const struct monjson_cwl *r = &in->logs[i];
		mjput(&o, "{");
		mjkv_int(&o, "tick", (long)r->tick, 1);
		mjkv_int(&o, "caid", r->caid, 1);
		mjkv_int(&o, "provid", r->provid, 1);
		mjkv_int(&o, "sid", r->sid, 1);
		mjkv_int(&o, "verdict", r->verdict, 1);
		mjkv_strq(&o, "verdict_name", cwlog_verdict_name(r->verdict), 1);
		mjkv_int(&o, "reason", r->reason, 1);
		mjkv_strq(&o, "reason_name", cwlog_reason_name(r->reason), 1);
		mjkv_int(&o, "peer", r->peerid, 1);
		mjkv_strq(&o, "cw", r->cw, 0);
		mjput(&o, "}");
		if (i < in->nlogs-1) mjput(&o, ",");
	}
	mjput(&o, "]},");

	/* -- the peer-reputation ladder (R4/D58): thresholds for alerting,
	 *    every record with stage as number AND name. */
	mjput(&o, "\"peerrep\":{");
	mjkv_int(&o, "on", in->peerrep_on ? 1 : 0, 1);
	mjput(&o, "\"thresholds\":{");
	mjkv_int(&o, "distrust", in->th_distrust, 1);
	mjkv_int(&o, "isolate", in->th_isolate, 1);
	mjkv_int(&o, "ban", in->th_ban, 0);
	mjput(&o, "},");
	mjput(&o, "\"peers\":[");
	for (i = 0; i < in->nreps; i++) {
		const struct monjson_rep *r = &in->reps[i];
		mjput(&o, "{\"peer\":\"");
		mjfmt(&o, "%u.%u.%u.%u:%d", 0xFFu&(r->ip), 0xFFu&(r->ip>>8), 0xFFu&(r->ip>>16), 0xFFu&(r->ip>>24), r->port);
		mjput(&o, "\",");
		mjkv_int(&o, "stage", r->stage, 1);
		mjkv_strq(&o, "stage_name", (r->stage>=0 && r->stage<=3) ? mj_stagename[r->stage] : "", 1);
		mjkv_int(&o, "events", r->events, 1);
		mjkv_strq(&o, "last", r->reason, 0);
		mjput(&o, "}");
		if (i < in->nreps-1) mjput(&o, ",");
	}
	mjput(&o, "]},");

	/* -- cache peers: the rows the /cache page shows, machine-readable,
	 *    each merged with its ladder record (ip:port match). */
	mjput(&o, "\"cache_peers\":[");
	int n;
	for (n = 0; n < in->npeers; n++) {
		const struct monjson_peer *w = &in->peers[n];
		mjput(&o, "{");
		mjkv_strq(&o, "name", w->name, 1);
		mjput(&o, "\"ip\":\"");
		mjdotted(&o, w->ip);
		mjput(&o, "\",");
		mjkv_int(&o, "port", w->port, 1);
		mjkv_int(&o, "ping", w->ping, 1);
		mjkv_int(&o, "cards", w->cards, 1);
		mjkv_int(&o, "ecm", (long)w->ecmnb, 1);
		mjkv_int(&o, "ecm_ok", (long)w->ecmok, 1);
		mjkv_int(&o, "hits", (long)w->hitnb, 1);
		mjkv_int(&o, "disabled", w->disabled ? 1 : 0, 1);
		mjkv_int(&o, "rep_stage", w->rep_stage, 1);
		mjkv_strq(&o, "rep_stage_name", (w->rep_stage>=0 && w->rep_stage<=3) ? mj_stagename[w->rep_stage] : "", 1);
		mjkv_int(&o, "rep_events", w->rep_events, 1);
		mjkv_strq(&o, "rep_last", w->rep_reason, 0);
		mjput(&o, "}");
		if (n < in->npeers-1) mjput(&o, ",");
	}
	mjput(&o, "]}");

	if (o.over) { buf[0] = 0; return -1; }
	*(o.p) = 0;
	return (int)(o.p - buf);
}
