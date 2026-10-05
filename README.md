# tsche Interactive TUI Dashboard & Task Scheduler

**tsche** เป็นระบบบริหารจัดการคิวงาน (POSIX Task Scheduler) ที่มาพร้อมหน้าจอ **Interactive TUI Dashboard (ncurses)** สำหรับมอนิเตอร์และสั่งการแบบเรียลไทม์ โดยรองรับการจัดการสระ Worker (Worker Pool), การจัดลำดับความสำคัญ (Priority Queue 0–9), การตรวจสอบ Logs สดๆ และการควบคุมโปรเซสงาน (SIGTERM / Kill) ผ่าน UNIX Domain Socket

---

## 🖥️ หน้าจอแดชบอร์ด (TUI Dashboard Preview)

เมื่อเปิดใช้งาน `sche top` (หรือรัน `./bin/sche`) หน้าจอจะแบ่งเป็น 3 โซนหลัก:

```text
┌─────────────────────────────────────────────────────────────────────────────┐
│ tasksched   pending:4 running:2 done:3 failed:1 killed:1                    │ <- สรุปสถานะภาพรวม
├─────────────────────────────────────────────────────────────────────────────┤
│  ID   PRI STATUS   PID     EXIT   COMMAND                                   │
│  12   9   RUNNING  845120  -      ./scripts/test_tasks/cpu_task.sh 12       │ <- ตารางงาน
│  11   8   RUNNING  845121  -      ./scripts/test_tasks/long_task.sh ...     │    (งานใหม่อยู่บน)
│  10   7   PENDING  -       -      ./scripts/test_tasks/long_task.sh ...     │
├─────────────────────────────────────────────────────────────────────────────┤
│  log of task 12                                                             │ <- หน้าต่างดู Log สด
│ [Database-Backup] เริ่มต้นทำงาน (PID: 845121, ระยะเวลา: 20s)...               │    (กด Enter เพื่อเปิด)
│ [Database-Backup] ดำเนินการไปแล้ว: 6/20s (30%)                              │
├─────────────────────────────────────────────────────────────────────────────┤
│ showing logs of task 12 (type 'close' or Esc to hide)                       │ <- แถบข้อความแจ้งเตือน
│ > submit -p 9 echo "ด่วนมาก"                                                 │ <- ช่องพิมพ์คำสั่ง
│ Enter: run   Up/Down: history   Tab: select tasks   Esc: close log          │ <- แถบวิธีใช้งาน
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 🛠️ การติดตั้งและการเตรียมระบบ (Prerequisites & Build)

### 1. ติดตั้ง Dependencies

**Debian / Ubuntu / Pop!_OS:**
```bash
sudo apt update
sudo apt install build-essential libncurses5-dev libncursesw5-dev
```

**Fedora / RHEL / CentOS:**
```bash
sudo dnf install gcc make ncurses-devel
```

**Arch Linux / Manjaro:**
```bash
sudo pacman -S base-devel ncurses
```

### 2. คอมไพล์โปรเจค
```bash
make
```
> จะได้ไบนารี 2 ตัวในโฟลเดอร์ `bin/`:
> - `bin/sche` (TUI Dashboard & Client CLI)
> - `bin/tasksched` (Background Daemon)

---

## 🚀 วิธีเปิดใช้งาน TUI Dashboard

เปิดหน้าจอ Dashboard ขึ้นมาได้ทันที:

```bash
./bin/sche
# หรือ
./bin/sche top
```

> 💡 **ระบบ Auto-start Daemon:** หาก Service Daemon (`tasksched`) ยังไม่ได้รัน ตัว Dashboard จะทำการ start daemon ขึ้นมาเบื้องหลังให้อัตโนมัติทันที

---

## 🎮 คู่มือการควบคุม Dashboard (Controls & Commands)

Dashboard รองรับการทำงาน 2 โหมด โดยสลับโหมดได้ด้วยปุ่ม **`Tab`**

### 1. โหมดพิมพ์คำสั่ง (Prompt Mode: `> `)
พิมพ์คำสั่งลงในช่องด้านล่าง แล้วกด **`Enter`**:

| คำสั่ง | ตัวอย่างการใช้งาน | คำอธิบาย |
|---|---|---|
| `submit [-p 0-9] <cmd>` | `submit -p 9 ./scripts/test_tasks/cpu_task.sh 10` | ส่งงานเข้าคิว (Priority `9` สูงสุด จะแซงคิวขึ้นมารันก่อน) |
| `logs <id>` | `logs 3` | เปิดหน้าต่างดู Log ของ Task หมายเลขนั้นแบบเรียลไทม์ |
| `close` | `close` | ปิดหน้าต่าง Log |
| `kill <id>` | `kill 2` | ยกเลิกงานที่รอคิว หรือส่งสัญญาณ `SIGTERM` หยุดงานที่กำลังรัน |
| `shutdown` | `shutdown` | สั่งปิด Daemon |
| `help` | `help` | แสดงคำสั่งช่วยเหลือ |
| `quit` หรือ `exit` | `quit` | ออกจากหน้า Dashboard (กด `Ctrl + D` ได้เช่นกัน) |
| **ลูกศร `↑` / `↓`** | - | เลื่อนดูประวัติคำสั่งที่เคยพิมพ์ก่อนหน้า (Command History) |

---

### 2. โหมดเลือกตารางงาน (Task List Mode)
กด **`Tab`** เพื่อสลับมาไฮไลต์เลือกแถวในตาราง:

| ปุ่มคีย์ลัด | การทำงาน |
|---|---|
| **`↑` / `↓`** | เลื่อนแถบไฮไลต์เลือก Task ที่ต้องการ |
| **`Enter`** | **เปิดดู Log** ของ Task ที่เลือกอยู่ทันที |
| **`x`** | **สั่ง Kill** งานที่เลือกอยู่ทันที (งาน Pending จะถูกยกเลิก, งาน Running จะถูก SIGTERM) |
| **`Esc`** | ปิดหน้าต่าง Log |
| **`Tab`** | สลับกลับไปที่ช่องพิมพ์คำสั่ง (Prompt Mode) |
| **`q`** | ออกจากหน้าจอ Dashboard |

---

## 🧪 การทดสอบชุดงานจำลอง (Long & Heavy Workload Testing)

เพื่อให้เห็นการทำงานของ Dashboard ชัดเจน ทั้ง**การจัดคิว (Pending)**, **การแย่ง Worker รันคู่ขนาน (Running)**, **การแซงคิวตาม Priority** และ**การดู Log ความคืบหน้าแบบสดๆ** ทางโปรเจคได้เตรียมชุดคำสั่งทดสอบงานหนักและใช้เวลานานไว้ให้แล้ว

### ไฟล์ชุดคำสั่งทดสอบ: `scripts/heavy_tasks.txt`
ประกอบด้วยงานหลากหลายรูปแบบกว่า 12 งาน เช่น:
- งานกิน CPU หนัก 12 วินาที (`cpu_task.sh 12`)
- งานจำลอง Backup ฐานข้อมูล 20 วินาที พร้อมแสดงเปอร์เซ็นต์ความคืบหน้าทุกวินาที
- งานประมวลผลรูปภาพ / เทรนโมเดล 15 - 25 วินาที
- งานจำลอง Error เพื่อทดสอบสถานะ `FAILED`
- งานด่วน Priority สูงสุดเพื่อดูการแทรกคิว

---

### 🌟 วิธีรันการทดสอบและดูผลบน Dashboard แบบสดๆ

เปิด 2 หน้าต่าง Terminal ควบคู่กัน:

#### **Terminal 1: เปิดหน้าจอ Dashboard เพื่อสังเกตการณ์**
```bash
./bin/sche top
```

#### **Terminal 2: ป้อนชุดคำสั่งงานหนักทั้งหมดเข้าสู่ระบบ**
```bash
./scripts/batch_submit.sh scripts/heavy_tasks.txt
```

#### สิ่งที่คุณจะได้เห็นบน Dashboard (Terminal 1):
1. **คิวงานและ Concurrency:** งานจะทยอยเข้าสู่สถานะ `RUNNING` ตามจำนวน Worker ที่เปิดไว้ (ค่าเริ่มต้น 4 ตัว) ส่วนงานที่เหลือจะอยู่ในสถานะ `PENDING`
2. **การจัดลำดับ Priority:** สังเกตคอลัมน์ `PRI` งานที่มีความสำคัญสูง (8, 9) จะได้รับการหยิบไปรันก่อนงาน Priority ต่ำ (1, 2, 3) เสมอเมื่อมี Worker ว่าง
3. **การดู Log สด:** กด `Tab` เลื่อนแถบไฮไลต์ไปที่งาน `Database-Backup` แล้วกด `Enter` จะเห็นหน้าต่าง Log ด้านล่างอัปเดตเปอร์เซ็นต์ `10%... 20%... 30%` แบบสดๆ
4. **การทดสอบ Kill:** กดเลื่อนไปที่งานที่กำลังรันแล้วกด `x` งานนั้นจะหยุดทันทีและเปลี่ยนสถานะเป็น `KILLED (sig15)` และเปิดโอกาสให้งานถัดไปในคิวเริ่มรันทันที!

---

## 📁 โครงสร้างโปรเจค

```text
tsche/
├── bin/                    # ไบนารีที่คอมไพล์แล้ว (sche, tasksched)
├── build/                  # ไฟล์คอมไพล์ (.o, .d)
├── include/                # C Header files
│   ├── client/
│   ├── common/
│   ├── daemon/
│   └── tui/                # ส่วนควบคุม Dashboard (dashboard.h)
├── scripts/
│   ├── batch_submit.sh     # สคริปต์ตัวอ่านและส่งคำสั่งเข้าคิวอัตโนมัติ
│   ├── heavy_tasks.txt     # ชุดงานทดสอบระยะยาวและหลากหลาย (12 งาน)
│   ├── tasks.txt           # ชุดงานทดสอบพื้นฐาน
│   ├── demo_workload.sh    # สคริปต์รันเดโมแบบอัตโนมัติ
│   └── test_tasks/
│       ├── long_task.sh    # สคริปต์จำลองงานรันนาน พร้อมรายงาน % progress ทุกวินาที
│       ├── cpu_task.sh     # สคริปต์เบิร์น CPU ตามจำนวนวินาที
│       └── error_task.sh   # สคริปต์จำลองข้อผิดพลาด (Exit 3)
├── src/                    # C Source files (dashboard.c, main.c, scheduler.c, ...)
├── Makefile
└── README.md
```
