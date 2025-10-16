# API Configuration Guide

## การใช้งาน API Client

### 1. เปิด/ปิดการใช้งาน API

แก้ไขในไฟล์ `ApiClient.h`:

```cpp
#define API_ENABLE_DOTNET       1  // 1 = เปิด, 0 = ปิด
#define API_ENABLE_NETPIE       1  // 1 = เปิด, 0 = ปิด
```

### 2. ตั้งค่า Site ID และ Room ID

แก้ไขในไฟล์ `ApiClient.h`:

```cpp
#define SITE_ID                 "site1"
#define ROOM_ID                 "roomA"
```

### 3. เพิ่ม API ใหม่

#### ตัวอย่างที่ 1: ThingSpeak API

**ใน ApiClient.h เพิ่ม:**
```cpp
#define API_ENABLE_THINGSPEAK   1
#define THINGSPEAK_API_URL      "https://api.thingspeak.com/update"
#define THINGSPEAK_API_KEY      "YOUR_WRITE_API_KEY"

class ApiClient {
public:
    // ...existing code...
    
    static bool sendToThingSpeak(
        float temp_c,
        float hum_rh,
        float light_lux
    );
};
```

**ใน ApiClient.cpp เพิ่ม:**
```cpp
bool ApiClient::sendToThingSpeak(
    float temp_c,
    float hum_rh,
    float light_lux
) {
    if (!API_ENABLE_THINGSPEAK) {
        return false;
    }

    String url = String(THINGSPEAK_API_URL) + "?api_key=" + THINGSPEAK_API_KEY;
    url += "&field1=" + String(temp_c);
    url += "&field2=" + String(hum_rh);
    url += "&field3=" + String(light_lux);

    HTTPClient http;
    http.begin(url);
    int httpCode = http.GET();
    http.end();

    return (httpCode == HTTP_CODE_OK);
}
```

**ใน HandySense.cpp ใช้งาน:**
```cpp
static void UpdateData_To_Server() {
    // ...existing code...
    
    #if API_ENABLE_THINGSPEAK
    ApiClient::sendToThingSpeak(temp, humidity, lux_44009);
    #endif
}
```

#### ตัวอย่างที่ 2: Custom REST API with Bearer Token

**ใน ApiClient.h เพิ่ม:**
```cpp
#define API_ENABLE_CUSTOM       1
#define CUSTOM_API_URL          "https://your-api.com/v1/data"
#define CUSTOM_BEARER_TOKEN     "your_bearer_token_here"

class ApiClient {
public:
    // ...existing code...
    
    static bool sendToCustomAPIWithBearer(
        const char* url,
        const char* bearerToken,
        const char* jsonPayload
    );
};
```

**ใน ApiClient.cpp เพิ่ม:**
```cpp
bool ApiClient::sendToCustomAPIWithBearer(
    const char* url,
    const char* bearerToken,
    const char* jsonPayload
) {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    HTTPClient http;
    http.begin(url);
    http.setTimeout(5000);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + bearerToken);

    int httpCode = http.POST(jsonPayload);
    bool success = (httpCode == HTTP_CODE_OK || 
                   httpCode == HTTP_CODE_CREATED || 
                   httpCode == HTTP_CODE_ACCEPTED);

    http.end();
    return success;
}
```

#### ตัวอย่างที่ 3: InfluxDB API

**ใน ApiClient.h เพิ่ม:**
```cpp
#define API_ENABLE_INFLUXDB     1
#define INFLUXDB_URL            "http://influxdb-server:8086/write?db=mydb"
#define INFLUXDB_TOKEN          "your_token"

class ApiClient {
public:
    // ...existing code...
    
    static bool sendToInfluxDB(
        float temp_c,
        float hum_rh,
        float light_lux
    );
};
```

**ใน ApiClient.cpp เพิ่ม:**
```cpp
bool ApiClient::sendToInfluxDB(
    float temp_c,
    float hum_rh,
    float light_lux
) {
    if (!API_ENABLE_INFLUXDB) {
        return false;
    }

    // InfluxDB Line Protocol
    String payload = "sensors,site=" + String(SITE_ID) + ",room=" + String(ROOM_ID);
    payload += " temperature=" + String(temp_c);
    payload += ",humidity=" + String(hum_rh);
    payload += ",light=" + String(light_lux);

    HTTPClient http;
    http.begin(INFLUXDB_URL);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    http.addHeader("Authorization", String("Token ") + INFLUXDB_TOKEN);

    int httpCode = http.POST(payload);
    http.end();

    return (httpCode == HTTP_CODE_NO_CONTENT);
}
```

## การตั้งค่าเพิ่มเติม

### ปรับความถี่การส่งข้อมูล

ในไฟล์ `HandySense.cpp`:

```cpp
// ส่งทุก 2 นาที (ปัจจุบัน)
const unsigned long eventInterval_publishData = 2 * 60 * 1000;

// ส่งทุก 1 นาที
const unsigned long eventInterval_publishData = 1 * 60 * 1000;

// ส่งทุก 30 วินาที
const unsigned long eventInterval_publishData = 30 * 1000;
```

### ปรับเกณฑ์การส่งข้อมูลทันที

```cpp
float difference_soil = 20.00;  // ความชื้นดินเปลี่ยน ±20%
float difference_temp = 4.00;   // อุณหภูมิเปลี่ยน ±4°C

// ถ้าต้องการให้ sensitive มากขึ้น
float difference_soil = 10.00;  // ความชื้นดินเปลี่ยน ±10%
float difference_temp = 2.00;   // อุณหภูมิเปลี่ยน ±2°C
```

## การ Debug

เปิด Log Level ในไฟล์ `platformio.ini`:

```ini
[env:debug]
build_flags = 
  ${common.build_flags}
  -DCORE_DEBUG_LEVEL=5  ; 0=None, 1=Error, 2=Warn, 3=Info, 4=Debug, 5=Verbose
```

ดู Log ผ่าน Serial Monitor:
- บรรทัดที่ส่ง API: `[V][ApiClient] Sending to DotNet API...`
- Response code: `[I][ApiClient] HTTP Response code: 200`
- Error: `[E][ApiClient] HTTP Error: ...`
