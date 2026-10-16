#!/bin/sh
DIR="$(cd "$(dirname "$0")/.." && pwd)"
PIDFILE="$DIR/kindledash.pid"
if [ -f "$PIDFILE" ]; then
    kill "$(cat "$PIDFILE")" 2>/dev/null
    rm -f "$PIDFILE"
fi
lipc-set-prop com.lab126.powerd preventScreenSaver 0 2>/dev/null
