/* ecmcompress.h — ECM Compression (R22 candidate #24)
 *
 * PURPOSE: compress ECM packets before sending to servers to reduce
 * bandwidth usage. ECM packets have a lot of redundancy (CAID, PROVID
 * repeated, fixed headers), so compression can save 30-50% bandwidth.
 *
 * MECHANISM: simple RLE (Run-Length Encoding) for repeated bytes,
 * plus header compression (remove fixed fields, send only variable
 * parts).
 *
 * CONFIG: ECM COMPRESS: ON (default OFF = no compression).
 *
 * ROLLBACK: remove ecmcompress_compress() calls; compression is inert.
 */
#ifndef MCS_ECMCOMPRESS_H
#define MCS_ECMCOMPRESS_H

#include <stdint.h>
#include <string.h>

#define ECMCOMPRESS_MAX_SIZE 512
#define ECMCOMPRESS_HEADER_SIZE 8  /* CAID(2) + PROVID(4) + SID(2) */

struct ecmcompress_stats {
    uint32_t total_compressed;
    uint32_t total_decompressed;
    uint32_t bytes_saved;
    uint32_t compression_failures;
};

/* Compress an ECM packet.
 * in: raw ECM (typically 50-200 bytes)
 * in_len: length of raw ECM
 * out: compressed output buffer
 * out_len: receives length of compressed output
 * caid, provid, sid: extracted from ECM header
 *
 * Returns: 1 if compression successful, 0 if failed (out = copy of in).
 */
static inline int ecmcompress_compress(const uint8_t *in, int in_len,
                                        uint8_t *out, int *out_len,
                                        uint16_t caid, uint32_t provid,
                                        uint16_t sid)
{
    int i, j = 0;
    int run_start, run_len;
    uint8_t run_byte;

    if (!in || !out || !out_len || in_len <= 0 || in_len > ECMCOMPRESS_MAX_SIZE) {
        return 0;
    }

    /* Header: CAID(2) + PROVID(4) + SID(2) */
    if (j + ECMCOMPRESS_HEADER_SIZE > ECMCOMPRESS_MAX_SIZE) return 0;
    out[j++] = (uint8_t)(caid >> 8);
    out[j++] = (uint8_t)(caid & 0xFF);
    out[j++] = (uint8_t)(provid >> 24);
    out[j++] = (uint8_t)((provid >> 16) & 0xFF);
    out[j++] = (uint8_t)((provid >> 8) & 0xFF);
    out[j++] = (uint8_t)(provid & 0xFF);
    out[j++] = (uint8_t)(sid >> 8);
    out[j++] = (uint8_t)(sid & 0xFF);

    /* Compress body with RLE */
    i = ECMCOMPRESS_HEADER_SIZE;  /* skip header in input */
    while (i < in_len) {
        run_byte = in[i];
        run_start = i;
        run_len = 1;

        /* Count consecutive identical bytes */
        while (i + run_len < in_len && in[i + run_len] == run_byte && run_len < 255) {
            run_len++;
        }

        if (run_len >= 4) {
            /* RLE: 0x00 + count + byte */
            if (j + 3 > ECMCOMPRESS_MAX_SIZE) {
                /* Compression would expand — abort */
                memcpy(out, in, in_len);
                *out_len = in_len;
                return 0;
            }
            out[j++] = 0x00;
            out[j++] = (uint8_t)run_len;
            out[j++] = run_byte;
            i += run_len;
        } else {
            /* Literal bytes */
            if (run_byte == 0x00) {
                /* Escape: 0x00 must be encoded as 0x00 0x01 0x00 */
                if (j + 3 > ECMCOMPRESS_MAX_SIZE) {
                    memcpy(out, in, in_len);
                    *out_len = in_len;
                    return 0;
                }
                out[j++] = 0x00;
                out[j++] = 0x01;
                out[j++] = 0x00;
                i++;
            } else {
                if (j + 1 > ECMCOMPRESS_MAX_SIZE) {
                    memcpy(out, in, in_len);
                    *out_len = in_len;
                    return 0;
                }
                out[j++] = run_byte;
                i++;
            }
        }
    }

    *out_len = j;
    return (j < in_len) ? 1 : 0;  /* only "successful" if actually smaller */
}

/* Decompress an ECM packet.
 * in: compressed ECM
 * in_len: length of compressed ECM
 * out: decompressed output buffer
 * out_len: receives length of decompressed output
 *
 * Returns: 1 if decompression successful, 0 if failed.
 */
static inline int ecmcompress_decompress(const uint8_t *in, int in_len,
                                          uint8_t *out, int *out_len)
{
    int i, j = 0;
    int run_len;
    uint8_t run_byte;

    if (!in || !out || !out_len || in_len < ECMCOMPRESS_HEADER_SIZE) {
        return 0;
    }

    /* Copy header */
    memcpy(out, in, ECMCOMPRESS_HEADER_SIZE);
    j = ECMCOMPRESS_HEADER_SIZE;
    i = ECMCOMPRESS_HEADER_SIZE;

    /* Decompress body */
    while (i < in_len) {
        if (in[i] == 0x00) {
            /* RLE or escape */
            if (i + 2 >= in_len) return 0;  /* malformed */
            run_len = in[i + 1];
            run_byte = in[i + 2];
            i += 3;

            if (j + run_len > ECMCOMPRESS_MAX_SIZE) return 0;  /* overflow */
            memset(out + j, run_byte, run_len);
            j += run_len;
        } else {
            /* Literal byte */
            if (j >= ECMCOMPRESS_MAX_SIZE) return 0;
            out[j++] = in[i++];
        }
    }

    *out_len = j;
    return 1;
}

/* Compute compression ratio (compressed_size / original_size) */
static inline double ecmcompress_ratio(int original_size, int compressed_size)
{
    if (original_size <= 0) return 1.0;
    return (double)compressed_size / (double)original_size;
}

/* Update compression statistics */
static inline void ecmcompress_stats_update(struct ecmcompress_stats *stats,
                                             int original_size,
                                             int compressed_size,
                                             int success)
{
    if (!stats) return;
    if (success) {
        stats->total_compressed++;
        if (compressed_size < original_size) {
            stats->bytes_saved += (uint32_t)(original_size - compressed_size);
        }
    } else {
        stats->compression_failures++;
    }
}

static inline void ecmcompress_stats_decompressed(struct ecmcompress_stats *stats)
{
    if (stats) stats->total_decompressed++;
}

#endif /* MCS_ECMCOMPRESS_H */
