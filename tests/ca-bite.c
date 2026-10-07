/* TASK 3.4 bite harness -- refuse exactly the cache-table allocation.
 *
 * The defect: getcachetabbycaid() (clustredcache.c) mallocs a cache_list node
 * and a 1024-slot cache_data table with no NULL checks, and every caller
 * dereferences the result. One failed allocation is a SIGSEGV on the cache
 * thread, which is the process.
 *
 * The table allocation is malloc(sizeof(struct cache_data)*1024) with
 * sizeof(struct cache_data)=88 in this build (packed; CACHEEX on; cwcycle_t is
 * an enum = 4 bytes), so 88*1024 = 90112. GCC compiles the malloc+memset pair
 * into calloc(90112, 1) -- confirmed in the disassembly of the shipped binary
 * -- which is why this file interposes calloc and matches the TOTAL size.
 * 90112 is requested nowhere else in the tree, so every other allocation
 * passes through untouched and the bite proves the cache path specifically,
 * not general OOM behaviour.
 *
 * LD_PRELOAD=.ca-bite.so <mcs> -C <cfg>
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

extern void *__libc_malloc(size_t size);

#define CACHE_TABLE_SIZE 90112   /* 88 * 1024, see comment above */

static unsigned long ca_refused = 0;
static unsigned long ca_passed  = 0;

void *malloc(size_t size)
{
	if (size == CACHE_TABLE_SIZE) {
		ca_refused++;
		fprintf(stderr, "[ca-bite] refused malloc(%d) #%lu\n",
		        (int)size, ca_refused);
		fflush(stderr);
		return NULL;
	}
	ca_passed++;
	return __libc_malloc(size);
}

void *calloc(size_t n, size_t size)
{
	/* glibc's calloc does not reach interposed malloc on all versions; make
	 * the same promise here so a table-sized calloc would bite too. */
	size_t total = n * size;
	if (total == CACHE_TABLE_SIZE) {
		ca_refused++;
		fprintf(stderr, "[ca-bite] refused calloc(%d) #%lu\n",
		        (int)total, ca_refused);
		fflush(stderr);
		return NULL;
	}
	void *p = __libc_malloc(total);
	if (p) memset(p, 0, total);
	return p;
}
