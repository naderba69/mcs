#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../src/msg-radegast.h"

static int failures;
#define CHECK(condition, description) do { \
    if (condition) printf("PASS: %s\n", description); \
    else { printf("FAIL: %s\n", description); failures++; } \
} while (0)

int main(void)
{
    uint8_t frame[21];
    memset(frame, 0, sizeof(frame));
    frame[0] = 0x02;
    frame[1] = 0x12;
    frame[2] = 0x05;
    frame[3] = 0x10;

    CHECK(!rdgd_response_header_valid(frame, 3u),
          "short type-0x02 response header is rejected before field access");
    frame[0] = 0x01;
    CHECK(rdgd_response_header_valid(frame, 1u),
          "one-byte non-CW status frame remains valid");
    frame[0] = 0x02;
    CHECK(rdgd_response_header_valid(frame, 4u) &&
          !rdgd_response_has_complete_cw(frame, 4u),
          "non-CW type-0x02 response may be handled without reading a CW");
    CHECK(rdgd_response_has_complete_cw(frame, 20u),
          "complete 20-byte Radegast CW response is accepted");
    CHECK(!rdgd_response_has_complete_cw(frame, 19u) &&
          !rdgd_response_has_complete_cw(frame, 21u),
          "truncated and overlong Radegast CW responses are rejected");
    frame[3] = 0x11;
    CHECK(!rdgd_response_has_complete_cw(frame, 20u),
          "Radegast CW response requires the existing 0x10 marker");

    printf("%d Radegast frame checks failed\n", failures);
    return failures ? 1 : 0;
}
