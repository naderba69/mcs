# x64 build warning audit

The forced `make -B target=x64 link` build initially produced 75 warnings.
The same forced build now completes with zero warnings, without adding warning
suppression flags. This result is specific to that build configuration and
compiler; it is not a statement that every platform or configuration is clean.

Fixes include explicit function declarations (particularly pointer-returning
`isset_get`), `debugf` argument and varargs corrections, standard library
headers, and local DES helper linkage. Packed SHA-1, MD5 and AES state now
uses aligned local arrays without changing packed structure layouts. SHA-1
compression copies its input instead of modifying potentially read-only input,
its block iteration accounts for available bytes, and the bit-counter carry
uses the old low word. The SID lists and camd35 CRC are handled using aligned
local storage. A separate native-layout time formatter prevents libc from
writing a native `struct tm` into the smaller packed form used by the build.
DES's negative signed shift was replaced by a 16-bit unsigned shift. During the
review, an independent overflow in the share-limit parser was also fixed:
provider lists are limited to 16 per CAID, with a warning and excess values
consumed and dropped rather than writing past the array.

Verification: SHA-1, MD5, AES-128/192/256 and DES known-answer vectors, SHA-1
block boundaries/read-only input and misaligned packed contexts; ASan/UBSan
for crypto, the packed profile SID gate, time formatter, share-limit parser and
trace core; packed trace core with warnings as errors; log-viewer regression;
isolated CCcam failure and synthetic Newcamd success; and `make -C make-x64
test`. The make-x64 test wrapper itself uses legacy warning-suppression flags
and is not evidence of a warning-free build.

The six earlier `parse_option_shares` object-size warnings were investigated:
each of the six destinations is a 100-entry array inside a packed parent. The
parser now accepts the actual byte capacity, reserves a slot for the sentinel,
and updates the destination via byte copies from an aligned local array. The
caller passes the actual `sizeof` the destination array; no layout is changed.
Packed ASan/UBSan builds at `-O1`, `-O2` and `-O3` now compile and run the
share-limit regression with no diagnostics, including with
`-Werror=stringop-overflow`. Tests cover an unaligned destination, small
capacities, 99 entries plus sentinel, untouched bytes and provider overflow.
A subsequent native-layout `-O1` sanitizer compile of `config.c` initially
reported 19 packed-member warnings. Four date parsing sites in config and four
in telnet now copy individual fields through a native-layout libc `struct tm`;
valid and invalid formats are checked against native libc in packed and native
builds. Four camd35/CS378x initialization sites now use local AES keys and CRC
before copying into packed members; an alignment-checking stub tests those
copies in both layouts. The native sanitizer compile of `config.c` now reports
seven warnings, all involving initialization or destruction of the packed
server mutex; the corresponding compile of `telnet.c` reports zero. Other
translation units and other packed synchronization or crypto call sites have
not been exhaustively checked. A mutex cannot safely be copied like ordinary
data, so those remaining warnings were not masked: a lifecycle/layout design
and architecture-specific verification are still required.

These tests do not prove production behavior, video playback, or correctness on
strict-alignment targets; no cross-platform production deployment was tested.
They also do not establish that every other packed libc structure or every
potential issue in the project has been audited. Tracker rejection coverage
remains partial as documented in `user-trace.md`.
