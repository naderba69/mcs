#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../src/cs378x-frame.h"

static int failures;

#define CHECK(condition, description) do { \
    if (condition) printf("PASS: %s\n", description); \
    else { printf("FAIL: %s\n", description); failures++; } \
} while (0)

int main(void)
{
    uint8_t frame[2048];
    int length;

    memset(frame, 0, sizeof(frame));
    frame[4] = 0x37; /* CAMD_KEEPALIVE */
    frame[5] = 1;
    CHECK(cs378x_frame_length(frame, 36, sizeof(frame), 0) == 36,
          "ordinary keepalive frame length is decoded without changing its layout");

    memset(frame, 0, sizeof(frame));
    frame[4] = 0; /* ECM request; section length 13 means 16 ECM bytes. */
    frame[25] = 0;
    frame[26] = 13;
    CHECK(cs378x_frame_length(frame, 36, sizeof(frame), 0) == 52,
          "ECM length field is decoded only after its header bytes are present");
    CHECK(cs378x_frame_length(frame, 26, sizeof(frame), 0) < 0,
          "truncated ECM header is rejected before reading its length field");

    memset(frame, 0, sizeof(frame));
    frame[4] = 0x3D; /* CacheEx ID request, 16-bit little-endian length 12. */
    frame[5] = 12;
    CHECK(cs378x_frame_length(frame, 36, sizeof(frame), 0) == 36,
          "CacheEx 16-bit length preserves the existing padded frame size");

    memset(frame, 0, sizeof(frame));
    frame[4] = 0x80;
    frame[5] = 44;
    frame[6] = 1; /* 300 bytes in the extended CS378X server mode. */
    CHECK(cs378x_frame_length(frame, 36, sizeof(frame), 1) == 324,
          "extended 0x80-family length is enabled only for the matching receive path");
    CHECK(cs378x_frame_length(frame, 36, sizeof(frame), 0) == 68,
          "other receive paths retain their original one-byte length interpretation");

    memset(frame, 0, sizeof(frame));
    frame[4] = 0x3F; /* CacheEx push. */
    frame[5] = 52;
    length = cs378x_frame_length(frame, 36, sizeof(frame), 0);
    CHECK(length == 84 && length >= 69,
          "CacheEx push frame has room for CW, hop count, and trailing node ID");

    memset(frame, 0, sizeof(frame));
    frame[4] = 0x3F;
    frame[5] = 0xFF;
    frame[6] = 0xFF;
    CHECK(cs378x_frame_length(frame, 36, sizeof(frame), 0) < 0,
          "oversized peer-declared frame is rejected before reading into the buffer");

    memset(frame, 0, sizeof(frame));
    frame[4] = 0x3F;
    frame[5] = 0xE8;
    frame[6] = 3; /* 1000-byte payload cannot fit after block padding in 1024. */
    CHECK(cs378x_frame_length(frame, 36, 1024, 0) < 0,
          "padded frame size is bounded by the caller's actual buffer capacity");

    memset(frame, 0, sizeof(frame));
    frame[4] = 0;
    frame[25] = 0x0F;
    frame[26] = 0xFF;
    CHECK(cs378x_frame_length(frame, 36, sizeof(frame), 0) < 0,
          "maximum ECM section length cannot overflow the fixed frame buffer");

    printf("%d CS378X frame checks failed\n", failures);
    return failures != 0;
}
