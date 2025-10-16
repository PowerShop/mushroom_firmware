# 🔄 Hybrid Mode - Two-Way Sync Summary

## 📊 ภาพรวมระบบ

ระบบ **Hybrid Mode (Mode 2)** ทำงานแบบ **Two-Way Synchronization**:
- 🔵 **บอร์ด → API**: ส่งสถานะไปอัปเดตฐานข้อมูลเมื่อมีการเปลี่ยนแปลง
- 🔴 **API → บอร์ด**: อ่านสถานะจากฐานข้อมูลทุก 1 วินาที

---

## 🎯 การทำงานแบบ Two-Way Sync

### 1️⃣ บอร์ด → API (ส่งสถานะไป Database)

#### **แหล่งที่มาของการเปลี่ยนแปลง:**

**A. กดจากจอบอร์ด (Touch Screen)**
```cpp
[ผู้ใช้กดที่จอ] 
    → [UI เรียก Open_relay() / Close_relay()]
    → [RelayStatus[i] เปลี่ยน]
    → [PUT /api/switch/{id}] ทันที
    → [Database อัปเดต]
```

**B. ควบคุมผ่าน MQTT**
```cpp
[Web/App ส่ง MQTT] 
    → [@private/led0-3]
    → [ControlRelay_Bymanual()]
    → [Open_relay() / Close_relay()]
    → [updateSwitchStateToAPI()] ทันที
    → [PUT /api/switch/{id}]
    → [Database อัปเดต]
```

**C. Timer อัตโนมัติ**
```cpp
[เวลาถึงตามกำหนด] 
    → [ControlRelay_Bytimmer()]
    → [RelayStatus[i] เปลี่ยน]
    → [syncAllRelaysToAPI()] ตรวจสอบ
    → [PUT /api/switch/{id}]
    → [Database อัปเดต]
```

**D. Sensor อัตโนมัติ**
```cpp
[Sensor เกินค่ากำหนด] 
    → [ControlRelay_BysoilMinMax() / BytempMinMax()]
    → [RelayStatus[i] เปลี่ยน]
    → [syncAllRelaysToAPI()] ตรวจสอบ
    → [PUT /api/switch/{id}]
    → [Database อัปเดต]
```

---

### 2️⃣ API → บอร์ด (อ่านสถานะจาก Database)

**ทุก 1 วินาที:**
```cpp
[syncSwitchStatesFromAPI()] ทำงาน
    ↓
[GET /api/switch] ดึงสถานะทั้ง 4 switches
    ↓
[เปรียบเทียบกับ lastKnownSwitchStates[]]
    ↓
[ถ้ามีการเปลี่ยนแปลง]
    ↓
[อัปเดต Relay Hardware]
    - Open_relay(i) สำหรับ state = ON
    - Close_relay(i) สำหรับ state = OFF
    ↓
[บันทึก lastKnownSwitchStates[] ใหม่]
```

**ตัวอย่าง Log:**
```
[INFO] Switch 1 changed: OFF -> ON
[DEBUG] Relay 0 turned ON
```

---

## ⚙️ Configuration

### ตั้งค่าโหมด
```cpp
// ใน HandySense.cpp (บรรทัด ~189)
#define USE_SWITCH_API_CONTROL  2  // Hybrid Mode

// โหมดที่มี:
// 0 = MQTT Only (ไม่ใช้ API)
// 1 = API Only (API ควบคุมทุกอย่าง)
// 2 = Hybrid (Two-Way Sync)
```

### ช่วงเวลาการดึงข้อมูล
```cpp
// ใน SwitchApiClient.h (บรรทัด ~20)
#define SWITCH_POLL_INTERVAL    1000  // 1 วินาที (1000ms)
```

### API Endpoint
```cpp
#define SWITCH_API_BASE_URL     "http://203.159.93.240"
#define SWITCH_API_ENDPOINT     "/api/switch"
#define SWITCH_API_KEY          "DD5B523CF73EF3386DB2DE4A7AEDD"
```

---

## 🔄 Flow Diagram ทั้งระบบ

```
┌────────────────────────────────────────────────┐
│         การควบคุม Switch (4 วิธี)              │
├────────────────────────────────────────────────┤
│  1. จอบอร์ด (Touch Screen)                     │
│  2. MQTT (@private/led0-3)                     │
│  3. Timer (ตั้งเวลา)                           │
│  4. Sensor (อุณหภูมิ/ความชื้น)                 │
└──────────────────┬─────────────────────────────┘
                   ↓
         ┌─────────────────┐
         │ Relay Hardware  │ ← ควบคุมสวิตช์จริง
         │ (O1, O2, O3, O4)│
         └────┬──────┬─────┘
              ↓      ↓
    ┌─────────┐    ┌──────────────────┐
    │ ส่ง API │    │ รับสถานะจาก API  │
    │ ทันที   │    │ ทุก 1 วินาที     │
    └────┬────┘    └────┬─────────────┘
         ↓              ↓
    ┌────────────────────────────┐
    │  PUT /api/switch/{id}      │
    │  GET /api/switch           │
    └──────────┬─────────────────┘
               ↓
    ┌──────────────────────┐
    │  .NET API Server     │
    │  203.159.93.240      │
    └──────────┬───────────┘
               ↓
    ┌──────────────────────┐
    │  MySQL Database      │
    │  table: switch       │
    │  (id, name, state,   │
    │   update_at)         │
    └──────────────────────┘
               ↑
               │
    ┌──────────┴───────────┐
    │  Web Application     │
    │  (ดูสถานะ Real-time) │
    └──────────────────────┘
```

---

## 📝 ตัวอย่างการทำงาน

### Scenario 1: กดสวิตช์จากเว็บ

```
[Web] ผู้ใช้กด Switch 1 เป็น ON
    ↓
[Web] ส่ง HTTP Request: PUT /api/switch/1 {"state": "on"}
    ↓
[API] อัปเดต Database: switch.id=1, state='on'
    ↓
[บอร์ด] ทุก 1 วินาที: GET /api/switch
    ↓
[บอร์ด] ตรวจพบ Switch 1 เปลี่ยนจาก OFF → ON
    ↓
[บอร์ด] Open_relay(0)
    ↓
[Hardware] Relay 1 เปิด! 💡
```

**เวลาที่ใช้:** ~1-2 วินาที (รวม network latency)

---

### Scenario 2: กดสวิตช์จากจอบอร์ด

```
[จอบอร์ด] ผู้ใช้กดปุ่ม Switch 2
    ↓
[UI] เรียก Open_relay(1)
    ↓
[Hardware] Relay 2 เปิด! 💡 (ทันที)
    ↓
[บอร์ด] updateSwitchStateToAPI(2, 1)
    ↓
[API] PUT /api/switch/2 {"state": "on"}
    ↓
[Database] switch.id=2, state='on' อัปเดต
    ↓
[Web] รีเฟรชหน้าเว็บ → เห็นสถานะ ON ✅
```

**เวลาที่ใช้:** Relay ทำงานทันที, API sync ใน ~100-300ms

---

### Scenario 3: Timer ทำงานอัตโนมัติ

```
[เวลา] เวลา 18:00 น. ถึง
    ↓
[บอร์ด] ControlRelay_Bytimmer() ทำงาน
    ↓
[บอร์ด] RelayStatus[2] = 1 (เปิด Relay 3)
    ↓
[Hardware] Relay 3 เปิด! 💡
    ↓
[บอร์ด] syncAllRelaysToAPI() ตรวจสอบ
    ↓
[บอร์ด] ส่ง PUT /api/switch/3 {"state": "on"}
    ↓
[Database] อัปเดตสถานะ
    ↓
[Web] เห็นสถานะ Real-time ✅
```

---

### Scenario 4: Sensor ควบคุม

```
[Sensor] อุณหภูมิ = 32°C (เกินกำหนด 30°C)
    ↓
[บอร์ด] ControlRelay_BytempMinMax() ทำงาน
    ↓
[บอร์ด] RelayStatus[3] = 1 (เปิดพัดลม)
    ↓
[Hardware] Relay 4 เปิด! 💡
    ↓
[บอร์ด] syncAllRelaysToAPI() ตรวจสอบ
    ↓
[บอร์ด] ส่ง PUT /api/switch/4 {"state": "on"}
    ↓
[Database] อัปเดตสถานะ
    ↓
[Web] เห็นว่าพัดลมเปิด + ประวัติการทำงาน ✅
```

---

## 🎨 Serial Monitor Logs

### เมื่อเริ่มต้น:
```
[INFO] Initializing Switch Manager (Mode 2)...
[INFO] Synced states from API
[INFO] API switch 1 state: OFF, Relay 0 state: OFF
[INFO] API switch 2 state: ON, Relay 1 state: OFF
[INFO] API switch 3 state: OFF, Relay 2 state: OFF
[INFO] API switch 4 state: OFF, Relay 3 state: OFF
[INFO] Hybrid Mode: MQTT/Timer/Sensor control relay, changes sync to API
```

### เมื่อ API มีการเปลี่ยนแปลง:
```
[INFO] Switch 2 changed: OFF -> ON
[DEBUG] Relay 1 turned ON
```

### เมื่อบอร์ดส่งข้อมูล:
```
[INFO] MQTT ON relay 0 -> Updating API switch 1
[INFO] Updated switch 1 to API: ON
```

### เมื่อ Timer/Sensor ทำงาน:
```
[INFO] Relay 2 changed to 1, syncing to API...
[INFO] Updated switch 3 to API: ON
```

---

## ⚡ Performance

### Build Results:
- ✅ **RAM Usage:** 40.8% (133,768 / 327,680 bytes)
- ✅ **Flash Usage:** 44.2% (1,476,629 / 3,342,336 bytes)
- ✅ **Build Time:** ~85 seconds
- ✅ **Status:** SUCCESS

### Network Usage:
- **อ่านข้อมูล:** GET request ทุก 1 วินาที (~200 bytes)
- **ส่งข้อมูล:** PUT request เมื่อมีการเปลี่ยนแปลง (~150 bytes)
- **Total:** ~1.7 MB/ชั่วโมง (ในกรณีไม่มีการเปลี่ยนแปลงบ่อย)

---

## 🔍 Troubleshooting

### ปัญหา: บอร์ดไม่อ่านค่าจาก API

**วิธีแก้:**
1. ตรวจสอบ `USE_SWITCH_API_CONTROL = 2`
2. ตรวจสอบ WiFi เชื่อมต่อ
3. ดู Serial Monitor มี log `syncSwitchStatesFromAPI()` หรือไม่
4. ทดสอบ API ด้วย Postman: `GET http://203.159.93.240/api/switch`

### ปัญหา: การอ่านช้าเกินไป

**วิธีแก้:**
```cpp
// ลด interval ใน SwitchApiClient.h
#define SWITCH_POLL_INTERVAL    500  // เปลี่ยนเป็น 0.5 วินาที
```

### ปัญหา: Conflict ระหว่างการควบคุม

**อธิบาย:**
- หากควบคุมจากหลายทางพร้อมกัน (เช่น MQTT + Web)
- สถานะจะถูกอัปเดตตามลำดับเวลา
- ตัวที่ทำล่าสุดจะเป็นตัวที่ชนะ (Last Write Wins)

---

## 📚 สรุปสุดท้าย

### ✅ จุดเด่นของ Hybrid Mode:

1. **Two-Way Sync** - ส่งและรับข้อมูลแบบสองทาง
2. **Real-time** - อัปเดตทันทีเมื่อมีการเปลี่ยนแปลง
3. **ยืดหยุ่น** - ควบคุมได้จากหลายช่องทาง
4. **ปลอดภัย** - ข้อมูลบันทึกในฐานข้อมูลเสมอ
5. **แสดงผล Real-time** - เว็บเห็นสถานะล่าสุดทันที

### 🎯 การใช้งานที่เหมาะสม:

- ✅ **ควบคุมจากเว็บ/แอป** → บอร์ดตอบสนองภายใน 1-2 วินาที
- ✅ **ควบคุมจากบอร์ด** → เว็บเห็นสถานะทันที
- ✅ **Timer/Sensor อัตโนมัติ** → บันทึกประวัติการทำงาน
- ✅ **ดูสถานะ Real-time** → ทั้งจากบอร์ดและเว็บ

---

**🚀 พร้อม Upload และใช้งานได้เลยครับ!**

**หมายเหตุ:** 
- อ่านสถานะจาก API ทุก **1 วินาที**
- ส่งสถานะไป API **ทันที** เมื่อมีการเปลี่ยนแปลง
