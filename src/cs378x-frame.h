#ifndef MCS_CS378X_FRAME_H
#define MCS_CS378X_FRAME_H

#include <stdint.h>

/* Return the complete CS378X frame size represented by a decrypted header,
 * or -1 if the header is too short or its declared payload cannot fit in the
 * caller's buffer. The initial read contains at least the 20-byte protocol
 * header plus the 4-byte UCRC. */
static inline int cs378x_frame_length(const uint8_t *buf, int received,
                                      int capacity, int extended_80_length)
{
    int datalen;
    int payload_len;
    int padded_len;

    if (!buf || received < 24 || capacity < 24 || received > capacity)
        return -1;

    if (buf[4] == 0) {
        if (received < 27) return -1;
        datalen = (((buf[25] & 0x0f) << 8) | buf[26]) + 3;
    } else if ((buf[4] & 0xFC) == 0x3C ||
               (extended_80_length && (buf[4] & 0xFE) == 0x80)) {
        if (received < 7) return -1;
        datalen = buf[5] | (buf[6] << 8);
    } else {
        datalen = buf[5];
    }

    if (datalen < 0 || datalen > capacity - 24) return -1;
    payload_len = 20 + datalen;
    padded_len = (payload_len + 15) & ~15;
    if (padded_len < payload_len || padded_len > capacity - 4) return -1;
    return 4 + padded_len;
}

#endif /* MCS_CS378X_FRAME_H */
