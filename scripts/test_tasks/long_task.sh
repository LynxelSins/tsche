#!/bin/bash
# usage: long_task.sh <name> <seconds>
NAME=${1:-"Job"}
TOTAL=${2:-15}

echo "[$NAME] เริ่มต้นทำงาน (PID: $$, ระยะเวลาเป้าหมาย: ${TOTAL}s)..."
for ((i=1; i<=TOTAL; i++)); do
    sleep 1
    pct=$(( i * 100 / TOTAL ))
    echo "[$NAME] ดำเนินการไปแล้ว: ${i}/${TOTAL}s (${pct}%)"
done
echo "[$NAME] เสร็จสมบูรณ์เรียบร้อยแล้ว!"
