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
and is not evidence of a warning-free build. Building `config.c` separately
under sanitizer instrumentation emitted six `parse_option_shares` object-size
warnings: the declarations in `config.h` are 100-element arrays, but those
warnings need separate compiler/platform investigation. They do not occur in
the forced x64 release build.

These tests do not prove production behavior, video playback, or correctness on
strict-alignment targets; no cross-platform production deployment was tested.
They also do not establish that every other packed libc structure or every
potential issue in the project has been audited. Tracker rejection coverage
remains partial as documented in `user-trace.md`.
