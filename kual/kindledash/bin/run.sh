#!/bin/sh
# Runs the dashboard in the foreground of its own session and records how it ended.
DIR="$1"
LOG="$DIR/kindledash.log"
trap '' HUP

export DISPLAY=:0
echo "[run.sh] $(date) launching" >> "$LOG"

/tmp/kindledash "$DIR/dash.ini" >> "$LOG" 2>&1 &
pid=$!
echo $pid > "$DIR/kindledash.pid"
# System log shortly after launch, for debugging.
( sleep 20; { tail -n 200 /var/log/messages 2>/dev/null || logread 2>/dev/null | tail -n 200; } > "$DIR/messages-after-launch.log" ) &
wait $pid
status=$?

echo "[run.sh] $(date) kindledash exited with status $status" >> "$LOG"
# System log around the exit, for debugging.
{ tail -n 150 /var/log/messages 2>/dev/null || logread 2>/dev/null | tail -n 150; } > "$DIR/messages-at-exit.log"
rm -f "$DIR/kindledash.pid"
