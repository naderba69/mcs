/* zerohandoff.h — Zero-Latency Handoff (R22 candidate #23)
 *
 * PURPOSE: when a server fails mid-session, hand off the client to a
 * backup server without any visible interruption. The client continues
 * receiving CWs from the backup without re-requesting.
 *
 * MECHANISM: maintain a "hot standby" connection to a backup server.
 * When the primary server fails, immediately switch to the backup.
 *
 * CONFIG: ZERO HANDOFF: ON (default OFF = stock failover).
 *
 * ROLLBACK: remove zerohandoff_switch() calls; handoff is inert.
 */
#ifndef MCS_ZEROHANDOFF_H
#define MCS_ZEROHANDOFF_H

#include <stdint.h>
#include <string.h>

#define ZEROHANDOFF_CLIENTS 32
#define ZEROHANDOFF_BACKUPS 4

struct zerohandoff_backup {
    int server_id;
    int priority;  /* lower = higher priority */
    uint8_t available;
};

struct zerohandoff_client {
    uint32_t client_ip;
    uint16_t caid;
    uint32_t provid;
    uint16_t sid;
    int primary_server;
    struct zerohandoff_backup backups[ZEROHANDOFF_BACKUPS];
    int backup_count;
    uint32_t last_switch;
    uint32_t switch_count;
    uint8_t used;
};

struct zerohandoff_table {
    struct zerohandoff_client clients[ZEROHANDOFF_CLIENTS];
    int enabled;
    uint32_t total_switches;
};

static inline void zerohandoff_init(struct zerohandoff_table *t, int enabled)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t->enabled = enabled;
}

static inline int zerohandoff_enabled(const struct zerohandoff_table *t)
{
    return t && t->enabled;
}

/* Register a client with primary and backup servers */
static inline void zerohandoff_register(struct zerohandoff_table *t,
                                         uint32_t client_ip,
                                         uint16_t caid, uint32_t provid,
                                         uint16_t sid,
                                         int primary_server,
                                         const int *backup_servers,
                                         int backup_count)
{
    int i, free_slot = -1;
    struct zerohandoff_client *c;
    int count;

    if (!t) return;
    if (backup_count > ZEROHANDOFF_BACKUPS) backup_count = ZEROHANDOFF_BACKUPS;

    /* Find or allocate */
    for (i = 0; i < ZEROHANDOFF_CLIENTS; i++) {
        c = &t->clients[i];
        if (c->used && c->client_ip == client_ip &&
            c->caid == caid && c->provid == provid && c->sid == sid) {
            /* Update existing */
            c->primary_server = primary_server;
            count = backup_count;
            for (i = 0; i < count; i++) {
                c->backups[i].server_id = backup_servers[i];
                c->backups[i].priority = i;
                c->backups[i].available = 1;
            }
            c->backup_count = count;
            return;
        }
        if (!c->used && free_slot < 0) {
            free_slot = i;
        }
    }

    if (free_slot < 0) {
        /* Replace oldest */
        int oldest = 0;
        for (i = 1; i < ZEROHANDOFF_CLIENTS; i++) {
            if (t->clients[i].last_switch < t->clients[oldest].last_switch) {
                oldest = i;
            }
        }
        free_slot = oldest;
    }

    c = &t->clients[free_slot];
    c->client_ip = client_ip;
    c->caid = caid;
    c->provid = provid;
    c->sid = sid;
    c->primary_server = primary_server;
    count = backup_count;
    for (i = 0; i < count; i++) {
        c->backups[i].server_id = backup_servers[i];
        c->backups[i].priority = i;
        c->backups[i].available = 1;
    }
    c->backup_count = count;
    c->last_switch = 0;
    c->switch_count = 0;
    c->used = 1;
}

/* Switch to backup server.
 * Returns: new server_id, or -1 if no backup available.
 */
static inline int zerohandoff_switch(struct zerohandoff_table *t,
                                      uint32_t client_ip,
                                      uint16_t caid, uint32_t provid,
                                      uint16_t sid, uint32_t now)
{
    int i;
    struct zerohandoff_client *c;
    int best_backup = -1;
    int best_priority = 999;

    if (!zerohandoff_enabled(t)) return -1;

    for (i = 0; i < ZEROHANDOFF_CLIENTS; i++) {
        c = &t->clients[i];
        if (c->used && c->client_ip == client_ip &&
            c->caid == caid && c->provid == provid && c->sid == sid) {
            /* Find best available backup */
            int j;
            for (j = 0; j < c->backup_count; j++) {
                if (c->backups[j].available && c->backups[j].priority < best_priority) {
                    best_backup = c->backups[j].server_id;
                    best_priority = c->backups[j].priority;
                }
            }
            if (best_backup >= 0) {
                c->primary_server = best_backup;
                c->last_switch = now;
                c->switch_count++;
                t->total_switches++;
            }
            return best_backup;
        }
    }
    return -1;
}

/* Mark a backup server as unavailable */
static inline void zerohandoff_mark_unavailable(struct zerohandoff_table *t,
                                                 int server_id)
{
    int i, j;
    if (!t) return;
    for (i = 0; i < ZEROHANDOFF_CLIENTS; i++) {
        struct zerohandoff_client *c = &t->clients[i];
        if (!c->used) continue;
        for (j = 0; j < c->backup_count; j++) {
            if (c->backups[j].server_id == server_id) {
                c->backups[j].available = 0;
            }
        }
    }
}

#endif /* MCS_ZEROHANDOFF_H */
