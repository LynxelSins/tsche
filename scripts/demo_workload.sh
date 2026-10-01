#!/bin/bash
# Demo: 2 workers, blockers first, then tasks with different priorities.
cd "$(dirname "$0")/.." || exit 1
S=./bin/sche

./bin/tasksched -w 2 &
DPID=$!
sleep 0.5

$S submit -p 5 "sleep 30"                              # 1: blocker A (will be killed)
$S submit -p 5 "sleep 2"                               # 2: blocker B
sleep 0.3                                              # let both workers pick them up
$S submit -p 1 "echo low priority"                     # 3
$S submit -p 9 "./scripts/test_tasks/cpu_task.sh 2"    # 4
$S submit -p 5 "./scripts/test_tasks/error_task.sh"    # 5
$S submit -p 5 "echo mid priority"                     # 6

echo; echo "--- after submit ---"; $S list
sleep 1
echo; echo "--- killing task 1 (frees a worker; highest priority pending starts next) ---"
$S kill 1

for _ in $(seq 1 20); do
    $S list | grep -qE 'PENDING|RUNNING' || break
    sleep 0.5
done

echo; echo "--- final ---"; $S list
echo; echo "--- log of task 4 ---"; $S logs 4
echo; $S status 5
echo; $S shutdown
wait $DPID
