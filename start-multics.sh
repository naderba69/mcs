#!/bin/sh
# ============================================================
#  مشغّل MultiCS r82a المُحصَّن — start / stop / status
#  جرّبه هكذا:  ./start-multics.sh start   ثم   ./start-multics.sh stop
#
#  مسجَّل بالتدريب: الخادم ينجو من SIGTERM — الإيقاف دائماً kill -9
#  بالـPID المسجَّل في ملف الإقفال، لا بنمط الاسم.
# ============================================================
set -u

DIR="$(cd "$(dirname "$0")" && pwd)"
PIDFILE="$DIR/.multics.pid"
LOG="$DIR/multics.log"
CFG="$DIR/multics-ready.cfg"
HTTPPORT=15500

# ابحث عن الثنائية: بجانب السكربت (الحزمة) أو في bin/ (الشجرة)
if [ -x "$DIR/multics-r82a-stats-x64" ]; then
	BIN="$DIR/multics-r82a-stats-x64"
elif [ -x "$DIR/bin/multics-r82a-stats-x64" ]; then
	BIN="$DIR/bin/multics-r82a-stats-x64"
else
	echo "ERROR: multics-r82a-stats-x64 not found next to the script or in bin/"; exit 1
fi

alive() {
	[ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null
}

do_start() {
	if alive; then
		echo "already running (pid $(cat "$PIDFILE"))"; exit 0
	fi
	rm -f "$PIDFILE"
	[ -f "$CFG" ] || { echo "ERROR: config not found: $CFG"; exit 1; }
	chmod +x "$BIN" 2>/dev/null
	nohup "$BIN" -C "$CFG" > "$LOG" 2>&1 &
	echo $! > "$PIDFILE"
	# انتظر المنفذ (الإقلاع قد يتأخر تحت الحمل — مرصود بالتدريب)
	i=0
	while [ $i -lt 25 ]; do
		if command -v curl >/dev/null 2>&1; then
			code=$(curl -s -m 3 -o /dev/null -w '%{http_code}' "http://127.0.0.1:$HTTPPORT/" 2>/dev/null)
			[ "$code" != "000" ] && break
		else
			(exec 3<>/dev/tcp/127.0.0.1/$HTTPPORT) 2>/dev/null && { exec 3>&-; break; }
		fi
		i=$((i+1)); sleep 1
	done
	if kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
		echo "UP: pid $(cat "$PIDFILE")"
		echo "  web:    http://127.0.0.1:$HTTPPORT  (see multics-ready.cfg for credentials)"
		echo "  telnet: port 15502  -> command: dcwstats"
		echo "  log:    $LOG"
		echo "  stop:   $0 stop"
	else
		echo "FAILED to start -- last log lines:"; tail -5 "$LOG"; exit 1
	fi
}

do_stop() {
	if alive; then
		pid=$(cat "$PIDFILE")
		kill -9 "$pid" 2>/dev/null
		i=0
		while kill -0 "$pid" 2>/dev/null && [ $i -lt 5 ]; do sleep 1; i=$((i+1)); done
		if kill -0 "$pid" 2>/dev/null; then echo "ERROR: pid $pid refuses to die"; exit 1; fi
		echo "stopped (pid $pid)"
	else
		echo "not running"
	fi
	rm -f "$PIDFILE"
}

do_status() {
	if alive; then
		echo "running (pid $(cat "$PIDFILE"))"
		if command -v curl >/dev/null 2>&1; then
			code=$(curl -s -m 3 -o /dev/null -w '%{http_code}' "http://127.0.0.1:$HTTPPORT/" 2>/dev/null)
			echo "  http: $code"
		fi
		echo "  counters: telnet port 15502 -> dcwstats"
	else
		echo "not running"
	fi
}

case "${1:-start}" in
	start)  do_start ;;
	stop)   do_stop ;;
	restart) do_stop; do_start ;;
	status) do_status ;;
	*) echo "usage: $0 [start|stop|restart|status]"; exit 1 ;;
esac
