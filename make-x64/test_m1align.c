/*
 * test_m1align.c — TASK R14 (M1): the packed-struct alignment contract.
 *
 * The whole server is compiled with -fpack-struct (deliberately: it fixes the
 * wire layouts). The side effect is that every struct — including AES_KEY,
 * SHA_CTX and struct MD5Context — has alignment 1, so `&thing->key` handed to a
 * function whose parameter is a 4-byte-aligned type (`uint32_t *state`, or the
 * `rk = key->rd_key` pointer arithmetic inside AES) may be under-aligned. On
 * x86-64 that is only a missed warning; on arm/mipsel/sh4 it is a real fault.
 *
 * M1 fixed it by never letting the under-aligned address meet a 4-byte-aligned
 * type: the digest transforms take void* and memcpy the state in and out,
 * AES works on an aligned local copy of the schedule (../src/aes.c), and
 * camd35_init_data_store() computes into aligned locals and copies the result
 * into the packed members (../src/msg-camd35.c).
 *
 * This test pins BOTH halves of the contract:
 *   (a) the hazard is real — the member offsets inside a packed holder are
 *       odd (1, not 4), so the addresses are genuinely under-aligned;
 *   (b) the fix is transparent — every known-answer vector still passes when
 *       the key/state lives in such an under-aligned holder, and the bytes the
 *       fixed path writes are identical to the aligned path's bytes.
 *
 * The contexts are driven through the real ../src/md5.c, ../src/sha1.c,
 * ../src/aes.c, ../src/crc32.c and ../src/msg-camd35.c (the last two are
 * textually included, exactly as ../src/main.c does at :2111/:2112).
 *
 *   make -C make-x64 test
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ---- the externals ../src/msg-camd35.c references, stubbed here: the file
 * is textually included and its UDP send/receive helpers are not exercised,
 * but their symbols must exist for the link. ---- */
void debugf(int dbg, char *fmt, ...)
{
	(void)dbg; (void)fmt;
}
char *ip2string(uint32_t ip)
{
	static char b[32];
	sprintf(b, "%u.%u.%u.%u", (ip >> 24) & 0xff, (ip >> 16) & 0xff,
		(ip >> 8) & 0xff, ip & 0xff);
	return b;
}
int send_nonb(int sock, uint8_t *buf, int len, int to)
{
	(void)sock; (void)buf; (void)len; (void)to;
	return -1;
}
int recv_nonb(int sock, uint8_t *buf, int len, int timeout)
{
	(void)sock; (void)buf; (void)len; (void)timeout;
	return -1;
}

#include "../src/aes.h"
#include "../src/sha1.h"
#include "../src/crc32.c"
#include "../src/md5.c"
#include "../src/msg-camd35.c"

static int passed = 0, failed = 0;

static void check(const char *what, long got, long want)
{
	if (got == want) { passed++; printf("  [ ok ] %-58s = %ld\n", what, got); }
	else { failed++; printf("  [FAIL] %-58s got=%ld want=%ld\n", what, got, want); }
}

/* a check whose "want" is a hex string, so failures print both strings */
static void checkhex(const char *what, const unsigned char *got, const char *want)
{
	char g[2 * 64 + 1], *p = g;
	int i;
	for (i = 0; want[i]; i += 2) {
		sprintf(p, "%02x", got[i / 2]);
		p += 2;
	}
	*p = 0;
	if (!strcmp(g, want)) { passed++; printf("  [ ok ] %-58s %s\n", what, g); }
	else { failed++; printf("  [FAIL] %-58s got=%s want=%s\n", what, g, want); }
}

/* ------------------------------------------------------------------------- *
 * packed holders: -fpack-struct is in force for this whole translation unit,
 * so `pad` is followed by the member at offset 1 -- under-aligned on purpose.
 * ------------------------------------------------------------------------- */
struct aes_holder { unsigned char pad; AES_KEY k; };
struct sha_holder { unsigned char pad; SHA_CTX  k; };
struct md5_holder { unsigned char pad; struct MD5Context k; };
struct camd_holder { unsigned char pad; AES_KEY ek; AES_KEY dk; uint32_t crc; };

static const unsigned char fips_key[16] = {
	0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
	0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f };
static const unsigned char fips_pt[16] = {
	0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
	0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff };

int main(void)
{
	struct aes_holder ah;
	struct sha_holder sh;
	struct md5_holder mh;
	struct camd_holder ch;
	AES_KEY ref;
	unsigned char out[64], back[64];
	char user[32], pass[32];

	printf("-- M1 alignment: the hazard and the shapes --\n");

	check("sizeof(AES_KEY) unchanged (upstream r82a size)",
		(long)sizeof(AES_KEY), 244);
	check("sizeof(SHA_CTX) unchanged",
		(long)sizeof(SHA_CTX), 92);
	check("AES_KEY member sits at offset 1 in a packed holder",
		(long)((unsigned char *)&ah.k - (unsigned char *)&ah), 1);
	check("...so its address is under-aligned (%4)",
		(long)((uintptr_t)&ah.k % 4), 1);
	check("SHA_CTX member address is under-aligned too",
		(long)((uintptr_t)&sh.k % 4), 1);
	check("MD5 context member address is under-aligned too",
		(long)((uintptr_t)&mh.k % 4), 1);
	check("camd35 encryptkey member address is under-aligned",
		(long)((uintptr_t)&ch.ek % 4), 1);
	check("camd35 ucrc member address is under-aligned",
		(long)((uintptr_t)&ch.crc % 4), 1);

	printf("-- AES-128 through an under-aligned key (FIPS-197 vector) --\n");

	memset(&ah, 0, sizeof(ah));
	AES_set_encrypt_key(fips_key, 128, &ah.k);
	AES_encrypt(fips_pt, out, &ah.k);
	checkhex("FIPS-197 C.1 ciphertext", out, "69c4e0d86a7b0430d8cdb78070b4c55a");

	/* AES-128 fills 44 of the 60 schedule words; the setter zeroes the rest,
	 * so the two representations must match byte for byte. */
	memset(&ref, 0, sizeof(ref));
	AES_set_encrypt_key(fips_key, 128, &ref);
	check("packed-key schedule == aligned-key schedule",
		(long)!memcmp(&ah.k, &ref, sizeof(ref)), 1);
	check("the unused tail of the schedule is zeroed, not stack garbage",
		(long)(ref.rd_key[44] | ref.rd_key[59]), 0);

	memset(&ah, 0, sizeof(ah));
	AES_set_decrypt_key(fips_key, 128, &ah.k);
	AES_decrypt(out, back, &ah.k);
	checkhex("FIPS-197 C.1 decrypt round-trip", back,
		"00112233445566778899aabbccddeeff");

	printf("-- MD5: public API and an under-aligned context --\n");

	checkhex("MD5(\"abc\")", MD5((const unsigned char *)"abc", 3, out),
		"900150983cd24fb0d6963f7d28e17f72");
	checkhex("MD5(\"\")", MD5((const unsigned char *)"", 0, out),
		"d41d8cd98f00b204e9800998ecf8427e");

	memset(&mh, 0, sizeof(mh));
	__md5_Init(&mh.k);
	__md5_Update(&mh.k, (const unsigned char *)"abc", 3);
	__md5_Final(out, &mh.k);
	checkhex("MD5(\"abc\") with the context in a packed holder", out,
		"900150983cd24fb0d6963f7d28e17f72");

	printf("-- SHA-1: under-aligned context and bare transform --\n");

	memset(&sh, 0, sizeof(sh));
	SHA1_Init(&sh.k);
	SHA1_Update(&sh.k, (const uint8_t *)"abc", 3);
	SHA1_Final(out, &sh.k);
	checkhex("SHA1(\"abc\") with the context in a packed holder", out,
		"a9993e364706816aba3e25717850c26c9cd0d89d");

	memset(&sh, 0, sizeof(sh));
	SHA1_Init(&sh.k);
	SHA1_Final(out, &sh.k);
	checkhex("SHA1(\"\") with the context in a packed holder", out,
		"da39a3ee5e6b4b0d3255bfef95601890afd80709");

	/* bare transform: identical states from an aligned and an under-aligned
	 * state array -- pins the memcpy read-in AND the write-back in sha1.c. */
	{
		struct { unsigned char pad; uint32_t state[5]; } u;
		uint32_t aligned[5];
		unsigned char b1[64], b2[64];
		int i;

		for (i = 0; i < 64; i++) b1[i] = (unsigned char)(i * 7 + 1);
		memcpy(b2, b1, 64);
		memset(&u, 0, sizeof(u));
		memcpy(aligned, "12345678901234567890", 20);
		memcpy(u.state, aligned, sizeof(aligned));
		SHA1_Transform(u.state, b1);
		SHA1_Transform(aligned, b2);
		check("SHA1_Transform on an unaligned state == aligned state",
			(long)!memcmp(u.state, aligned, sizeof(aligned)), 1);
		check("...and it leaves the caller's block byte-identical",
			(long)!memcmp(b1, b2, 64), 1);
	}

	printf("-- read-only input >= 64 bytes (D69: the transform used to write"
		" into the caller's buffer) --\n");
	{
		/* .rodata on purpose: writing here is a SIGSEGV, not a surprise
		 * later. Both paths below used to die on this. */
		static const unsigned char ro[128] = { 1, 2, 3, 4 };
		unsigned char ro_copy[128];
		unsigned char d1[20], d2[16];

		memcpy(ro_copy, ro, sizeof(ro));

		memset(&sh, 0, sizeof(sh));
		SHA1_Init(&sh.k);
		SHA1_Update(&sh.k, ro, sizeof(ro));
		SHA1_Final(d1, &sh.k);
		check("SHA1 over 128 read-only bytes completes", 1, 1);
		checkhex("SHA1(128 read-only bytes) == hashlib",
			d1, "272049b5add909ae0ed72e347781e50e3670f4dd");
		check("the read-only buffer is byte-identical afterwards",
			(long)!memcmp(ro, ro_copy, sizeof(ro)), 1);

		MD5(ro, sizeof(ro), d2);
		checkhex("MD5(128 read-only bytes) == hashlib", d2,
			"e985d0bfed0587aa1e1b0c1975446847");
		check("the read-only buffer is byte-identical afterwards (MD5)",
			(long)!memcmp(ro, ro_copy, sizeof(ro)), 1);
	}

	printf("-- camd35 handshake data: store path == direct path --\n");

	strcpy(user, "user1");
	strcpy(pass, "pass1");
	memset(&ch, 0, sizeof(ch));
	{
		AES_KEY ek, dk;
		uint32_t crc;

		camd35_init_data_store(user, pass, &ch.ek, &ch.dk, &ch.crc);
		camd35_init_data(user, pass, &ek, &dk, &crc);

		check("store: encryptkey bytes == direct encryptkey bytes",
			(long)!memcmp(&ch.ek, &ek, sizeof(ek)), 1);
		check("store: decryptkey bytes == direct decryptkey bytes",
			(long)!memcmp(&ch.dk, &dk, sizeof(dk)), 1);
		check("store: ucrc value == direct ucrc", (long)ch.crc, (long)crc);
		check("store: ucrc is not zero (a real crc32 was computed)",
			ch.crc != 0, 1);

		/* interop: encrypt along the store path, decrypt along the direct
		 * path -- the two representations really are the same key. */
		memset(back, 0, 16);
		memcpy(back, "DCW-PAYLOAD-0001", 16);
		memcpy(out, back, 16);
		aes_encrypt(&ch.ek, out, 16);
		check("interop: ciphertext differs from the plaintext",
			(long)memcmp(out, back, 16) != 0, 1);
		aes_decrypt(&dk, out, 16);
		check("interop: store->direct AES round-trip restores the payload",
			(long)!memcmp(out, back, 16), 1);
	}

	printf("\n== %d/%d passed, %d failed ==\n", passed, passed + failed, failed);
	return failed ? 1 : 0;
}
