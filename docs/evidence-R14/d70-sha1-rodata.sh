#!/bin/sh
# D70 probe: does SHA1_Update() survive a >= 64-byte input that lives in
# read-only memory?
#
# The FreeBSD-derived SHA1_Transform() expands the message schedule in place
# through `block`, so the arm that did `block = (CHAR64LONG16*)buffer` wrote
# into the caller's buffer -- and SHA1_Update() hands it a `const uint8_t *`.
# With ./src/sha1.c before TASK R14 (D70) this probe dies with SIGSEGV; with
# the fix (local aligned copy of the block) it prints the digest, which is
# compared against Python's hashlib in the same script.
#
#   usage: docs/evidence-R14/d70-sha1-rodata.sh <path-to-sha1.c> [workdir]
#
# Expected before the fix: "rc=139"  (SIGSEGV)
# Expected after  the fix: "rc=0" and matching digests.
set -u

SRC="${1:?usage: d70-sha1-rodata.sh <sha1.c> [workdir]}"
WORK="${2:-/tmp/d69probe}"
mkdir -p "$WORK"
INC="$(cd "$(dirname "$SRC")" && pwd)"

cat > "$WORK/d69probe.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "sha1.h"

int main(void)
{
	static const unsigned char big[128] = { 1, 2, 3, 4 };
	SHA_CTX ctx;
	unsigned char d[SHA_DIGEST_LENGTH];
	int i, mutated = 0;

	SHA1_Init(&ctx);
	SHA1_Update(&ctx, big, sizeof(big));   /* >= 64: the loop transforms big+64 */
	SHA1_Final(d, &ctx);

	for (i = 0; i < (int)sizeof(big); i++) {
		unsigned char want = (i < 4) ? (unsigned char)(i + 1) : 0;
		if (big[i] != want) mutated = 1;
	}
	for (i = 0; i < SHA_DIGEST_LENGTH; i++) printf("%02x", d[i]);
	printf("  input-mutated=%d\n", mutated);
	return mutated;
}
EOF

echo "== source: $SRC"
gcc -O2 -std=gnu89 -I"$INC" -o "$WORK/d69probe" "$WORK/d69probe.c" "$SRC" || exit 1
"$WORK/d69probe"
echo "rc=$?"
echo "== hashlib reference (python3) =="
python3 - <<'PY'
import hashlib
print(hashlib.sha1(bytes([1, 2, 3, 4] + [0] * 124)).hexdigest() + "  reference")
PY
