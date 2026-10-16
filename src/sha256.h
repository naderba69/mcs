/* sha256.h -- TASK R27 (D78): SHA-256 (FIPS 180-4).
 *
 * WHY. The tree's hash inventory stopped at MD5 (ecmd5 identity,
 * camd35 key derivation -- both dictated by peer wire formats and
 * kept for compatibility) and SHA-1 (cccam). Any NEW construction
 * in this tree must not be built on either. This is the modern
 * primitive the R26 AES CW transport derives its session keys with
 * (cw_session_key_derive), and the one future negotiated protocol
 * work should reach for. API mirrors sha1.h so call sites read the
 * same. Implementation is written from the FIPS 180-4 spec and is
 * pinned by NIST test vectors in make-x64/test_sha256.c.
 */
#ifndef MCS_SHA256_H
#define MCS_SHA256_H

#include <sys/types.h>
#include <stdint.h>
#include <string.h>

#define SHA256_DIGEST_LENGTH 32
#define SHA256_BLOCK_LENGTH  64

typedef struct {
	uint32_t state[8];
	uint64_t bitlen;
	uint8_t  data[SHA256_BLOCK_LENGTH];
	uint32_t datalen;
} SHA256_CTX;

void SHA256_Init(SHA256_CTX *ctx);
void SHA256_Update(SHA256_CTX *ctx, const uint8_t *data, size_t len);
void SHA256_Final(SHA256_CTX *ctx, uint8_t digest[SHA256_DIGEST_LENGTH]);

/* One-shot convenience. */
static inline void SHA256(const uint8_t *data, size_t len,
			  uint8_t digest[SHA256_DIGEST_LENGTH])
{
	SHA256_CTX ctx;
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, data, len);
	SHA256_Final(&ctx, digest);
}

#endif /* MCS_SHA256_H */
