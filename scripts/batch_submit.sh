#!/bin/bash
# สคริปต์อ่านไฟล์คำสั่งและส่งเข้า scheduler ตามลำดับ
set -e

FILE="${1:-scripts/tasks.txt}"
CLIENT="./bin/sche"

if [ ! -f "$FILE" ]; then
    echo "Error: ไม่พบไฟล์ '$FILE'" >&2
    echo "การใช้งาน: $0 [path/to/tasks_file]" >&2
    exit 1
fi

if [ ! -x "$CLIENT" ]; then
    echo "Error: ไม่พบไบนารี '$CLIENT' กรุณาสั่ง 'make' ก่อน" >&2
    exit 1
fi

echo "=== กำลังอ่านคำสั่งจาก $FILE และส่งเข้า scheduler ==="
line_no=0
while IFS= read -r line || [ -n "$line" ]; do
    line_no=$((line_no + 1))
    # ตัดช่องว่างหัวท้าย
    trimmed=$(echo "$line" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')
    
    # ข้ามบรรทัดว่างและคอมเมนต์
    if [ -z "$trimmed" ] || [[ "$trimmed" =~ ^# ]]; then
        continue
    fi

    echo "[$line_no] กำลังสั่ง: $CLIENT $trimmed"
    # shellcheck disable=SC2086
    $CLIENT $trimmed
done < "$FILE"

echo ""
echo "=== รายการ Task ในระบบปัจจุบัน ==="
$CLIENT list
