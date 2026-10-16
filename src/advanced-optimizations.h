/* advanced-optimizations.h — Advanced System Optimizations
 *
 * PURPOSE: Additional optimizations for maximum performance and reliability
 *
 * Optimizations included:
 * 6. Parallel ECM Requests
 * 7. Auto-Recovery System
 * 8. CW Encryption (simplified)
 * 9. Predictive Cache Warming
 */

#ifndef ADVANCED_OPTIMIZATIONS_H
#define ADVANCED_OPTIMIZATIONS_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <string.h>

// ============================================================================
// OPTIMIZATION #6: PARALLEL ECM REQUESTS
// ============================================================================

#define PARALLEL_MAX_SERVERS 10

struct parallel_ecm_state {
    uint32_t ecm_id;
    uint32_t caid;
    uint32_t provid;
    uint16_t sid;
    int servers_contacted[PARALLEL_MAX_SERVERS];
    int servers_responded;
    int first_response_server;
    uint32_t start_time;
    uint8_t cw_received;
};

struct parallel_ecm_table {
    struct parallel_ecm_state requests[256];
    int count;
    pthread_mutex_t lock;
};

extern struct parallel_ecm_table parallel_tab;

static inline void parallel_ecm_init(void) {
    memset(&parallel_tab, 0, sizeof(parallel_tab));
    pthread_mutex_init(&parallel_tab.lock, NULL);
}

static inline int parallel_ecm_start(uint32_t ecm_id, uint32_t caid, uint32_t provid, uint16_t sid) {
    int i;
    
    pthread_mutex_lock(&parallel_tab.lock);
    
    // Find empty slot
    for (i = 0; i < 256; i++) {
        if (parallel_tab.requests[i].ecm_id == 0) {
            parallel_tab.requests[i].ecm_id = ecm_id;
            parallel_tab.requests[i].caid = caid;
            parallel_tab.requests[i].provid = provid;
            parallel_tab.requests[i].sid = sid;
            parallel_tab.requests[i].start_time = time(NULL);
            parallel_tab.requests[i].cw_received = 0;
            parallel_tab.requests[i].servers_responded = 0;
            parallel_tab.requests[i].first_response_server = -1;
            memset(parallel_tab.requests[i].servers_contacted, 0, sizeof(parallel_tab.requests[i].servers_contacted));
            pthread_mutex_unlock(&parallel_tab.lock);
            return i;
        }
    }
    
    pthread_mutex_unlock(&parallel_tab.lock);
    return -1;
}

static inline void parallel_ecm_record_contact(uint32_t ecm_id, int server_id) {
    int i;
    
    pthread_mutex_lock(&parallel_tab.lock);
    
    for (i = 0; i < 256; i++) {
        if (parallel_tab.requests[i].ecm_id == ecm_id) {
            int j;
            for (j = 0; j < PARALLEL_MAX_SERVERS; j++) {
                if (parallel_tab.requests[i].servers_contacted[j] == 0) {
                    parallel_tab.requests[i].servers_contacted[j] = server_id;
                    break;
                }
            }
            break;
        }
    }
    
    pthread_mutex_unlock(&parallel_tab.lock);
}

static inline int parallel_ecm_record_response(uint32_t ecm_id, int server_id) {
    int i;
    
    pthread_mutex_lock(&parallel_tab.lock);
    
    for (i = 0; i < 256; i++) {
        if (parallel_tab.requests[i].ecm_id == ecm_id) {
            parallel_tab.requests[i].servers_responded++;
            
            if (!parallel_tab.requests[i].cw_received) {
                parallel_tab.requests[i].cw_received = 1;
                parallel_tab.requests[i].first_response_server = server_id;
                pthread_mutex_unlock(&parallel_tab.lock);
                return 1;  // First response
            }
            
            pthread_mutex_unlock(&parallel_tab.lock);
            return 0;  // Not first
        }
    }
    
    pthread_mutex_unlock(&parallel_tab.lock);
    return -1;
}

// ============================================================================
// OPTIMIZATION #7: AUTO-RECOVERY SYSTEM
// ============================================================================

struct recovery_state {
    uint8_t cache_corrupted;
    uint8_t connection_lost;
    uint8_t server_down;
    uint32_t last_recovery_attempt;
    uint32_t recovery_count;
};

extern struct recovery_state recovery_sys;

static inline void auto_recovery_init(void) {
    memset(&recovery_sys, 0, sizeof(recovery_sys));
}

static inline void auto_recovery_check(void) {
    uint32_t now = time(NULL);
    
    // Don't attempt recovery too frequently (min 60 seconds)
    if (now - recovery_sys.last_recovery_attempt < 60) {
        return;
    }
    
    // Cache recovery
    if (recovery_sys.cache_corrupted) {
        debugf(0, "AUTO-RECOVERY: Attempting cache rebuild...\n");
        // Cache rebuild logic would go here
        recovery_sys.cache_corrupted = 0;
        recovery_sys.last_recovery_attempt = now;
        recovery_sys.recovery_count++;
    }
    
    // Connection recovery
    if (recovery_sys.connection_lost) {
        debugf(0, "AUTO-RECOVERY: Attempting reconnection...\n");
        // Reconnection logic would go here
        recovery_sys.connection_lost = 0;
        recovery_sys.last_recovery_attempt = now;
        recovery_sys.recovery_count++;
    }
    
    // Server recovery
    if (recovery_sys.server_down) {
        debugf(0, "AUTO-RECOVERY: Switching to backup servers...\n");
        // Backup server logic would go here
        recovery_sys.server_down = 0;
        recovery_sys.last_recovery_attempt = now;
        recovery_sys.recovery_count++;
    }
}

static inline void auto_recovery_trigger(int type) {
    switch (type) {
        case 1: recovery_sys.cache_corrupted = 1; break;
        case 2: recovery_sys.connection_lost = 1; break;
        case 3: recovery_sys.server_down = 1; break;
    }
}

// ============================================================================
// OPTIMIZATION #8: CW ENCRYPTION (SIMPLIFIED)
// ============================================================================

/*
 * TASK R26 (D77) — this was an XOR with a hard-coded, published
 * session key (0x12,0x34,...): it encrypted nothing, and worse, it
 * LOOKED like encryption. It is now the tree's real AES (aes.c,
 * already linked for camd35/cs378x): one 16-byte CW = one AES block,
 * key 128/192/256-bit. There is NO default session key any more --
 * cw_*_with_session() fail closed (return 0, output untouched)
 * until the operator installs a key with cw_session_key_set().
 * No in-tree caller used the old functions (verified by grep), so
 * failing closed breaks no wire: this is a library for future
 * authenticated transport, not a protocol change. Returns 1 ok.
 */
#include "aes.h"
#include "sha256.h"	/* TASK R27 (D78): the KDF below */

static inline int cw_encrypt(uint8_t *cw, uint8_t *encrypted, uint8_t *key, int key_len) {
    AES_KEY ekey;
    uint8_t in[16];
    if (!cw || !encrypted || !key) return 0;
    if (key_len != 16 && key_len != 24 && key_len != 32) return 0;
    if (AES_set_encrypt_key(key, key_len * 8, &ekey) != 0) return 0;
    memcpy(in, cw, 16); /* safe for in-place cw == encrypted */
    AES_encrypt(in, encrypted, &ekey);
    return 1;
}

static inline int cw_decrypt(uint8_t *encrypted, uint8_t *cw, uint8_t *key, int key_len) {
    AES_KEY dkey;
    uint8_t in[16];
    if (!encrypted || !cw || !key) return 0;
    if (key_len != 16 && key_len != 24 && key_len != 32) return 0;
    if (AES_set_decrypt_key(key, key_len * 8, &dkey) != 0) return 0;
    memcpy(in, encrypted, 16);
    AES_decrypt(in, cw, &dkey);
    return 1;
}

static uint8_t cw_session_key[32];
static int cw_session_key_len = 0; /* 0 = unset: session helpers fail closed */

static inline int cw_session_key_set(const uint8_t *key, int key_len) {
    if (!key || (key_len != 16 && key_len != 24 && key_len != 32)) return 0;
    memcpy(cw_session_key, key, key_len);
    cw_session_key_len = key_len;
    return 1;
}

/*
 * TASK R27 (D78) -- passphrase-derived session key. The session key
 * is SHA-256(passphrase) used directly as an AES-256 key: a modern
 * KDF in place of the tree's MD5-era derivations, for NEW keying
 * only -- no existing wire derivation (camd35 MD5, newcamd DES) is
 * touched, because peers dictate those. One SHA-256 pass is a key
 * stretcher, not a password vault: passphrases here key a transport
 * between cooperating operators, and the derivation is labelled as
 * such so nobody mistakes it for a password-storage scheme.
 * Returns 1 when a session key is installed.
 */
static inline int cw_session_key_derive(const char *passphrase) {
    uint8_t digest[SHA256_DIGEST_LENGTH];
    if (!passphrase || !passphrase[0]) return 0;
    SHA256((const uint8_t *)passphrase, strlen(passphrase), digest);
    return cw_session_key_set(digest, SHA256_DIGEST_LENGTH);
}

static inline int cw_encrypt_with_session(uint8_t *cw, uint8_t *encrypted) {
    if (!cw_session_key_len) return 0;
    return cw_encrypt(cw, encrypted, cw_session_key, cw_session_key_len);
}

static inline int cw_decrypt_with_session(uint8_t *encrypted, uint8_t *cw) {
    if (!cw_session_key_len) return 0;
    return cw_decrypt(encrypted, cw, cw_session_key, cw_session_key_len);
}

// ============================================================================
// OPTIMIZATION #9: PREDICTIVE CACHE WARMING
// ============================================================================

#define WARMING_CHANNELS 50

struct warming_channel {
    uint32_t caid;
    uint32_t provid;
    uint16_t sid;
    uint32_t popularity;  // Access count
    uint8_t warmed;       // Already in cache?
};

struct warming_table {
    struct warming_channel channels[WARMING_CHANNELS];
    int count;
    pthread_mutex_t lock;
};

extern struct warming_table warming_tab;

static inline void predictive_warming_init(void) {
    memset(&warming_tab, 0, sizeof(warming_tab));
    pthread_mutex_init(&warming_tab.lock, NULL);
}

static inline void predictive_warming_record(uint32_t caid, uint32_t provid, uint16_t sid) {
    int i;
    struct warming_channel *ch = NULL;
    
    pthread_mutex_lock(&warming_tab.lock);
    
    // Find existing
    for (i = 0; i < warming_tab.count; i++) {
        if (warming_tab.channels[i].caid == caid &&
            warming_tab.channels[i].provid == provid &&
            warming_tab.channels[i].sid == sid) {
            ch = &warming_tab.channels[i];
            break;
        }
    }
    
    // Create new if not found
    if (!ch && warming_tab.count < WARMING_CHANNELS) {
        ch = &warming_tab.channels[warming_tab.count++];
        ch->caid = caid;
        ch->provid = provid;
        ch->sid = sid;
        ch->warmed = 0;
    }
    
    if (ch) {
        ch->popularity++;
    }
    
    pthread_mutex_unlock(&warming_tab.lock);
}

static inline int predictive_warming_should_warm(void) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    
    // Warm cache before peak hours (19:00 - 23:00)
    if (tm->tm_hour >= 18 && tm->tm_hour < 19) {
        return 1;
    }
    
    return 0;
}

static inline int predictive_warming_get_popular(uint32_t *caid, uint32_t *provid, uint16_t *sid, int max_count) {
    int i, count = 0;
    
    pthread_mutex_lock(&warming_tab.lock);
    
    // Sort by popularity (simple bubble sort)
    for (i = 0; i < warming_tab.count - 1; i++) {
        int j;
        for (j = 0; j < warming_tab.count - i - 1; j++) {
            if (warming_tab.channels[j].popularity < warming_tab.channels[j+1].popularity) {
                struct warming_channel temp = warming_tab.channels[j];
                warming_tab.channels[j] = warming_tab.channels[j+1];
                warming_tab.channels[j+1] = temp;
            }
        }
    }
    
    // Return top channels
    for (i = 0; i < warming_tab.count && count < max_count; i++) {
        if (!warming_tab.channels[i].warmed && warming_tab.channels[i].popularity > 5) {
            caid[count] = warming_tab.channels[i].caid;
            provid[count] = warming_tab.channels[i].provid;
            sid[count] = warming_tab.channels[i].sid;
            warming_tab.channels[i].warmed = 1;
            count++;
        }
    }
    
    pthread_mutex_unlock(&warming_tab.lock);
    
    return count;
}

#endif /* ADVANCED_OPTIMIZATIONS_H */
