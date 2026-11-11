#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../src/cccam-session.h"

#define THREAD_COUNT 8
#define GENERATIONS_PER_THREAD 20000
#define GENERATION_COUNT (THREAD_COUNT * GENERATIONS_PER_THREAD)

static uint32_t generation_counter;
static uint32_t generations[GENERATION_COUNT];

struct worker_args {
	int index;
};

static int failures;

#define CHECK(condition, label) do { \
	if (condition) printf("[ ok ] %s\n", label); \
	else { printf("[FAIL] %s\n", label); failures++; } \
} while (0)

static void *generation_worker(void *arg)
{
	struct worker_args *worker = (struct worker_args *)arg;
	int start = worker->index * GENERATIONS_PER_THREAD;
	int i;
	for (i = 0; i < GENERATIONS_PER_THREAD; i++)
		generations[start + i] =
			cccam_session_generation_next(&generation_counter);
	return NULL;
}

static int compare_u32(const void *left, const void *right)
{
	uint32_t a = *(const uint32_t *)left;
	uint32_t b = *(const uint32_t *)right;
	return (a > b) - (a < b);
}

int main(void)
{
	pthread_t threads[THREAD_COUNT];
	struct worker_args args[THREAD_COUNT];
	uint64_t token;
	uint32_t client_id = 0, generation = 0;
	int i, created = 0;

	generation_counter = UINT32_MAX - 1;
	CHECK(cccam_session_generation_next(&generation_counter) == UINT32_MAX,
	      "generation counter returns UINT32_MAX before wrap");
	CHECK(cccam_session_generation_next(&generation_counter) == 1,
	      "generation counter skips reserved zero at wrap");
	CHECK(cccam_session_generation_next(&generation_counter) == 2,
	      "generation counter continues after wrap");

	CHECK(cccam_session_matches(41, 1, 7, 41, 7),
	      "poll snapshot matches its active socket and generation");
	CHECK(cccam_session_matches(41, 1, 7, 0, 7),
	      "epoll snapshot matches by generation when fd is carried by epoll");
	CHECK(!cccam_session_matches(41, 1, 8, 41, 7),
	      "stale generation cannot match a reused descriptor");
	CHECK(!cccam_session_matches(41, 0, 7, 41, 7),
	      "disconnected session rejects a queued event");
	CHECK(!cccam_session_matches(42, 1, 7, 41, 7),
	      "poll snapshot rejects a different current socket");
	CHECK(!cccam_session_matches(41, 1, 7, 41, 0),
	      "zero generation is never a valid session");

	token = cccam_session_epoll_token(0x89abcdefU, 0x12345678U);
	CHECK(token != 0 && cccam_session_epoll_token_decode(token, &client_id,
	                                                     &generation) &&
	      client_id == 0x89abcdefU && generation == 0x12345678U,
	      "epoll token round-trips client identity and session generation");
	CHECK(cccam_session_epoll_token(0, 1) == 0 &&
	      cccam_session_epoll_token(1, 0) == 0 &&
	      !cccam_session_epoll_token_decode(0, &client_id, &generation),
	      "epoll control token remains reserved and invalid identities are rejected");

	generation_counter = UINT32_MAX - 4;
	for (i = 0; i < THREAD_COUNT; i++) {
		args[i].index = i;
		if (pthread_create(&threads[i], NULL, generation_worker, &args[i]) != 0) {
			printf("[FAIL] pthread_create worker %d\n", i);
			failures++;
			break;
		}
		created++;
	}
	for (i = 0; i < created; i++) pthread_join(threads[i], NULL);
	if (created == THREAD_COUNT) {
		qsort(generations, GENERATION_COUNT, sizeof(generations[0]), compare_u32);
		for (i = 0; i < GENERATION_COUNT; i++) {
			if (!generations[i] ||
			    (i && generations[i] == generations[i - 1])) break;
		}
		CHECK(i == GENERATION_COUNT,
		      "concurrent generation allocation remains nonzero and unique across wrap");
	} else {
		CHECK(0, "concurrent generation allocation workers started");
	}

	if (failures) {
		printf("FAIL: %d CCcam session regression(s)\n", failures);
		return 1;
	}
	printf("PASS: CCcam session generation, poll/epoll identity, stale-event and wrap checks\n");
	return 0;
}
