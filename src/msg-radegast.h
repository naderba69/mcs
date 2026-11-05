#ifndef MCS_MSG_RADEGAST_H
#define MCS_MSG_RADEGAST_H

#include <stddef.h>
#include <stdint.h>

int rdgd_message_receive(int sock, unsigned char *buffer, int timeout);
int rdgd_message_send(int sock, unsigned char *buf, int len);
int rdgd_check_message(int sock);

/* A type-0x02 Radegast response has three header bytes before the CW marker.
 * A successful response is the exact 20-byte frame: type, 18-byte payload,
 * CW tag/length marker, then sixteen CW bytes. */
static inline int rdgd_response_header_valid(const uint8_t *frame, size_t length)
{
    return frame && length >= 1u && (frame[0] != 0x02 || length >= 4u);
}

static inline int rdgd_response_has_complete_cw(const uint8_t *frame,
                                                 size_t length)
{
    return rdgd_response_header_valid(frame, length) && length == 20u &&
           frame[0] == 0x02 && frame[1] == 0x12 &&
           frame[2] == 0x05 && frame[3] == 0x10;
}

#endif /* MCS_MSG_RADEGAST_H */
