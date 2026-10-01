#!/bin/bash
# usage: cpu_task.sh [seconds]  -- burn one CPU core for N seconds
secs=${1:-3}
echo "cpu_task: burning CPU for ${secs}s (pid $$)"
n=0
while [ "$SECONDS" -lt "$secs" ]; do n=$((n + 1)); done
echo "cpu_task: finished, iterations=$n"
