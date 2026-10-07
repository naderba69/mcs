#!/bin/sh
# TASK 4.1 — official memory checks.
#
#   ./memcheck.sh units     ASan+UBSan, TSan, and valgrind on `make test`
#   ./memcheck.sh live      the same three tools on a short server session
#   ./memcheck.sh           both
#
# Alignment UBSan is off. Packed structs make that check a flood, and
# the packed-member warnings belong to TASK 4.3, not to this pass.
# The x64 release binaries are not rebuilt and not replaced.
set -u

cd "$(dirname "$0")" || exit 1
LOG=../docs/memcheck-4.1
mkdir -p "$LOG" x64
MODE=${1:-all}
FAIL=0

note() { printf '%s\n' "$*"; }

# Rewrite `make -n test` compile lines into $1 with extra flags $2.
compile_units() {
	dest=$1
	extra=$2
	mkdir -p "$dest"
	make -s -n -B test > "$LOG/make-n-test.txt"
	while IFS= read -r line; do
		out=$(printf '%s\n' "$line" | sed -n 's/.* -o \([^ ]*\).*/\1/p')
		base=$(basename "$out")
		cmd=$(printf '%s\n' "$line" | sed \
			-e "s|^gcc |gcc ${extra} |" \
			-e 's/ -O2 / /g' \
			-e 's/ -O3 / /g' \
			-e "s| -o ${out} | -o ${dest}/${base} |")
		# shellcheck disable=SC2086
		if ! eval "$cmd" >"$LOG/compile-${base}.log" 2>&1; then
			note "COMPILE FAIL $dest/$base"
			tail -20 "$LOG/compile-${base}.log"
			return 1
		fi
	done <<EOF
$(grep '^gcc ' "$LOG/make-n-test.txt")
EOF
}

run_units() {
	dest=$1
	tag=$2
	shift 2
	# remaining args are env assignments, then the runner prefix
	pass=0
	bad=0
	: > "$LOG/${tag}.summary"
	for bin in "$dest"/test_*; do
		name=$(basename "$bin")
		if ! env "$@" "$bin" >"$LOG/${tag}-${name}.log" 2>&1; then
			bad=$((bad + 1))
			note "  FAIL $tag $name"
			tail -30 "$LOG/${tag}-${name}.log" >> "$LOG/${tag}.summary"
		else
			pass=$((pass + 1))
			rm -f "$LOG/${tag}-${name}.log"
		fi
	done
	note "$tag: $pass passed, $bad failed"
	printf '%s\n' "$tag: $pass passed, $bad failed" >> "$LOG/RESULT.txt"
	if [ "$bad" -ne 0 ]; then
		FAIL=1
	fi
}

units() {
	note "== unit tests =="
	asan_flags="-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize=alignment"
	tsan_flags="-O1 -g -fno-omit-frame-pointer -fsanitize=thread"
	vg_flags="-O1 -g -fno-omit-frame-pointer"

	note "compiling ASan+UBSan units"
	compile_units x64/mem-asan "$asan_flags" || return 1
	note "compiling TSan units"
	compile_units x64/mem-tsan "$tsan_flags" || return 1
	note "compiling valgrind units"
	compile_units x64/mem-vg "$vg_flags" || return 1

	run_units x64/mem-asan asan \
		ASAN_OPTIONS=halt_on_error=1:detect_leaks=1:abort_on_error=0 \
		UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1:abort_on_error=0
	run_units x64/mem-tsan tsan \
		TSAN_OPTIONS=halt_on_error=1:abort_on_error=0
	run_units x64/mem-vg valgrind \
		valgrind --quiet --error-exitcode=99 --leak-check=full \
		--show-leak-kinds=definite,indirect \
		--errors-for-leak-kinds=definite,indirect \
		--suppressions=memcheck.supp
}

compile_server() {
	dest=$1
	extra=$2
	mkdir -p "$dest"
	base=$(make -s print-cflags | sed 's/-O2/-O1 -g -fno-omit-frame-pointer/')
	srcs="sha1.c des.c md5.c aes.c dcw.c convert.c tools.c debug.c parser.c ipdata.c threads.c sockets.c msg-newcamd.c msg-cccam.c msg-radegast.c config.c ecmdata.c httpserver.c telnet.c main.c"
	for s in $srcs; do
		# shellcheck disable=SC2086
		if ! gcc $extra $base -w -c "../src/$s" -o "$dest/${s%.c}.o" >"$LOG/srv-${dest##*/}-${s}.log" 2>&1; then
			note "COMPILE FAIL $dest/$s"
			tail -20 "$LOG/srv-${dest##*/}-${s}.log"
			return 1
		fi
		rm -f "$LOG/srv-${dest##*/}-${s}.log"
	done
	# shellcheck disable=SC2086
	if ! gcc $extra $base -w -o "$dest/multics" "$dest"/*.o -pthread >"$LOG/srv-${dest##*/}-link.log" 2>&1; then
		note "LINK FAIL $dest/multics"
		tail -30 "$LOG/srv-${dest##*/}-link.log"
		return 1
	fi
	note "built $dest/multics"
}

# A confirmed memory error. TSan "WARNING: data race" is not in this
# pattern: that class is the documented D49 exception, counted separately.
# "definitely lost: 0" is a clean summary, not a leak. Match a non-zero count.
BAD_RE='ERROR: (AddressSanitizer|ThreadSanitizer)|runtime error:|Invalid (read|write)|definitely lost: [1-9]|indirectly lost: [1-9]|uninitialised value|Conditional jump or move depends'

# Short session: empty config must refuse, then a live config must answer
# HTTP / and /profiles. No stdbuf: its LD_PRELOAD makes ASan refuse to start.
# The server has no shutdown command, so the process is ended with SIGTERM.
# valgrind still prints its summary. ASan leak detection does not run on a
# signal death, so live leaks are valgrind's job.
live_one() {
	tag=$1
	bin=$2
	shift 2
	http=16980
	cfg="$LOG/${tag}.cfg"
	log="$LOG/${tag}-live.log"
	cat > "$cfg" <<EOF
HTTP PORT: $http
HTTP USER: admin
HTTP PASS: admin
HTTP TITLE: memcheck
TELNET PORT: 16982

[ memcheck ]
CAID: 1884
PORT: 16981
USER: u1 p1
EOF
	# empty config, must exit 1 and not crash. env so an ASAN_OPTIONS=
	# word is an assignment, and so `valgrind ...` is the program.
	printf 'HTTP PORT: 16983\n' > "$LOG/${tag}-empty.cfg"
	env "$@" "$bin" -C "$LOG/${tag}-empty.cfg" >"$LOG/${tag}-empty.log" 2>&1
	rc=$?
	if grep -E "$BAD_RE" "$LOG/${tag}-empty.log" >/dev/null 2>&1; then
		note "  FAIL $tag empty-config sanitizer rc=$rc"
		grep -E "$BAD_RE" "$LOG/${tag}-empty.log" | head -20
		FAIL=1
	elif [ "$rc" -ne 1 ] && [ "$rc" -ne 66 ]; then
		note "  FAIL $tag empty-config rc=$rc (expected 1)"
		tail -15 "$LOG/${tag}-empty.log"
		FAIL=1
	elif ! grep -q "no profile configured" "$LOG/${tag}-empty.log"; then
		note "  FAIL $tag empty-config rc=$rc but no refusal message"
		FAIL=1
	else
		note "  ok $tag empty-config refused (rc=$rc)"
	fi

	env "$@" "$bin" -C "$cfg" >"$log" 2>&1 &
	pid=$!
	code=000
	i=0
	while [ "$i" -lt 40 ]; do
		code=$(curl -s -m 2 -u admin:admin -o /dev/null -w '%{http_code}' "http://127.0.0.1:${http}/" || true)
		if [ "$code" = "200" ]; then
			break
		fi
		if ! kill -0 "$pid" 2>/dev/null; then
			break
		fi
		i=$((i + 1))
		sleep 1
	done
	prof=000
	if [ "$code" = "200" ]; then
		prof=$(curl -s -m 3 -u admin:admin -o /dev/null -w '%{http_code}' "http://127.0.0.1:${http}/profiles" || true)
	fi
	# Let the HTTP threads reach dynbuf_free. A signal in the middle
	# abandons the request buffer and valgrind calls that a definite leak.
	sleep 1
	kill -TERM "$pid" 2>/dev/null
	wait "$pid" 2>/dev/null
	wrc=$?
	if [ "$code" != "200" ] || [ "$prof" != "200" ]; then
		note "  FAIL $tag live HTTP /=$code /profiles=$prof"
		tail -20 "$log"
		FAIL=1
		return
	fi
	races=$(grep -c 'WARNING: ThreadSanitizer: data race' "$log" 2>/dev/null || true)
	races=${races:-0}
	if grep -E "$BAD_RE" "$log" >/dev/null 2>&1; then
		note "  FAIL $tag live tool reported an error (HTTP was $code)"
		grep -E "$BAD_RE|SUMMARY:" "$log" | head -40
		FAIL=1
		return
	fi
	note "  ok $tag live HTTP / and /profiles, tool rc=$wrc, races=$races"
	if [ "$races" -gt 0 ]; then
		grep -o 'SUMMARY: ThreadSanitizer:.*' "$log" | sort | uniq -c | sort -nr \
			> "$LOG/${tag}-races.txt"
		note "  TSan data races recorded in $LOG/${tag}-races.txt (D49, not a failure)"
	fi
}

live() {
	note "== short live session =="
	asan_flags="-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize=alignment"
	tsan_flags="-O1 -g -fno-omit-frame-pointer -fsanitize=thread"
	compile_server x64/mem-asan "$asan_flags" || return 1
	compile_server x64/mem-tsan "$tsan_flags" || return 1
	compile_server x64/mem-vg "-O1 -g -fno-omit-frame-pointer" || return 1

	live_one asan x64/mem-asan/multics \
		ASAN_OPTIONS=halt_on_error=0:detect_leaks=0:abort_on_error=0 \
		UBSAN_OPTIONS=halt_on_error=0:print_stacktrace=1:abort_on_error=0
	live_one tsan x64/mem-tsan/multics \
		TSAN_OPTIONS=halt_on_error=0:abort_on_error=0:history_size=2
	live_one valgrind x64/mem-vg/multics \
		valgrind --error-exitcode=99 --leak-check=full \
		--show-leak-kinds=definite,indirect \
		--errors-for-leak-kinds=definite,indirect \
		--suppressions=memcheck.supp \
		--trace-children=no
}

: > "$LOG/RESULT.txt"
case "$MODE" in
	units) units || FAIL=1 ;;
	live) live || FAIL=1 ;;
	all) units || FAIL=1; live || FAIL=1 ;;
	*) note "usage: memcheck.sh [units|live|all]"; exit 2 ;;
esac

if [ "$FAIL" -ne 0 ]; then
	note "MEMCHECK: findings recorded in $LOG"
	printf '%s\n' "MEMCHECK: findings" >> "$LOG/RESULT.txt"
	exit 1
fi
note "MEMCHECK: clean"
printf '%s\n' "MEMCHECK: clean" >> "$LOG/RESULT.txt"
exit 0
