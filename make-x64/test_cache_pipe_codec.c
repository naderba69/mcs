#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../src/cache-pipe-codec.h"

static int failures;

#define CHECK(condition, description) do { \
    if (condition) printf("PASS: %s\n", description); \
    else { printf("FAIL: %s\n", description); failures++; } \
} while (0)

int main(void)
{
    long page_size = sysconf(_SC_PAGESIZE);
    uint8_t out[16];
    uint8_t full_frame[160];
    uint8_t *mapping;
    uint8_t *frame;
    size_t base_len = 16u + sizeof(void *) + sizeof(uint64_t) + 16u;
    int i, copied;

    CHECK(cacheex_pipe_frame_length_valid(PIPE_CACHEEX_PUSH_LOCAL, 45u),
          "CACHEEX local push accepts its fixed 45-byte payload");
    CHECK(!cacheex_pipe_frame_length_valid(PIPE_CACHEEX_PUSH_LOCAL, 44u) &&
          !cacheex_pipe_frame_length_valid(PIPE_CACHEEX_PUSH_LOCAL, 46u),
          "CACHEEX local push rejects truncated and overlong payloads");
    CHECK(cacheex_pipe_frame_length_valid(PIPE_CACHEEX_PUSH_REMOTE, 53u) &&
          !cacheex_pipe_frame_length_valid(PIPE_CACHEEX_PUSH_REMOTE, 52u),
          "CACHEEX remote push requires exactly 53 bytes");
    CHECK(cacheex_pipe_frame_length_valid(PIPE_CACHEEX_PUSH_IN, 48u) &&
          !cacheex_pipe_frame_length_valid(PIPE_CACHEEX_PUSH_IN, 47u),
          "CACHEEX inbound push requires exactly 48 bytes");
    CHECK(cacheex_pipe_frame_length_valid(PIPE_WAKEUP, 2u) &&
          !cacheex_pipe_frame_length_valid(PIPE_WAKEUP, 1u),
          "CACHEEX wakeup uses the two-byte pipe command frame");

    if (page_size <= 0 || base_len >= (size_t)page_size) {
        fprintf(stderr, "invalid page size for cache-pipe frame test\n");
        return 1;
    }
    mapping = (uint8_t *)mmap(NULL, (size_t)page_size * 2u,
                              PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    if (mprotect(mapping + page_size, (size_t)page_size, PROT_NONE) != 0) {
        perror("mprotect");
        munmap(mapping, (size_t)page_size * 2u);
        return 1;
    }

    /* Put the short frame immediately before an inaccessible page. */
    frame = mapping + page_size - base_len;
    memset(frame, 0x5a, base_len);
    memset(out, 0xa5, sizeof(out));
    copied = cache_pipe_copy_optional_cw(frame, base_len, base_len, out);
    CHECK(!copied, "short PIPE_CACHE_FIND frame reports no optional prevcw");
    CHECK(out[0] == 0 && out[15] == 0,
          "absent prevcw is cleared without reading beyond the frame boundary");

    memset(full_frame, 0, sizeof(full_frame));
    for (i = 0; i < 16; i++) full_frame[base_len + (size_t)i] = (uint8_t)(i + 1);
    memset(out, 0, sizeof(out));
    copied = cache_pipe_copy_optional_cw(full_frame, base_len + 16u,
                                         base_len, out);
    CHECK(copied && out[0] == 1 && out[15] == 16,
          "complete optional prevcw suffix is copied exactly");
    CHECK(!cache_pipe_copy_optional_cw(full_frame, base_len,
                                       base_len + 1u, out),
          "offset beyond payload is rejected safely");

    munmap(mapping, (size_t)page_size * 2u);
    printf("%d cache-pipe codec checks failed\n", failures);
    return failures ? 1 : 0;
}
