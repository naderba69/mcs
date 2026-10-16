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
