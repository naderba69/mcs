/* biss.h — BISS Key Management System
 *
 * PURPOSE: Store and manage BISS keys for channels
 * Allows adding BISS keys via web interface
 * Keys are used directly for decryption
 *
 * USAGE:
 *   biss_add(caid, sid, provid, key) - Add a BISS key
 *   biss_get(caid, sid, provid, key) - Get BISS key if exists
 *   biss_remove(caid, sid) - Remove a BISS key
 */

#ifndef MCS_BISS_H
#define MCS_BISS_H

#include <stdint.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#define BISS_MAX_KEYS 256
#define BISS_KEY_SIZE 8  /* BISS keys are 8 bytes (64 bits) */

struct biss_key {
	uint16_t caid;
	uint16_t sid;
	uint32_t provid;
	uint8_t key[BISS_KEY_SIZE];
	uint32_t added_time;
	uint32_t last_used;
	uint32_t use_count;
	int active;
};

struct biss_table {
	struct biss_key keys[BISS_MAX_KEYS];
	int count;
	pthread_mutex_t lock;
};

/* Global BISS table */
extern struct biss_table biss_keys;

/* Initialize BISS system */
static inline void biss_init(void)
{
	memset(&biss_keys, 0, sizeof(biss_keys));
	pthread_mutex_init(&biss_keys.lock, NULL);
}

/* Add or update a BISS key
 * Returns: 1 on success, 0 on failure
 */
static inline int biss_add(uint16_t caid, uint16_t sid, uint32_t provid, const uint8_t *key)
{
	int i;
	struct biss_key *slot = NULL;
	
	if (!key) return 0;
	
	pthread_mutex_lock(&biss_keys.lock);
	
	/* Check if key already exists */
	for (i = 0; i < biss_keys.count; i++) {
		if (biss_keys.keys[i].caid == caid && 
		    biss_keys.keys[i].sid == sid &&
		    biss_keys.keys[i].provid == provid) {
			slot = &biss_keys.keys[i];
			break;
		}
	}
	
	/* If not found, allocate new slot */
	if (!slot && biss_keys.count < BISS_MAX_KEYS) {
		slot = &biss_keys.keys[biss_keys.count++];
	}
	
	if (slot) {
		slot->caid = caid;
		slot->sid = sid;
		slot->provid = provid;
		memcpy(slot->key, key, BISS_KEY_SIZE);
		slot->added_time = time(NULL);
		slot->last_used = 0;
		slot->use_count = 0;
		slot->active = 1;
		pthread_mutex_unlock(&biss_keys.lock);
		return 1;
	}
	
	pthread_mutex_unlock(&biss_keys.lock);
	return 0;
}

/* Get BISS key for a channel
 * Returns: 1 if found (key is copied to output), 0 if not found
 */
static inline int biss_get(uint16_t caid, uint16_t sid, uint32_t provid, uint8_t *key)
{
	int i;
	
	if (!key) return 0;
	
	pthread_mutex_lock(&biss_keys.lock);
	
	for (i = 0; i < biss_keys.count; i++) {
		if (biss_keys.keys[i].active &&
		    biss_keys.keys[i].caid == caid &&
		    biss_keys.keys[i].sid == sid &&
		    biss_keys.keys[i].provid == provid) {
			memcpy(key, biss_keys.keys[i].key, BISS_KEY_SIZE);
			biss_keys.keys[i].last_used = time(NULL);
			biss_keys.keys[i].use_count++;
			pthread_mutex_unlock(&biss_keys.lock);
			return 1;
		}
	}
	
	pthread_mutex_unlock(&biss_keys.lock);
	return 0;
}

/* Remove a BISS key
 * Returns: 1 if removed, 0 if not found
 */
static inline int biss_remove(uint16_t caid, uint16_t sid, uint32_t provid)
{
	int i;
	
	pthread_mutex_lock(&biss_keys.lock);
	
	for (i = 0; i < biss_keys.count; i++) {
		if (biss_keys.keys[i].caid == caid &&
		    biss_keys.keys[i].sid == sid &&
		    biss_keys.keys[i].provid == provid) {
			biss_keys.keys[i].active = 0;
			pthread_mutex_unlock(&biss_keys.lock);
			return 1;
		}
	}
	
	pthread_mutex_unlock(&biss_keys.lock);
	return 0;
}

/* Check if channel has BISS key
 * Returns: 1 if exists, 0 if not
 */
static inline int biss_has_key(uint16_t caid, uint16_t sid, uint32_t provid)
{
	int i;
	
	pthread_mutex_lock(&biss_keys.lock);
	
	for (i = 0; i < biss_keys.count; i++) {
		if (biss_keys.keys[i].active &&
		    biss_keys.keys[i].caid == caid &&
		    biss_keys.keys[i].sid == sid &&
		    biss_keys.keys[i].provid == provid) {
			pthread_mutex_unlock(&biss_keys.lock);
			return 1;
		}
	}
	
	pthread_mutex_unlock(&biss_keys.lock);
	return 0;
}

/* ==================================================================== */
/* TASK R26 (D77) — BISS2: 128-bit AES session word (CAID 2610).
 *
 * BISS1 above stores an 8-byte constant key and th-ecm.c repeats it
 * into both CW halves. BISS2 is a different object: the session word
 * is the full 16 bytes, it is NOT a repeated half and it carries no
 * DVB-CSA checksum bytes (casystem.h: CA_CW_AES). The tables are kept
 * separate so a BISS1 lookup can never return half a BISS2 word and
 * vice versa. Entries are added from the /biss page with a 32-hex
 * key; th-ecm.c consults this table only for CAID 2610.
 */
#define BISS2_MAX_KEYS 128
#define BISS2_KEY_SIZE 16
#define BISS2_CAID 0x2610

struct biss2_key {
	uint16_t caid;
	uint16_t sid;
	uint32_t provid;
	uint8_t key[BISS2_KEY_SIZE];
	uint32_t added_time;
	uint32_t last_used;
	uint32_t use_count;
	int active;
};

struct biss2_table {
	struct biss2_key keys[BISS2_MAX_KEYS];
	int count;
	pthread_mutex_t lock;
};

extern struct biss2_table biss2_keys;

static inline void biss2_init(void)
{
	memset(&biss2_keys, 0, sizeof(biss2_keys));
	pthread_mutex_init(&biss2_keys.lock, NULL);
}

static inline int biss2_add(uint16_t caid, uint16_t sid, uint32_t provid, const uint8_t *key)
{
	int i;
	struct biss2_key *slot = NULL;
	if (!key) return 0;
	pthread_mutex_lock(&biss2_keys.lock);
	for (i = 0; i < biss2_keys.count; i++) {
		if (biss2_keys.keys[i].caid == caid &&
		    biss2_keys.keys[i].sid == sid &&
		    biss2_keys.keys[i].provid == provid) {
			slot = &biss2_keys.keys[i];
			break;
		}
	}
	if (!slot && biss2_keys.count < BISS2_MAX_KEYS) {
		slot = &biss2_keys.keys[biss2_keys.count++];
	}
	if (slot) {
		slot->caid = caid;
		slot->sid = sid;
		slot->provid = provid;
		memcpy(slot->key, key, BISS2_KEY_SIZE);
		slot->added_time = (uint32_t)time(NULL);
		slot->last_used = 0;
		slot->use_count = 0;
		slot->active = 1;
		pthread_mutex_unlock(&biss2_keys.lock);
		return 1;
	}
	pthread_mutex_unlock(&biss2_keys.lock);
	return 0;
}

static inline int biss2_get(uint16_t caid, uint16_t sid, uint32_t provid, uint8_t *key)
{
	int i;
	if (!key) return 0;
	pthread_mutex_lock(&biss2_keys.lock);
	for (i = 0; i < biss2_keys.count; i++) {
		if (biss2_keys.keys[i].active &&
		    biss2_keys.keys[i].caid == caid &&
		    biss2_keys.keys[i].sid == sid &&
		    biss2_keys.keys[i].provid == provid) {
			memcpy(key, biss2_keys.keys[i].key, BISS2_KEY_SIZE);
			biss2_keys.keys[i].last_used = (uint32_t)time(NULL);
			biss2_keys.keys[i].use_count++;
			pthread_mutex_unlock(&biss2_keys.lock);
			return 1;
		}
	}
	pthread_mutex_unlock(&biss2_keys.lock);
	return 0;
}

static inline int biss2_remove(uint16_t caid, uint16_t sid, uint32_t provid)
{
	int i;
	pthread_mutex_lock(&biss2_keys.lock);
	for (i = 0; i < biss2_keys.count; i++) {
		if (biss2_keys.keys[i].caid == caid &&
		    biss2_keys.keys[i].sid == sid &&
		    biss2_keys.keys[i].provid == provid) {
			biss2_keys.keys[i].active = 0;
			pthread_mutex_unlock(&biss2_keys.lock);
			return 1;
		}
	}
	pthread_mutex_unlock(&biss2_keys.lock);
	return 0;
}

static inline int biss2_count(void)
{
	int i, count = 0;
	pthread_mutex_lock(&biss2_keys.lock);
	for (i = 0; i < biss2_keys.count; i++)
		if (biss2_keys.keys[i].active) count++;
	pthread_mutex_unlock(&biss2_keys.lock);
	return count;
}

/* Get count of active BISS keys */
static inline int biss_count(void)
{
	int i, count = 0;
	
	pthread_mutex_lock(&biss_keys.lock);
	for (i = 0; i < biss_keys.count; i++) {
		if (biss_keys.keys[i].active) count++;
	}
	pthread_mutex_unlock(&biss_keys.lock);
	
	return count;
}

#endif /* MCS_BISS_H */
