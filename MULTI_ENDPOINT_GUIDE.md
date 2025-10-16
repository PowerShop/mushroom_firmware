# 📡 Multi-Endpoint API Guide

## โครงสร้างการรองรับหลาย Endpoints

ระบบถูกออกแบบให้รองรับหลาย API endpoints ได้อย่างยืดหยุ่น

### 📁 ไฟล์ที่เกี่ยวข้อง

```
src/
├── ApiClient.h       # กำหนด endpoints และ configuration
└── ApiClient.cpp     # Implementation
```

---

## 🔧 การเพิ่ม Endpoint ใหม่

### ขั้นตอนที่ 1: กำหนด Endpoint ใน `ApiClient.h`

```cpp
// .NET API Endpoints
#define ENDPOINT_TELEMETRY      "/api/telemetry"
#define ENDPOINT_CONFIG         "/api/config"
#define ENDPOINT_STATUS         "/api/status"
#define ENDPOINT_ALERTS         "/api/alerts"
#define ENDPOINT_CONTROL        "/api/control"
```

### ขั้นตอนที่ 2: สร้างฟังก์ชันส่งข้อมูลใน `ApiClient.h`

```cpp
class ApiClient {
public:
    // ...existing functions...
    
    // เพิ่มฟังก์ชันใหม่สำหรับ endpoint ใหม่
    static bool sendConfig(const char* jsonPayload);
    static bool sendStatus(const char* jsonPayload);
    static bool sendAlert(const char* jsonPayload);
};
```

### ขั้นตอนที่ 3: Implement ฟังก์ชันใน `ApiClient.cpp`

```cpp
bool ApiClient::sendConfig(const char* jsonPayload) {
    if (!API_ENABLE_DOTNET) {
        return false;
    }
    
    ESP_LOGD(TAG, "Sending config to DotNet API...");
    return sendToDotNetEndpoint(ENDPOINT_CONFIG, jsonPayload);
}

bool ApiClient::sendStatus(const char* jsonPayload) {
    if (!API_ENABLE_DOTNET) {
        return false;
    }
    
    ESP_LOGD(TAG, "Sending status to DotNet API...");
    return sendToDotNetEndpoint(ENDPOINT_STATUS, jsonPayload);
}

bool ApiClient::sendAlert(const char* jsonPayload) {
    if (!API_ENABLE_DOTNET) {
        return false;
    }
    
    ESP_LOGD(TAG, "Sending alert to DotNet API...");
    return sendToDotNetEndpoint(ENDPOINT_ALERTS, jsonPayload);
}
```

---

## 💡 ตัวอย่างการใช้งาน

### 1. ส่งข้อมูล Telemetry (มีอยู่แล้ว)

```cpp
// ใน HandySense.cpp
ApiClient::sendTelemetryToDotNetAPI(
    temp,           // 25.5°C
    humidity,       // 60.2%
    lux_44009,      // 1.234 Klux
    0.0,            // water
    0.0             // energy
);

// ส่งไปยัง: http://203.159.93.240/api/telemetry
```

### 2. ส่ง Configuration

```cpp
// สร้าง JSON payload
DynamicJsonDocument doc(256);
doc["site_id"] = "site1";
doc["room_id"] = "roomA";
doc["temp_threshold"] = 30;
doc["humidity_threshold"] = 80;
doc["update_interval"] = 120;

String payload;
serializeJson(doc, payload);

// ส่งข้อมูล
ApiClient::sendConfig(payload.c_str());

// ส่งไปยัง: http://203.159.93.240/api/config
```

### 3. ส่ง Device Status

```cpp
// สร้าง JSON payload
DynamicJsonDocument doc(256);
doc["device_id"] = "ESP32";
doc["uptime"] = millis() / 1000;
doc["free_heap"] = ESP.getFreeHeap();
doc["wifi_rssi"] = WiFi.RSSI();
doc["status"] = "online";

String payload;
serializeJson(doc, payload);

// ส่งข้อมูล
ApiClient::sendStatus(payload.c_str());

// ส่งไปยัง: http://203.159.93.240/api/status
```

### 4. ส่ง Alert/Warning

```cpp
// สร้าง JSON payload
DynamicJsonDocument doc(256);
doc["site_id"] = "site1";
doc["room_id"] = "roomA";
doc["type"] = "temperature";
doc["level"] = "warning";
doc["message"] = "Temperature too high";
doc["value"] = 35.5;
doc["timestamp"] = "2025-10-12T10:30:00.000Z";

String payload;
serializeJson(doc, payload);

// ส่งข้อมูล
ApiClient::sendAlert(payload.c_str());

// ส่งไปยัง: http://203.159.93.240/api/alerts
```

### 5. ส่งคำสั่งควบคุม (Control Command Response)

```cpp
// สร้าง JSON payload
DynamicJsonDocument doc(256);
doc["device_id"] = "ESP32";
doc["command_id"] = "relay1_on";
doc["status"] = "success";
doc["executed_at"] = "2025-10-12T10:30:00.000Z";

String payload;
serializeJson(doc, payload);

// ส่งข้อมูล
bool result = ApiClient::sendToDotNetEndpoint(
    ENDPOINT_CONTROL,
    payload.c_str()
);

// ส่งไปยัง: http://203.159.93.240/api/control
```

---

## 🎯 ตัวอย่างแบบสมบูรณ์

### สร้างระบบ Alert เมื่ออุณหภูมิสูงเกิน

```cpp
// ใน HandySense.cpp - ภายใน loop หรือ sensor reading function

void checkTemperatureAlert() {
    static unsigned long lastAlertTime = 0;
    const unsigned long ALERT_INTERVAL = 5 * 60 * 1000; // ส่ง alert ทุก 5 นาที
    
    if (temp > 35.0) { // ถ้าอุณหภูมิสูงกว่า 35°C
        if (millis() - lastAlertTime > ALERT_INTERVAL) {
            // สร้าง alert payload
            DynamicJsonDocument doc(256);
            doc["site_id"] = SITE_ID;
            doc["room_id"] = ROOM_ID;
            doc["type"] = "temperature";
            doc["level"] = "critical";
            doc["message"] = "Temperature exceeded threshold";
            doc["value"] = temp;
            doc["threshold"] = 35.0;
            doc["timestamp"] = ApiClient::getCurrentTimestamp();
            
            String payload;
            serializeJson(doc, payload);
            
            // ส่ง alert
            if (ApiClient::sendAlert(payload.c_str())) {
                ESP_LOGI("Alert", "Temperature alert sent successfully");
                lastAlertTime = millis();
            }
        }
    }
}
```

---

## 📊 ตัวอย่าง API Endpoints ที่น่าสนใจ

### `/api/telemetry` - ข้อมูลเซนเซอร์ตามเวลา
```json
POST /api/telemetry
{
  "site_id": "site1",
  "room_id": "roomA",
  "ts": "2025-10-12T10:00:00.000Z",
  "temp_c": 25.5,
  "hum_rh": 60.2,
  "light_lux": 1.234
}
```

### `/api/config` - การตั้งค่าอุปกรณ์
```json
POST /api/config
{
  "site_id": "site1",
  "device_id": "ESP32",
  "settings": {
    "temp_threshold": 30,
    "update_interval": 120,
    "alert_enabled": true
  }
}
```

### `/api/status` - สถานะอุปกรณ์
```json
POST /api/status
{
  "device_id": "ESP32",
  "online": true,
  "uptime": 86400,
  "wifi_rssi": -65,
  "free_heap": 234567
}
```

### `/api/alerts` - การแจ้งเตือน
```json
POST /api/alerts
{
  "site_id": "site1",
  "type": "temperature",
  "level": "warning",
  "message": "Temperature too high",
  "value": 35.5
}
```

### `/api/control` - การควบคุมอุปกรณ์
```json
POST /api/control
{
  "command": "relay_on",
  "relay_id": 1,
  "status": "success"
}
```

### `/api/events` - เหตุการณ์สำคัญ
```json
POST /api/events
{
  "site_id": "site1",
  "event_type": "power_outage",
  "timestamp": "2025-10-12T10:00:00.000Z",
  "duration": 300
}
```

---

## 🔐 การปรับ Security

### เพิ่ม Token Authentication แยกตาม Endpoint

```cpp
// ใน ApiClient.h
#define DOTNET_API_KEY_TELEMETRY    "key_for_telemetry"
#define DOTNET_API_KEY_CONFIG       "key_for_config"
#define DOTNET_API_KEY_CONTROL      "key_for_control"

// ใน ApiClient.cpp
bool ApiClient::sendConfig(const char* jsonPayload) {
    // ใช้ key เฉพาะสำหรับ config
    http.addHeader("X-API-KEY", DOTNET_API_KEY_CONFIG);
    // ...
}
```

---

## 🚀 Best Practices

1. **แยก Endpoint ตามหน้าที่**
   - Telemetry: ข้อมูลเซนเซอร์
   - Config: การตั้งค่า
   - Status: สถานะอุปกรณ์
   - Alerts: การแจ้งเตือน

2. **ใช้ Rate Limiting**
   ```cpp
   static unsigned long lastSendTime = 0;
   const unsigned long MIN_INTERVAL = 1000; // 1 วินาที
   
   if (millis() - lastSendTime < MIN_INTERVAL) {
       return false; // ส่งบ่อยเกินไป
   }
   ```

3. **Error Handling**
   ```cpp
   if (!ApiClient::sendAlert(payload.c_str())) {
       // Retry logic หรือ queue ไว้ส่งทีหลัง
       saveToQueue(payload);
   }
   ```

4. **Logging**
   ```cpp
   ESP_LOGI(TAG, "Sending to endpoint: %s", ENDPOINT_ALERTS);
   ESP_LOGV(TAG, "Payload size: %d bytes", payload.length());
   ```

---

## 📈 สรุป Architecture

```
[ESP32-S3] 
    ↓
[ApiClient::sendToDotNetEndpoint()]
    ↓
[buildFullUrl()] → http://203.159.93.240 + /api/xxx
    ↓
[HTTP POST with X-API-KEY header]
    ↓
[.NET API Server]
    ├── /api/telemetry  → บันทึกข้อมูลเซนเซอร์
    ├── /api/config     → อัปเดตการตั้งค่า
    ├── /api/status     → อัปเดตสถานะ
    ├── /api/alerts     → บันทึกการแจ้งเตือน
    └── /api/control    → ประมวลผลคำสั่ง
```

---

**💡 ตอนนี้คุณสามารถเพิ่ม API endpoints ใหม่ๆ ได้ง่ายและรวดเร็วแล้วครับ!**
