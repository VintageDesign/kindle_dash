#!/bin/sh
# Starts the dashboard. With --wait (used by the scriptlet) it stays in the foreground
# until the dashboard quits, so the library doesn't come back on top of it.
DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$DIR" || exit 1

# Replace a previous instance (it may be running hidden). Only trust the pid file if
# that pid really is kindledash, since a USB connect can kill us without cleanup.
PIDFILE="$DIR/kindledash.pid"
if [ -f "$PIDFILE" ]; then
    old="$(cat "$PIDFILE")"
    if [ -n "$old" ] && grep -q kindledash "/proc/$old/cmdline" 2>/dev/null; then
        kill "$old"
        sleep 1
    fi
    rm -f "$PIDFILE"
fi

[ -f dash.ini ] || cp dash.ini.example dash.ini

# /mnt/us is FAT (no exec bits), so run the binary from tmpfs.
cp "$DIR/bin/kindledash" /tmp/kindledash && chmod +x /tmp/kindledash

if [ "$1" = "--wait" ]; then
    exec sh "$DIR/bin/run.sh" "$DIR"
fi

# New session, so whatever launched us can't take the app down with it.
if command -v setsid >/dev/null 2>&1; then
    setsid sh "$DIR/bin/run.sh" "$DIR" </dev/null >/dev/null 2>&1 &
else
    nohup sh "$DIR/bin/run.sh" "$DIR" </dev/null >/dev/null 2>&1 &
fi
