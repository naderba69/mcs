/* test_sha256.c -- TASK R27 (D78): SHA-256 (FIPS 180-4) NIST vectors
 * plus the AES session-key derivation built on it (advanced-
 * optimizations.h). Links the REAL ../src/sha256.c and ../src/aes.c.
 * Build is wired in make-x64/Makefile (x64/test_sha256).
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* standalone link stub: advanced header's recovery section logs */
void debugf(unsigned int flag, char *format, ...);
void debugf(unsigned int flag, char *format, ...) { (void)flag; (void)format; }

#include "../src/sha256.h"
#include "../src/advanced-optimizations.h"

/* advanced-optimizations.c owns these in the server; the test
 * supplies its own instances so no server object is needed. */
struct parallel_ecm_table parallel_tab;
struct recovery_state recovery_sys;
struct warming_table warming_tab;

static int fail = 0, total = 0;

#define CHECK(cond, name) do { \
	total++; \
	if (cond) printf("  ok  %s\n", name); \
	else { fail++; printf("  FAIL %s\n", name); } \
} while (0)

static int hex_eq(const uint8_t *digest, const char *want_hex)
{
	char got[65];
	int i;
	for (i = 0; i < 32; i++) sprintf(got + i * 2, "%02x", digest[i]);
	got[64] = 0;
	return strcmp(got, want_hex) == 0;
}

static void one_shot(const char *msg, const char *want, const char *name)
{
	uint8_t d[SHA256_DIGEST_LENGTH];
	SHA256((const uint8_t *)msg, strlen(msg), d);
	CHECK(hex_eq(d, want), name);
}

int main(void)
{
	uint8_t d[SHA256_DIGEST_LENGTH];
	SHA256_CTX ctx;
	static const char *msg56 =
		"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
	static const uint8_t cw[16] = {
		0x10,0x32,0x54,0x76, 0x98,0xBA,0xDC,0xFE,
		0x11,0x33,0x55,0x77, 0x99,0xBB,0xDD,0xF0
	};
	uint8_t enc[16], dec[16], enc2[16];

	printf("[test_sha256] TASK R27 (D78) SHA-256 vectors + AES KDF\n");

	/* FIPS 180-4 / NIST example vectors */
	one_shot("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
		 "SHA256(\"\")");
	one_shot("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
		 "SHA256(\"abc\")");
	one_shot(msg56, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
		 "SHA256(56-byte, two-block padding)");

	/* streaming in odd chunks must equal the one-shot */
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, (const uint8_t *)msg56, 7);
	SHA256_Update(&ctx, (const uint8_t *)msg56 + 7, 30);
	SHA256_Update(&ctx, (const uint8_t *)msg56 + 37, strlen(msg56) - 37);
	SHA256_Final(&ctx, d);
	CHECK(hex_eq(d, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"),
	      "streaming update == one-shot");

	/* long-message vector: 1,000,000 x 'a' (the FIPS example) */
	{
		uint8_t block[1000];
		int i;
		memset(block, 'a', sizeof(block));
		SHA256_Init(&ctx);
		for (i = 0; i < 1000; i++) SHA256_Update(&ctx, block, sizeof(block));
		SHA256_Final(&ctx, d);
		CHECK(hex_eq(d, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"),
		      "SHA256(1,000,000 x 'a')");
	}

	/* KDF: passphrase -> AES-256 session key for the CW transport */
	CHECK(cw_encrypt_with_session((uint8_t *)cw, enc) == 0,
	      "no session key before derivation: encrypt fails closed");
	cw_session_key_len = 0; /* the R26 static: same TU via the header */
	CHECK(cw_session_key_derive("") == 0, "empty passphrase refused");
	CHECK(cw_session_key_derive(NULL) == 0, "NULL passphrase refused");
	CHECK(cw_session_key_derive("r27-modern-ca") == 1, "derive installs a session key");
	CHECK(cw_encrypt_with_session((uint8_t *)cw, enc) == 1, "session encrypt works after derive");
	CHECK(memcmp(enc, cw, 16) != 0, "ciphertext differs from CW");
	CHECK(cw_decrypt_with_session(enc, dec) == 1 && memcmp(dec, cw, 16) == 0,
	      "derived-key roundtrip recovers the CW");
	/* a different passphrase must derive a different key */
	CHECK(cw_session_key_derive("r27-modern-cb") == 1, "re-derive with another passphrase");
	CHECK(cw_encrypt_with_session((uint8_t *)cw, enc2) == 1, "second session encrypt works");
	CHECK(memcmp(enc, enc2, 16) != 0, "different passphrase -> different ciphertext");
	CHECK(cw_decrypt_with_session(enc, dec) == 1 && memcmp(dec, cw, 16) != 0,
	      "wrong-passphrase key does not recover the CW");

	printf("[test_sha256] %d checks, %d failures\n", total, fail);
	return fail ? 1 : 0;
}
