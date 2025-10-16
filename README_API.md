# 🚀 สรุปการเพิ่ม .NET API Integration

## ✅ สิ่งที่เพิ่มเข้าไปในโปรเจกต์

### 1. **ไฟล์ใหม่ที่สร้าง**

#### `src/ApiClient.h`
- Header file สำหรับจัดการ API calls
- กำหนดค่า configuration ต่างๆ
- สามารถเปิด/ปิดการใช้งาน API แต่ละตัวได้

#### `src/ApiClient.cpp`  
- Implementation ของ API client
- ส่งข้อมูลไปยัง .NET API ด้วย HTTP POST
- รองรับ Custom API อื่นๆ ได้

#### `API_GUIDE.md`
- คู่มือการใช้งานและขยาย API
- ตัวอย่างการเพิ่ม API อื่นๆ (ThingSpeak, InfluxDB, Custom API)

---

## 📊 โครงสร้างการส่งข้อมูล

### .NET API Endpoint
```
POST http://203.159.93.240/api/telemetry
Headers:
  Content-Type: application/json
  X-API-KEY: DD5B523CF73EF3386DB2DE4A7AEDD
```

### JSON Payload ที่ส่ง
```json
{
  "id": 0,
  "site_id": "site1",
  "room_id": "roomA",
  "ts": "2025-10-11T20:23:16.000Z",
  "temp_c": 25.5,
  "hum_rh": 60.2,
  "light_lux": 1.234,
  "water_delta_l": 0,
  "energy_delta_kwh": 0,
  "rssi": -65,
  "device": "ESP32"
}
```

### ค่าที่ส่ง
- **temp_c**: อุณหภูมิ (°C)
- **hum_rh**: ความชื้น (%RH)
- **light_lux**: แสง (Klux)
- **water_delta_l**: 0 (รอเซนเซอร์)
- **energy_delta_kwh**: 0 (รอเซนเซอร์)
- **rssi**: WiFi signal strength (dBm)
- **device**: "ESP32"
- **site_id**: "site1"
- **room_id**: "roomA"
- **ts**: timestamp ปัจจุบัน (ISO 8601)

---

## 🔧 การตั้งค่า

### เปิด/ปิดการส่งข้อมูล

แก้ไขใน `src/ApiClient.h`:

```cpp
#define API_ENABLE_DOTNET       1  // 1 = เปิด, 0 = ปิด .NET API
#define API_ENABLE_NETPIE       1  // 1 = เปิด, 0 = ปิด NETPIE MQTT
```

### เปลี่ยน URL หรือ API Key

```cpp
#define DOTNET_API_URL          "http://203.159.93.240/api/telemetry"
#define DOTNET_API_KEY          "DD5B523CF73EF3386DB2DE4A7AEDD"
```

### เปลี่ยน Site ID / Room ID

```cpp
#define SITE_ID                 "site1"
#define ROOM_ID                 "roomA"
```

---

## 🔄 Flow การทำงาน

```
[เซนเซอร์] 
    ↓ อ่านค่าทุก 1 วินาที
[ESP32-S3]
    ↓ ตรวจสอบเงื่อนไข
    ├─ ค่าเปลี่ยนแปลงมาก (±4°C หรือ ±20% soil) → ส่งทันที
    └─ ครบ 2 นาที → ส่งตามกำหนด
[WiFi Connected]
    ├─ MQTT → NETPIE (@shadow/data/update)
    └─ HTTP POST → .NET API (http://203.159.93.240/api/telemetry)
```

---

## 📝 ตัวอย่างการใช้งาน

### ส่งข้อมูลเซนเซอร์

```cpp
// ใน HandySense.cpp - UpdateData_To_Server()

// Send to .NET API
bool apiResult = ApiClient::sendTelemetryToDotNetAPI(
    temp,           // 25.5°C
    humidity,       // 60.2%
    lux_44009,      // 1.234 Klux
    0.0,            // water (รอเซนเซอร์)
    0.0             // energy (รอเซนเซอร์)
);
```

### ส่งไปยัง Custom API

```cpp
// ตัวอย่างส่งไป API อื่น
String customPayload = "{\"temp\": 25.5, \"hum\": 60.2}";

bool success = ApiClient::sendToCustomAPI(
    "https://your-api.com/endpoint",  // URL
    "your-api-key",                    // API Key
    customPayload.c_str()              // JSON payload
);
```

---

## 🎯 การขยายรองรับ API อื่นๆ

### ตัวอย่าง: เพิ่ม ThingSpeak API

1. **เพิ่มใน `ApiClient.h`:**

```cpp
#define API_ENABLE_THINGSPEAK   1
#define THINGSPEAK_API_KEY      "YOUR_WRITE_API_KEY"

static bool sendToThingSpeak(float temp, float hum, float light);
```

2. **เพิ่มใน `ApiClient.cpp`:**

```cpp
bool ApiClient::sendToThingSpeak(float temp, float hum, float light) {
    String url = "https://api.thingspeak.com/update";
    url += "?api_key=" + String(THINGSPEAK_API_KEY);
    url += "&field1=" + String(temp);
    url += "&field2=" + String(hum);
    url += "&field3=" + String(light);

    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();
    http.end();

    return (httpCode == HTTP_CODE_OK);
}
```

3. **เรียกใช้ใน `HandySense.cpp`:**

```cpp
static void UpdateData_To_Server() {
    // ...existing code...
    
    #if API_ENABLE_THINGSPEAK
    ApiClient::sendToThingSpeak(temp, humidity, lux_44009);
    #endif
}
```

---

## 🐛 การ Debug

### เปิด Verbose Log

ใน `platformio.ini`:

```ini
[env:release]
build_flags = 
  ${common.build_flags}
  -DCORE_DEBUG_LEVEL=5  ; 5 = Verbose
```

### Log Messages ที่จะเห็น

```
[V][ApiClient] Sending to DotNet API...
[V][ApiClient] Payload: {"id":0,"site_id":"site1",...}
[I][ApiClient] HTTP Response code: 200
[V][ApiClient] Response: {"success":true}
[V][ApiClient] Send Data Complete (.NET API)
```

### ถ้ามี Error

```
[W][ApiClient] WiFi not connected, cannot send to API
[E][ApiClient] HTTP Error: Connection refused
[W][ApiClient] Send Data Failed (.NET API)
```

---

## 📈 ประสิทธิภาพ

### Memory Usage
- **RAM**: 40.8% (133,680 / 327,680 bytes)
- **Flash**: 43.9% (1,468,409 / 3,342,336 bytes)

### การส่งข้อมูล
- **ปกติ**: ทุก 2 นาที
- **ฉุกเฉิน**: เมื่อค่าเปลี่ยนแปลงเกินเกณฑ์
- **Timeout**: 5 วินาที/request

---

## 🔐 Security

### API Key
- เก็บใน `ApiClient.h` (compile time constant)
- ส่งผ่าน HTTP Header: `X-API-KEY`

### Recommendations
- ใช้ HTTPS แทน HTTP (ถ้า server รองรับ)
- เปลี่ยน API Key เป็นระยะๆ
- ใช้ Certificate pinning สำหรับ HTTPS

---

## 🚀 การ Deploy

### 1. Build Firmware
```bash
platformio run -e release
```

### 2. Upload ไปยังบอร์ด
```bash
platformio run -e release -t upload
```

### 3. Monitor Serial Output
```bash
platformio device monitor
```

---

## 📞 การทดสอบ API

### ใช้ curl ทดสอบ

```bash
curl -X POST http://203.159.93.240/api/telemetry \
  -H "Content-Type: application/json" \
  -H "X-API-KEY: DD5B523CF73EF3386DB2DE4A7AEDD" \
  -d '{
    "id": 0,
    "site_id": "site1",
    "room_id": "roomA",
    "ts": "2025-10-11T20:23:16.000Z",
    "temp_c": 25.5,
    "hum_rh": 60.2,
    "light_lux": 1.234,
    "water_delta_l": 0,
    "energy_delta_kwh": 0,
    "rssi": -65,
    "device": "ESP32"
  }'
```

### ใช้ Postman

1. Method: POST
2. URL: `http://203.159.93.240/api/telemetry`
3. Headers:
   - `Content-Type: application/json`
   - `X-API-KEY: DD5B523CF73EF3386DB2DE4A7AEDD`
4. Body: Raw JSON (ดูตัวอย่างด้านบน)

---

## ✨ Features ที่เพิ่มเข้ามา

✅ ส่งข้อมูลไปยัง .NET API พร้อมกับ NETPIE  
✅ รองรับการเปิด/ปิดแต่ละ API  
✅ Auto retry และ timeout handling  
✅ รวม WiFi RSSI ในข้อมูลที่ส่ง  
✅ รองรับ Timestamp แบบ ISO 8601  
✅ สามารถขยาย API อื่นๆ ได้ง่าย  
✅ Error logging และ debugging  

---

## 📚 ไฟล์ที่แก้ไข

1. ✨ **NEW** `src/ApiClient.h` - API Client header
2. ✨ **NEW** `src/ApiClient.cpp` - API Client implementation
3. ✨ **NEW** `API_GUIDE.md` - คู่มือการใช้งาน
4. 📝 **MODIFIED** `src/HandySense.cpp` - เพิ่มการเรียกใช้ API
5. 📝 **MODIFIED** `platformio.ini` - มี HTTPClient library อยู่แล้ว

---

**🎉 สำเร็จแล้วครับ! ตอนนี้ระบบสามารถส่งข้อมูลไปยัง .NET API ของคุณพร้อมกับ NETPIE MQTT แล้วครับ**
