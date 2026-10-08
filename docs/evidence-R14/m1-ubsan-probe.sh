#!/bin/sh
# M1 probe: is the under-aligned access UB still there?
#
# The packed-struct warning family is about UB, not style: an AES_KEY/SHA_CTX
# that lives at an odd offset (which -fpack-struct makes the norm) must never
# be reached through a 4-byte-aligned pointer type. UBSan's alignment check
# turns that UB into a hard error, so this probe compiles the same driver
# twice -- once against src/{aes,sha1} as of HEAD~ (before M1) and once against
# the working tree -- and prints the sanitizer verdict for both.
#
#   usage: docs/evidence-R14/m1-ubsan-probe.sh [ref-with-old-sources] [workdir]
#
# Expected: the pre-M1 build aborts with "misaligned address" on the first AES
# use; the current tree finishes with rc=0 and no sanitizer line.
set -u

REF="${1:-27bb871}"
WORK="${2:-/tmp/ubsan-m1}"
REPO="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
mkdir -p "$WORK/old" "$WORK/new"

cat > "$WORK/probe.c" <<'EOF'
/* Drive AES and SHA-1 through an AES_KEY / SHA_CTX placed at an odd offset --
 * exactly what a packed server struct does to them. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "aes.h"
#include "sha1.h"

struct packed_holder { unsigned char pad; AES_KEY k; SHA_CTX s; };

int main(void)
{
	unsigned char raw[sizeof(struct packed_holder) + 4];
	struct packed_holder *h = (struct packed_holder *)(raw + 1);
	unsigned char key[16], in[16], out[16], data[128], d[20];
	int i;

	memset(raw, 0, sizeof(raw));
	for (i = 0; i < 16; i++) { key[i] = (unsigned char)(0x10 + i); in[i] = (unsigned char)(0x90 + i); }
	for (i = 0; i < 128; i++) data[i] = (unsigned char)(i * 7 + 1);

	printf("offsets (want non-zero): &k%%4=%lu &rd_key%%4=%lu &state%%4=%lu\n",
		(unsigned long)((uintptr_t)&h->k % 4),
		(unsigned long)((uintptr_t)&h->k.rd_key % 4),
		(unsigned long)((uintptr_t)&h->s.state % 4));

	AES_set_encrypt_key(key, 128, &h->k);
	AES_encrypt(in, out, &h->k);
	printf("aes out[0]=%02x\n", out[0]);

	SHA1_Init(&h->s);
	SHA1_Update(&h->s, data, sizeof(data));
	SHA1_Final(d, &h->s);
	printf("sha1[0..3]=%02x%02x%02x%02x\n", d[0], d[1], d[2], d[3]);
	return 0;
}
EOF

for f in aes.c aes.h sha1.c sha1.h; do
	git -C "$REPO" show "$REF:src/$f" > "$WORK/old/$f" || exit 1
	cp "$REPO/src/$f" "$WORK/new/$f"
done

BASE="-O1 -g -fpack-struct -std=gnu89 -fsanitize=undefined,alignment"

# run every probe through a file so the REAL exit status is reported (a pipe
# would hand back grep's status and lie about the abort).
run() {
	tag="$1"; bin="$2"
	"$bin" > "$WORK/$tag.out" 2>&1
	rc=$?
	echo "-- $tag: exit status $rc, $(grep -c 'runtime error' "$WORK/$tag.out") misaligned access(es)"
	grep -v '^0x' "$WORK/$tag.out" | grep -v '^ ' | sed 's/^/   /'
}

echo "== pre-M1 sources (git $REF) -- recovery build: every misaligned site =="
gcc $BASE -I"$WORK/old" -o "$WORK/probe-old-recover" "$WORK/probe.c" "$WORK/old/aes.c" "$WORK/old/sha1.c" || exit 1
run recover-prefix "$WORK/probe-old-recover"

echo
echo "== pre-M1 sources -- halt-on-first-error build (strict) =="
gcc $BASE -fno-sanitize-recover=all -I"$WORK/old" -o "$WORK/probe-old" "$WORK/probe.c" "$WORK/old/aes.c" "$WORK/old/sha1.c" || exit 1
run strict-prefix "$WORK/probe-old"

echo
echo "== working tree (after M1) -- strict build =="
gcc $BASE -fno-sanitize-recover=all -I"$WORK/new" -o "$WORK/probe-new" "$WORK/probe.c" "$WORK/new/aes.c" "$WORK/new/sha1.c" || exit 1
run strict-tree "$WORK/probe-new"
