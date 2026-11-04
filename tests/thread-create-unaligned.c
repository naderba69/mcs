#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "threads.h"

static int worker_finished;

void debugf(uint32_t flag, char *format, ...)
{
	(void)flag;
	(void)format;
}

static void *finish_worker(void *arg)
{
	(void)arg;
	__atomic_store_n(&worker_finished, 1, __ATOMIC_RELEASE);
	return NULL;
}

int main(void)
{
	unsigned char storage[sizeof(pthread_t) + 64];
	uintptr_t alignment = __alignof__(pthread_t);
	uintptr_t base = ((uintptr_t)storage + alignment - 1) & ~(alignment - 1);
	pthread_t *unaligned_tid = (pthread_t *)(void *)(base + 1);
	struct timespec pause = { 0, 1000000 };
	int i;

	if (((uintptr_t)unaligned_tid % alignment) == 0) {
		fprintf(stderr, "fixture did not produce an unaligned pthread_t slot\n");
		return 1;
	}
	if (!create_thread(unaligned_tid, finish_worker, NULL)) {
		fprintf(stderr, "create_thread failed\n");
		return 1;
	}
	for (i = 0; i < 1000; i++) {
		if (__atomic_load_n(&worker_finished, __ATOMIC_ACQUIRE)) {
			puts("PASS: create_thread accepts an unaligned packed thread-ID slot");
			return 0;
		}
		nanosleep(&pause, NULL);
	}
	fprintf(stderr, "worker did not finish\n");
	return 1;
}
