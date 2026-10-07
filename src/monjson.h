/*
 * monjson.h -- TASK R5 (D59): the machine readout, /json.
 *
 * A PURE document builder: no globals, no locks, no sockets. The caller
 * snapshots the live data into plain structs (the same unlocked read the
 * stock HTML pages have always done) and gets back one valid JSON
 * document for Grafana/alerts. Everything is hand-rolled -- no external
 * library -- and the unit test (make-x64/test_monjson.c) owns the
 * escaping, the comma discipline and the overflow safety.
 */
#ifndef MCS_MONJSON_H
#define MCS_MONJSON_H

#include <stdint.h>

#define MONJSON_MAX_PEERS   256
#define MONJSON_MAX_REPS     64   /* == PEERREP_SLOTS */
#define MONJSON_NAME_MAX     64
#define MONJSON_REASON_MAX   32
#define MONJSON_BUF_MAX  (256*1024)

/* TASK R11 (D65): row bound for the cwlog snapshot a caller may pass.
 * Must match CWLOG_CAP in cwlog.h; the /json handler sizes its local
 * with THIS one so the stock flavour (no cwlog.h) still compiles. */
#define MONJSON_MAX_LOGS  100

/* One cache-peer row, snapshotted by the caller. rep_* are filled from
 * the reputation snapshot by ip:port match; rep_stage is -1 when the
 * peer has no ladder record. */
struct monjson_peer {
	char          name[MONJSON_NAME_MAX];
	uint32_t      ip;
	int           port;
	int           ping;
	int           cards;
	unsigned long ecmnb, ecmok, hitnb;
	int           disabled;
	int           rep_stage;      /* -1 none, else 0..3 */
	int           rep_events;
	char          rep_reason[MONJSON_REASON_MAX];
};

/* One peer-reputation record, snapshotted via peerrep_get(). */
struct monjson_rep {
	uint32_t ip;
	int      port;
	int      stage;
	int      events;
	char     reason[MONJSON_REASON_MAX];
};

/* TASK R11 (D65): one verdict-ring row, snapshotted via cwlog_snapshot().
 * The 16 key bytes arrive already hex-encoded (33 = 32 chars + NUL) so
 * this header stays free of cwlog.h. */
struct monjson_cwl {
	uint32_t tick;
	int      caid, provid, sid;
	int      verdict, reason;
	int      peerid;
	char     cw[33];
};

struct monjson_in {
	int                   dcwstats_on;
	const unsigned long  *dcwstats;   /* DCW_REJ_COUNT entries, [0]=accepted */
	int                   peerrep_on;
	int                   th_distrust, th_isolate, th_ban;
	unsigned long         uptime;
	long                  now;
	int                   nreps;
	const struct monjson_rep *reps;
	int                   npeers;
	const struct monjson_peer *peers;
	/* TASK R11 (D65): the verdict ring. Stock builds pass on=0, cap=0,
	 * nlogs=0, logs=NULL and get the same empty section shape. */
	int                   cwlog_on;
	int                   cwlog_cap;
	int                   nlogs;
	const struct monjson_cwl *logs;
};
/* Build the document. Returns its length, or -1 when cap was too small
 * (buf is then empty and NUL-safe -- nothing partial ever goes out). */
int monjson_build(char *buf, int cap, const struct monjson_in *in);

/* JSON string escaping: \" \\ \b \f \n \r \t, \u00XX for the other
 * control bytes, bytes >= 0x80 pass through raw (the document is UTF-8).
 * Returns 0, or -1 when dst was too small (dst stays NUL-terminated). */
int monjson_escape(char *dst, int dstcap, const char *src);

#endif /* MCS_MONJSON_H */
