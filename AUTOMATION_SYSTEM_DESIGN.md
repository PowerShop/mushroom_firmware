# 🤖 ระบบอัตโนมัติ Three-Way Sync

## 📋 สรุประบบอัตโนมัติที่มีอยู่

### 1. **Timer-Based Control (ตั้งเวลา)**
- **จำนวน Timer**: 3 Timer/Relay (รวม 12 Timers สำหรับ 4 Relays)
- **การตั้งค่า**: แยกตามวันในสัปดาห์ (Mon-Sun)
- **เวลา**: เปิด-ปิด (HH:MM)
- **สถานะ**: Enable/Disable
- **Storage**: EEPROM (ถาวร)

#### โครงสร้างข้อมูล Timer
```cpp
// time_open[relay][day][timer]
// time_close[relay][day][timer]
// relay: 0-3 (4 relays)
// day: 0-6 (Monday-Sunday)
// timer: 0-2 (3 timers per relay)
// time: Minutes from midnight (0-1440)
// 3000 = Disabled
```

#### ตัวอย่าง Timer Data
```
Timer 1: เปิด 08:00 - ปิด 12:00 (จันทร์-ศุกร์)
Timer 2: เปิด 14:00 - ปิด 18:00 (ทุกวัน)
Timer 3: เปิด 20:00 - ปิด 22:00 (เสาร์-อาทิตย์)
```

### 2. **Sensor-Based Control (ตามค่าเซนเซอร์)**

#### 2.1 Temperature Control
- **Min Temp**: อุณหภูมิต่ำสุด → เปิด Relay (ให้ความร้อน)
- **Max Temp**: อุณหภูมิสูงสุด → เปิด Relay (ระบายความร้อน/พัดลม)
- **Hysteresis**: มี Dead Zone เพื่อป้องกัน On/Off บ่อย

#### 2.2 Soil Moisture Control
- **Min Soil**: ความชื้นดินต่ำสุด → เปิด Relay (รดน้ำ)
- **Max Soil**: ความชื้นดินสูงสุด → เปิด Relay (ระบายน้ำ)

### 3. **Manual Control**
- **MQTT Command**: จาก Web/Mobile App
- **Physical Button**: กดปุ่มบนบอร์ด
- **Touch Screen**: UI บนหน้าจอ

---

## 🔄 Three-Way Synchronization Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      Three-Way Sync                         │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│        ┌──────────────┐                                    │
│        │  .NET API    │◄────────────────┐                 │
│        │  (Database)  │                 │                 │
│        └──────┬───────┘                 │                 │
│               │                         │                 │
│               │ HTTP                    │ HTTP            │
│               │ POST/GET                │ GET             │
│               ▼                         │                 │
│        ┌──────────────┐         ┌──────┴───────┐         │
│        │   ESP32-S3   │◄───────►│     MQTT     │         │
│        │  (HandySense)│  MQTT   │   (NETPIE)   │         │
│        └──────────────┘  PubSub └──────────────┘         │
│               │                                            │
│               │ Control                                    │
│               ▼                                            │
│        ┌──────────────┐                                   │
│        │  4x Relays   │                                   │
│        │  (Hardware)  │                                   │
│        └──────────────┘                                   │
│                                                            │
└────────────────────────────────────────────────────────────┘
```

---

## 📊 ฟีเจอร์ที่ต้องการ

### 1. **Timer Management API**
- ✅ อ่านการตั้งค่า Timer ทั้งหมด
- ✅ แก้ไข Timer (เวลา, วัน, Enable/Disable)
- ✅ Sync กลับไปยัง Board และ MQTT

### 2. **Sensor Threshold API**
- ✅ อ่านค่า Min/Max Temperature
- ✅ อ่านค่า Min/Max Soil Moisture
- ✅ แก้ไขค่า Threshold
- ✅ Sync กลับไปยัง Board และ MQTT

### 3. **Automation Status API**
- ✅ อ่านสถานะปัจจุบัน (Timer Active, Sensor Active)
- ✅ Enable/Disable ระบบอัตโนมัติ
- ✅ Log การทำงาน

### 4. **Override & Priority System**
- Manual > Sensor > Timer
- เมื่อ Manual Override → หยุด Auto ชั่วคราว
- Auto Resume เมื่อครบเวลา (Timeout)

---

## 🎯 Use Cases

### Use Case 1: ตั้งเวลาผ่าน Web
```
User:     ตั้งเวลาผ่าน Web → Timer 1 เปิด 08:00-12:00
API:      บันทึก Database → ส่งไป MQTT → ESP32 อ่าน
ESP32:    บันทึก EEPROM → เมื่อถึงเวลา → เปิด Relay
Status:   API ← ESP32 ส่งสถานะกลับ → MQTT Update
```

### Use Case 2: แก้ไข Sensor Threshold
```
User:     ตั้งค่าผ่าน Web → Min Temp = 20°C
API:      บันทึก Database → ส่งไป MQTT → ESP32 อ่าน
ESP32:    บันทึก EEPROM → เมื่ออุณหภูมิ < 20°C → เปิด Relay
Status:   API ← ESP32 ส่งสถานะกลับ → MQTT Update
```

### Use Case 3: Manual Override
```
User:     กดปุ่มเปิด Relay ผ่าน Web
API:      บันทึก Database → ส่งไป MQTT → ESP32 เปิด Relay
ESP32:    ระงับ Timer/Sensor ชั่วคราว → หลัง 30 นาที Auto Resume
Status:   API ← ESP32 ส่งสถานะกลับ → MQTT Update
```

---

## ⚙️ Configuration Parameters

### Timer Configuration
```json
{
  "relay_id": 0,
  "timer_id": 0,
  "enabled": true,
  "days": [1, 1, 1, 1, 1, 0, 0],  // Mon-Fri
  "time_on": "08:00:00",
  "time_off": "12:00:00"
}
```

### Sensor Threshold
```json
{
  "relay_id": 0,
  "sensor_type": "temperature",
  "min_value": 20.0,
  "max_value": 28.0,
  "enabled": true
}
```

### Automation Status
```json
{
  "relay_id": 0,
  "current_state": "on",
  "control_mode": "timer",  // manual, timer, sensor_temp, sensor_soil
  "timer_active": true,
  "sensor_active": true,
  "override_until": null
}
```

---

## 🔧 Implementation Steps

### Phase 1: Database Schema ✅
- สร้างตาราง automation_timers
- สร้างตาราง automation_sensors
- สร้างตาราง automation_logs

### Phase 2: API Endpoints
- GET/POST /api/timers
- GET/POST /api/sensors
- GET /api/automation/status
- POST /api/automation/override

### Phase 3: ESP32 Integration
- เพิ่ม AutomationApiClient.h/cpp
- Sync Timer จาก API
- Sync Sensor Threshold จาก API
- ส่งสถานะกลับ API

### Phase 4: MQTT Sync
- เพิ่ม topic สำหรับ automation
- Sync Timer config
- Sync Sensor config
- Sync Status

---

## 📈 Data Flow Examples

### Timer Sync Flow
```
Web → API → Database (timers)
          ↓
      MQTT Publish (@private/timer00)
          ↓
    ESP32 Subscribe → Parse → Save EEPROM
          ↓
    When Time Match → Control Relay
          ↓
    HTTP POST → API (status update)
          ↓
    MQTT Publish (@shadow/data/update)
```

### Sensor Sync Flow
```
Web → API → Database (sensors)
          ↓
      MQTT Publish (@private/min_temp0)
          ↓
    ESP32 Subscribe → Parse → Save Variable
          ↓
    When Temp < Min → Control Relay
          ↓
    HTTP POST → API (status update)
          ↓
    MQTT Publish (@shadow/data/update)
```

---

## 🛡️ Safety & Conflict Resolution

### Priority System
```
1. Manual Override (สูงสุด)
2. Sensor Control
3. Timer Control (ต่ำสุด)
```

### Conflict Rules
- Manual Override → ยกเลิก Auto ทั้งหมด
- Sensor > Timer → Sensor ชนะ
- Timer Overlap → Timer ล่าสุดชนะ
- Auto Resume → หลัง 30 นาที (configurable)

### Safety Limits
- ไม่ให้ On/Off เกิน 10 ครั้ง/นาที
- Min/Max Temp: 0-50°C
- Min/Max Soil: 0-100%
- Timer: 00:00-23:59

---

## 📱 UI/UX Features

### Web Dashboard
- 📅 Calendar View สำหรับ Timer
- 📊 Graph แสดงค่า Sensor vs Threshold
- 🎛️ Toggle Switch สำหรับ Enable/Disable
- 📝 Log History
- ⚠️ Alert & Notifications

### Mobile App
- 📲 Push Notification เมื่อ Auto Trigger
- 📍 Quick Toggle (Override)
- 📅 Schedule Management
- 📈 Real-time Status

---

## 🔮 Future Enhancements

- 🤖 AI-based scheduling
- 📈 Historical data analysis
- 🌡️ Weather integration
- 📱 Voice control
- 🔔 Smart notifications
- 📊 Energy optimization
- 🎯 Multi-zone control
- 🔄 Backup & restore

