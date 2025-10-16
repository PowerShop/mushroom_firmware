# 🔌 Switch API Integration Guide

## การเปลี่ยนแปลงหลัก

ระบบได้ถูกอัปเดตให้ใช้ **Switch API** แทนการควบคุม Relay ผ่าน **MQTT**

---

## 📁 ไฟล์ที่เพิ่มเข้ามา

### 1. `SwitchApiClient.h`
- คลาส `SwitchApiClient` - จัดการการเรียก API
- คลาส `SwitchManager` - จัดการ Auto Sync และ Polling

### 2. `SwitchApiClient.cpp`
- Implementation ของ HTTP GET/PUT requests
- จัดการ JSON parsing และ error handling
- Auto sync ทุก 0.5 วินาที

### 3. `HandySense.cpp` (Modified)
- เพิ่ม `#include "SwitchApiClient.h"`
- เพิ่มตัวแปร `USE_SWITCH_API_CONTROL` (เปิด/ปิดใช้งาน)
- ปิดการทำงาน MQTT Switch Control
- เพิ่มการเรียก `syncSwitchStatesFromAPI()` ใน loop

---

## 🔧 Configuration

### การเปิด/ปิดใช้งาน

ใน `HandySense.cpp` มีตัวแปรควบคุม:

```cpp
#define USE_SWITCH_API_CONTROL  1  // 1 = ใช้ Switch API, 0 = ใช้ MQTT เดิม
```

### API Configuration

ใน `SwitchApiClient.h`:

```cpp
#define SWITCH_API_BASE_URL     "http://203.159.93.240"
#define SWITCH_API_ENDPOINT     "/api/switch"
#define SWITCH_API_KEY          "DD5B523CF73EF3386DB2DE4A7AEDD"
#define SWITCH_POLL_INTERVAL    500  // 0.5 วินาที
```

---

## 🎯 วิธีการทำงาน

### 1. Initialization (เมื่อ WiFi เชื่อมต่อสำเร็จ)

```cpp
// ใน TaskWifiStatus() - หลังจาก wifi_ready = true
SwitchManager::begin();

// ดึงสถานะเริ่มต้นจาก API
int initialStates[4];
SwitchApiClient::getAllSwitchStates(initialStates);

// อัปเดต Relay Hardware
for (int i = 0; i < 4; i++) {
    if (initialStates[i] == 1) {
        Open_relay(i);  // เปิด Relay
    } else {
        Close_relay(i); // ปิด Relay
    }
}
```

### 2. Auto Sync Loop (ทุก 0.5 วินาที)

```cpp
// ใน HandySense_loop()
syncSwitchStatesFromAPI();

// ฟังก์ชันนี้จะ:
// 1. ดึงสถานะทั้ง 4 switches จาก API
// 2. เปรียบเทียบกับสถานะเดิม
// 3. ถ้ามีการเปลี่ยนแปลง -> อัปเดต Relay Hardware
```

### 3. Update API (เมื่อบอร์ดเปลี่ยนสถานะ)

```cpp
// เมื่อมีการเปลี่ยนแปลงจากบอร์ด
updateSwitchStateToAPI(switchId, state);

// ส่ง PUT request ไปยัง API
// PUT /api/switch/{id}
// Body: {"id": 1, "state": "on"}
```

---

## 📊 API Endpoints ที่ใช้

### GET /api/switch - ดึงสถานะทั้งหมด

**Request:**
```http
GET /api/switch HTTP/1.1
X-API-KEY: DD5B523CF73EF3386DB2DE4A7AEDD
```

**Response:**
```json
{
  "success": true,
  "message": "ดึงข้อมูล Switch สำเร็จ",
  "data": [
    {"id": 1, "name": "switch1", "state": "off"},
    {"id": 2, "name": "switch2", "state": "on"},
    {"id": 3, "name": "switch3", "state": "off"},
    {"id": 4, "name": "switch4", "state": "off"}
  ]
}
```

### GET /api/switch/{id} - ดึงสถานะเฉพาะ

**Request:**
```http
GET /api/switch/1 HTTP/1.1
X-API-KEY: DD5B523CF73EF3386DB2DE4A7AEDD
```

**Response:**
```json
{
  "success": true,
  "message": "ดึงข้อมูล Switch สำเร็จ",
  "data": {
    "id": 1,
    "name": "switch1",
    "state": "off",
    "description": "สวิตช์ 1",
    "updateAt": "2025-10-12T21:28:14"
  }
}
```

### PUT /api/switch/{id} - อัปเดตสถานะ

**Request:**
```http
PUT /api/switch/1 HTTP/1.1
X-API-KEY: DD5B523CF73EF3386DB2DE4A7AEDD
Content-Type: application/json

{
  "id": 1,
  "state": "on"
}
```

**Response:**
```json
{
  "success": true,
  "message": "อัพเดท Switch ID 1 เป็น on สำเร็จ",
  "data": {
    "id": 1,
    "name": "switch1",
    "state": "on",
    "description": "สวิตช์ 1",
    "updateAt": "2025-10-12T22:30:45"
  }
}
```

---

## 🔍 การทำงานโดยละเอียด

### Flow Diagram

```
[บอร์ด ESP32]
    ↓
[WiFi Connected] → [SwitchManager::begin()]
    ↓
[ดึงสถานะเริ่มต้น] → GET /api/switch
    ↓
[อัปเดต Relay 1-4]
    ↓
[Loop ทุก 0.5 วินาที]
    ├→ GET /api/switch
    ├→ เปรียบเทียบกับสถานะเดิม
    ├→ ถ้าเปลี่ยน → อัปเดต Relay Hardware
    └→ บันทึกสถานะใหม่
```

### Switch State Mapping

| Switch ID | Relay Index | Pin | Array Position |
|-----------|-------------|-----|----------------|
| 1 | 0 | O1_PIN | RelayStatus[0] |
| 2 | 1 | O2_PIN | RelayStatus[1] |
| 3 | 2 | O3_PIN | RelayStatus[2] |
| 4 | 3 | O4_PIN | RelayStatus[3] |

### State Conversion

- API: `"on"` → Board: `1` → Relay: `HIGH`
- API: `"off"` → Board: `0` → Relay: `LOW`

---

## 🚨 สิ่งที่ถูกปิดการใช้งาน (MQTT)

### 1. MQTT Switch Control (Disabled)

```cpp
// ใน callback() - ส่วนนี้ถูก comment
// else if (topic.substring(0, 12) == "@private/led") {
//     ControlRelay_Bymanual(topic, message, length);
// }
```

### 2. MQTT Status Report (Disabled)

```cpp
// ใน sendStatus_RelaytoWeb() - ไม่ทำงานเมื่อใช้ Switch API
#if !USE_SWITCH_API_CONTROL
  // ส่งสถานะผ่าน MQTT เท่านั้น
  client.publish("@shadow/data/update", _payload.c_str());
#endif
```

### 3. ฟังก์ชันที่ยังใช้งานได้ (ไม่กระทบ)

- `ControlRelay_Bytimmer()` - Timer control
- `ControlRelay_BysoilMinMax()` - Soil sensor control  
- `ControlRelay_BytempMinMax()` - Temperature sensor control
- `send_soilMinMax()` - Soil min/max reporting
- `send_tempMinMax()` - Temp min/max reporting

---

## 📝 ตัวแปรสำคัญ

### Global Variables

```cpp
// ใน HandySense.cpp
int RelayStatus[4];                    // สถานะปัจจุบันของ Relay
int lastKnownSwitchStates[4];         // สถานะล่าสุดจาก API
bool wifi_ready;                       // WiFi connection status

// ใน SwitchManager
unsigned long lastPollTime;            // เวลา Poll ครั้งล่าสุด
int previousStates[4];                 // สถานะก่อนหน้าสำหรับเปรียบเทียบ
bool initialized;                      // สถานะการ initialize
bool autoSyncEnabled;                  // เปิด/ปิด Auto Sync
```

---

## 🎨 ตัวอย่างการใช้งาน

### ตัวอย่าง 1: ควบคุม Switch จากเว็บ/แอป

```
[Web/App] 
    ↓ PUT /api/switch/1 {"state": "on"}
    ↓
[API Server] → อัปเดต Database
    ↓
[บอร์ด ESP32] → GET /api/switch (ทุก 0.5s)
    ↓
[ตรวจพบการเปลี่ยนแปลง]
    ↓
[Open_relay(0)] → Relay 1 เปิด
```

### ตัวอย่าง 2: ควบคุมจากบอร์ดโดยตรง

```
[บอร์ด ESP32] → Open_relay(0) (จาก Timer/Sensor)
    ↓
[RelayStatus[0] = 1]
    ↓
[updateSwitchStateToAPI(1, 1)]
    ↓ PUT /api/switch/1 {"state": "on"}
    ↓
[API Server] → อัปเดต Database
    ↓
[Web/App] → อัปเดตหน้าจอ
```

---

## 🔒 Security

### API Authentication

ทุก Request ต้องมี Header:
```
X-API-KEY: DD5B523CF73EF3386DB2DE4A7AEDD
```

### Error Handling

- **401 Unauthorized**: API Key ไม่ถูกต้อง
- **404 Not Found**: Switch ID ไม่มีในระบบ
- **400 Bad Request**: ข้อมูลไม่ถูกต้อง
- **500 Internal Server Error**: ปัญหาเซิร์ฟเวอร์

---

## 📈 Monitoring & Debugging

### Serial Monitor Output

```
[INFO] Switch API Control Enabled
[INFO] Initializing Switch Manager...
[INFO] Synced states from API
[INFO] Initial switch 1 state: OFF
[INFO] Initial switch 2 state: ON
[INFO] Initial switch 3 state: OFF
[INFO] Initial switch 4 state: OFF
[INFO] Switch 2 changed: OFF -> ON
[DEBUG] Relay 2 turned ON
```

### Log Levels

- `ESP_LOGI` - Information (สถานะปกติ)
- `ESP_LOGD` - Debug (รายละเอียด)
- `ESP_LOGV` - Verbose (ทุกอย่าง)
- `ESP_LOGW` - Warning (คำเตือน)
- `ESP_LOGE` - Error (ข้อผิดพลาด)

---

## 🛠 Troubleshooting

### ปัญหา 1: Switch ไม่อัปเดต

**อาการ:** Relay ไม่เปลี่ยนสถานะตาม API

**แก้ไข:**
1. ตรวจสอบ WiFi: `WiFi.isConnected()`
2. ตรวจสอบ API Key ถูกต้อง
3. ตรวจสอบ Serial Monitor มี log `Failed to sync`
4. ลอง Restart บอร์ด

### ปัญหา 2: Polling ช้า

**อาการ:** ใช้เวลานานกว่า 0.5 วินาที

**แก้ไข:**
1. ตรวจสอบ Network latency
2. เพิ่ม timeout: `http.setTimeout(5000)`
3. ปรับ `SWITCH_POLL_INTERVAL` ถ้าจำเป็น

### ปัญหา 3: Build Error

**อาการ:** Compile ไม่ผ่าน

**แก้ไข:**
1. ตรวจสอบ `#include "SwitchApiClient.h"` ใน HandySense.cpp
2. ตรวจสอบไฟล์ `SwitchApiClient.h` และ `.cpp` อยู่ใน `src/`
3. Clean และ Build ใหม่

---

## 🚀 การ Deploy

### ขั้นตอน Build & Upload

```powershell
# Clean project
pio run -t clean

# Build release
pio run -e release

# Upload to board
pio run -e release -t upload

# Monitor Serial
pio device monitor -b 115200
```

### การทดสอบ

1. **Upload firmware**
2. **เปิด Serial Monitor**
3. **รอ WiFi connect**
4. **สังเกต log:**
   ```
   [INFO] Switch API Control Enabled
   [INFO] Initializing Switch Manager...
   [INFO] Synced states from API
   ```
5. **ทดสอบจากเว็บ:** เปลี่ยนสถานะ Switch
6. **ตรวจสอบ Relay:** ควรเปลี่ยนภายใน 0.5 วินาที

---

## 📚 สรุป

### ✅ ข้อดีของระบบใหม่

- ✅ ควบคุมผ่าน API ได้ทันที
- ✅ Sync อัตโนมัติทุก 0.5 วินาที
- ✅ รองรับหลายอุปกรณ์พร้อมกัน
- ✅ มี Error handling ที่ดี
- ✅ สามารถเปิด/ปิดได้ง่าย (`USE_SWITCH_API_CONTROL`)

### ⚠️ ข้อควรระวัง

- ต้องมี WiFi เชื่อมต่อ
- ต้องมี API Server ทำงาน
- API Key ต้องถูกต้อง
- Network latency อาจมีผล

---

**💡 หากมีปัญหาหรือข้อสงสัย ตรวจสอบ Serial Monitor ก่อนเสมอ!**
