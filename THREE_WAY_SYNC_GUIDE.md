# 🔄 Three-Way Sync Integration Guide

ESP32 ↔ .NET API ↔ MQTT

## 📋 สารบัญ

1. [ภาพรวม](#-ภาพรวม)
2. [การติดตั้ง](#-การติดตั้ง)
3. [การใช้งานพื้นฐาน](#-การใช้งานพื้นฐาน)
4. [Timer-based Control](#-timer-based-control)
5. [Sensor-based Control](#-sensor-based-control)
6. [Manual Override](#-manual-override)
7. [Status Management](#-status-management)
8. [Complete Example](#-complete-example)
9. [Troubleshooting](#-troubleshooting)

---

## 🎯 ภาพรวม

### Three-Way Sync Flow

```
┌─────────────────────────────────────────────────────────┐
│                  Three-Way Sync                         │
├─────────────────────────────────────────────────────────┤
│                                                         │
│        ┌──────────────┐                                │
│        │  .NET API    │                                │
│        │  (Database)  │                                │
│        └──────┬───────┘                                │
│               │                                         │
│               │ HTTP GET/POST                           │
│               │ (Sync, Control)                         │
│               ▼                                         │
│        ┌──────────────┐         ┌──────────────┐      │
│        │   ESP32-S3   │◄───────►│     MQTT     │      │
│        │ (Automation) │  PubSub │   (NETPIE)   │      │
│        └──────┬───────┘         └──────────────┘      │
│               │                                         │
│               │ GPIO Control                            │
│               ▼                                         │
│        ┌──────────────┐                                │
│        │  4x Relays   │                                │
│        └──────────────┘                                │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

### Sync Schedule

| Task | Interval | Purpose |
|------|----------|---------|
| **Full Sync** | 10 minutes | ดึงการตั้งค่า Timer/Sensor ทั้งหมด |
| **Timer Check** | 1 minute | ตรวจสอบและเปิด/ปิด Relay |
| **Sensor Check** | 5 minutes | ตรวจสอบค่า Sensor และควบคุม |
| **Status Update** | On change | ส่งสถานะเมื่อมีการเปลี่ยนแปลง |

---

## 🔧 การติดตั้ง

### 1. เพิ่ม Library

ใน `platformio.ini`:

```ini
lib_deps =
    ...existing...
    ArduinoJson @ 6.21.5
    HTTPClient @ 2.0.0
```

### 2. Include Headers

ใน `HandySense.cpp` หรือ `main.cpp`:

```cpp
#include "AutomationApiClient.h"
```

### 3. Initialize

```cpp
void setup() {
    // ... WiFi, NTP, etc ...
    
    // Initialize Automation API Client
    AutomationApiClient::init();
    
    // Sync on startup
    AutomationApiClient::syncFromAPI();
}
```

---

## 📖 การใช้งานพื้นฐาน

### Full Sync (เมื่อเริ่มต้น)

```cpp
void syncAutomation() {
    ESP_LOGI(TAG, "Syncing automation from API...");
    
    if (AutomationApiClient::syncFromAPI()) {
        int timerCount = AutomationApiClient::getLocalTimerCount();
        int sensorCount = AutomationApiClient::getLocalSensorCount();
        
        ESP_LOGI(TAG, "Sync complete: %d timers, %d sensors", 
                 timerCount, sensorCount);
    } else {
        ESP_LOGE(TAG, "Sync failed!");
    }
}
```

### Periodic Sync in Loop

```cpp
void loop() {
    static unsigned long lastSync = 0;
    
    // Sync every 10 minutes
    if (millis() - lastSync > AUTOMATION_SYNC_INTERVAL) {
        AutomationApiClient::syncFromAPI();
        lastSync = millis();
    }
}
```

---

## ⏰ Timer-based Control

### ตรวจสอบและเปิด/ปิด Relay ตาม Timer

```cpp
void checkAndTriggerTimers() {
    // Get current time
    time_t now;
    time(&now);
    struct tm* timeinfo = localtime(&now);
    
    int currentMinutes = timeinfo->tm_hour * 60 + timeinfo->tm_min;
    int dayOfWeek = AutomationApiClient::getDayOfWeek(timeinfo);
    
    // Check all relays
    for (int relayId = 0; relayId < 4; relayId++) {
        // Skip if manual override active
        if (AutomationApiClient::isOverrideActive(relayId)) {
            continue;
        }
        
        // Check all 3 timers for this relay
        bool shouldBeOn = false;
        int activeTimerId = -1;
        
        for (int timerId = 0; timerId < 3; timerId++) {
            if (AutomationApiClient::isTimerActive(relayId, timerId, 
                                                   currentMinutes, dayOfWeek)) {
                shouldBeOn = true;
                activeTimerId = timerId;
                break;  // Found active timer
            }
        }
        
        // Get current relay state
        bool currentState = digitalRead(RELAY_PINS[relayId]);
        
        // State changed?
        if (shouldBeOn != currentState) {
            ESP_LOGI(TAG, "Timer %d-%d: Relay %d should be %s", 
                     relayId, activeTimerId, relayId, 
                     shouldBeOn ? "ON" : "OFF");
            
            // Trigger via API
            AutomationApiClient::triggerRelay(
                relayId,
                shouldBeOn,
                "timer",
                "timer",
                activeTimerId,
                0.0f,
                0.0f,
                "Timer triggered"
            );
            
            // Control hardware
            digitalWrite(RELAY_PINS[relayId], shouldBeOn ? HIGH : LOW);
            
            // Update status
            AutomationApiClient::updateStatus(
                relayId,
                shouldBeOn,
                "timer",
                true,   // timer_active
                false,  // sensor_active
                "timer"
            );
            
            // Log event
            AutomationApiClient::logEvent(
                relayId,
                shouldBeOn ? "timer_on" : "timer_off",
                "esp32",
                currentState,
                shouldBeOn,
                activeTimerId,
                0.0f,
                "Timer triggered"
            );
        }
    }
}
```

### Timer Check Loop

```cpp
void loop() {
    static unsigned long lastTimerCheck = 0;
    
    // Check every minute
    if (millis() - lastTimerCheck > TIMER_CHECK_INTERVAL) {
        checkAndTriggerTimers();
        lastTimerCheck = millis();
    }
}
```

---

## 🌡️ Sensor-based Control

### ตรวจสอบและควบคุมตามค่า Sensor

```cpp
void checkAndTriggerSensors() {
    // Read sensor values
    float temp = readTemperature();
    float soilMoisture = readSoilMoisture();
    float humidity = readHumidity();
    
    // Check Temperature Control (Relay 0)
    checkSensorControl(0, "temperature", temp);
    
    // Check Soil Moisture Control (Relay 1)
    checkSensorControl(1, "soil_moisture", soilMoisture);
    
    // Check Humidity Control (Relay 2)
    checkSensorControl(2, "humidity", humidity);
}

void checkSensorControl(int relayId, const char* sensorType, float currentValue) {
    // Skip if manual override active
    if (AutomationApiClient::isOverrideActive(relayId)) {
        return;
    }
    
    // Check if sensor should trigger
    bool shouldTurnOn = false;
    bool hasTrigger = AutomationApiClient::checkSensorTrigger(
        relayId, 
        sensorType, 
        currentValue, 
        &shouldTurnOn
    );
    
    if (!hasTrigger) {
        return;  // No sensor config for this relay
    }
    
    // Get current relay state
    bool currentState = digitalRead(RELAY_PINS[relayId]);
    
    // State changed?
    if (shouldTurnOn != currentState) {
        ESP_LOGI(TAG, "Sensor %s: %.2f → Relay %d %s", 
                 sensorType, currentValue, relayId, 
                 shouldTurnOn ? "ON" : "OFF");
        
        // Get threshold for logging
        AutomationSensor* sensor = AutomationApiClient::getLocalSensor(relayId, sensorType);
        float threshold = (sensor->control_mode[0] == 'm') ? sensor->min_value : sensor->max_value;
        
        // Build message
        char message[128];
        snprintf(message, sizeof(message), 
                 "%s: %.2f %s %.2f", 
                 sensorType, currentValue,
                 shouldTurnOn ? "<" : ">",
                 threshold);
        
        // Determine control mode
        char controlMode[20];
        snprintf(controlMode, sizeof(controlMode), "sensor_%s", sensorType);
        
        // Trigger via API
        AutomationApiClient::triggerRelay(
            relayId,
            shouldTurnOn,
            controlMode,
            "sensor",
            -1,
            currentValue,
            threshold,
            message
        );
        
        // Control hardware
        digitalWrite(RELAY_PINS[relayId], shouldTurnOn ? HIGH : LOW);
        
        // Update status
        AutomationApiClient::updateStatus(
            relayId,
            shouldTurnOn,
            controlMode,
            false,  // timer_active
            true,   // sensor_active
            "sensor"
        );
        
        // Log event
        AutomationApiClient::logEvent(
            relayId,
            shouldTurnOn ? "sensor_on" : "sensor_off",
            "esp32",
            currentState,
            shouldTurnOn,
            -1,
            currentValue,
            message
        );
    }
}
```

### Sensor Check Loop

```cpp
void loop() {
    static unsigned long lastSensorCheck = 0;
    
    // Check every 5 minutes
    if (millis() - lastSensorCheck > SENSOR_CHECK_INTERVAL) {
        checkAndTriggerSensors();
        lastSensorCheck = millis();
    }
}
```

---

## 🎛️ Manual Override

### เปิด/ปิดด้วยตัวเอง (ชั่วคราว)

```cpp
void manualControl(int relayId, bool turnOn, const char* source) {
    ESP_LOGI(TAG, "Manual control: Relay %d %s", 
             relayId, turnOn ? "ON" : "OFF");
    
    // Set override for 30 minutes
    AutomationApiClient::setManualOverride(
        relayId,
        turnOn,
        30,  // duration in minutes
        "Manual control from button"
    );
    
    // Control hardware
    digitalWrite(RELAY_PINS[relayId], turnOn ? HIGH : LOW);
    
    // Update status
    AutomationApiClient::updateStatus(
        relayId,
        turnOn,
        "manual",
        false,  // timer_active
        false,  // sensor_active
        "manual"
    );
    
    // Log event
    AutomationApiClient::logEvent(
        relayId,
        turnOn ? "manual_on" : "manual_off",
        source,
        !turnOn,
        turnOn,
        -1,
        0.0f,
        "Manual control activated"
    );
}

// Cancel override
void cancelManualControl(int relayId) {
    ESP_LOGI(TAG, "Cancel manual override: Relay %d", relayId);
    
    AutomationApiClient::cancelOverride(relayId);
    
    // Resume automation
    checkAndTriggerTimers();
    checkAndTriggerSensors();
}
```

### MQTT Manual Control Integration

```cpp
void ControlRelay_Bymanual(String topic, String message, unsigned int length) {
    String manual_message = message;
    int manual_relay = topic.substring(topic.length() - 1).toInt();
    
    bool turnOn = (manual_message == "on");
    
    // Manual control with override
    manualControl(manual_relay, turnOn, "mqtt");
    
    // Existing MQTT code...
    #if USE_SWITCH_API_CONTROL == 2
    updateSwitchStateToAPI(manual_relay, turnOn ? 1 : 0);
    check_sendData_status = 1;
    #endif
}
```

---

## 📊 Status Management

### อัพเดทสถานะเมื่อมีการเปลี่ยนแปลง

```cpp
void updateAllStatus() {
    for (int relayId = 0; relayId < 4; relayId++) {
        bool currentState = digitalRead(RELAY_PINS[relayId]);
        
        AutomationStatus* status = AutomationApiClient::getLocalStatus(relayId);
        
        AutomationApiClient::updateStatus(
            relayId,
            currentState,
            status->control_mode,
            status->timer_active,
            status->sensor_active,
            nullptr
        );
    }
}
```

### Query Status from API

```cpp
void getStatusFromAPI() {
    if (AutomationApiClient::getStatus()) {
        for (int i = 0; i < 4; i++) {
            AutomationStatus* status = AutomationApiClient::getLocalStatus(i);
            
            ESP_LOGI(TAG, "Relay %d: State=%d, Mode=%s, Override=%d", 
                     i, 
                     status->current_state,
                     status->control_mode,
                     status->manual_override);
        }
    }
}
```

---

## 🎯 Complete Example

### Main Loop Integration

```cpp
#include "AutomationApiClient.h"

const int RELAY_PINS[4] = {5, 18, 19, 21};

void setup() {
    Serial.begin(115200);
    
    // Setup relays
    for(int i = 0; i < 4; i++) {
        pinMode(RELAY_PINS[i], OUTPUT);
        digitalWrite(RELAY_PINS[i], LOW);
    }
    
    // Connect WiFi
    WiFi.begin("SSID", "PASSWORD");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
    
    // Initialize NTP
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    // Initialize Automation API
    AutomationApiClient::init();
    
    // Initial sync
    AutomationApiClient::syncFromAPI();
    
    ESP_LOGI(TAG, "Setup complete");
}

void loop() {
    static unsigned long lastSync = 0;
    static unsigned long lastTimerCheck = 0;
    static unsigned long lastSensorCheck = 0;
    
    unsigned long now = millis();
    
    // Full sync every 10 minutes
    if (now - lastSync > AUTOMATION_SYNC_INTERVAL) {
        ESP_LOGI(TAG, "Syncing...");
        AutomationApiClient::syncFromAPI();
        lastSync = now;
    }
    
    // Check timers every minute
    if (now - lastTimerCheck > TIMER_CHECK_INTERVAL) {
        ESP_LOGD(TAG, "Checking timers...");
        checkAndTriggerTimers();
        lastTimerCheck = now;
    }
    
    // Check sensors every 5 minutes
    if (now - lastSensorCheck > SENSOR_CHECK_INTERVAL) {
        ESP_LOGD(TAG, "Checking sensors...");
        checkAndTriggerSensors();
        lastSensorCheck = now;
    }
    
    // Check for manual override expiration
    for (int i = 0; i < 4; i++) {
        if (AutomationApiClient::isOverrideActive(i)) {
            AutomationStatus* status = AutomationApiClient::getLocalStatus(i);
            if (now > status->override_until) {
                ESP_LOGI(TAG, "Override expired for relay %d", i);
                cancelManualControl(i);
            }
        }
    }
    
    delay(1000);
}
```

---

## 🔍 Troubleshooting

### Debug Logging

```cpp
// Enable detailed logging
#define CORE_DEBUG_LEVEL 4  // VERBOSE

// Check sync status
ESP_LOGI(TAG, "Timers: %d", AutomationApiClient::getLocalTimerCount());
ESP_LOGI(TAG, "Sensors: %d", AutomationApiClient::getLocalSensorCount());

// Check specific timer
AutomationTimer* timer = AutomationApiClient::getLocalTimer(0, 0);
if (timer) {
    ESP_LOGI(TAG, "Timer 0-0: %s %02d:%02d-%02d:%02d", 
             timer->enabled ? "ON" : "OFF",
             timer->time_on / 60, timer->time_on % 60,
             timer->time_off / 60, timer->time_off % 60);
}

// Check specific sensor
AutomationSensor* sensor = AutomationApiClient::getLocalSensor(0, "temperature");
if (sensor) {
    ESP_LOGI(TAG, "Sensor temp: %.1f-%.1f", 
             sensor->min_value, sensor->max_value);
}
```

### Common Issues

**1. Sync ไม่สำเร็จ**
```cpp
// Check WiFi
if (WiFi.status() != WL_CONNECTED) {
    ESP_LOGE(TAG, "WiFi not connected!");
}

// Check API response
String response;
if (AutomationApiClient::sendGetRequest("/api/automation/timers", response)) {
    ESP_LOGI(TAG, "Response: %s", response.c_str());
}
```

**2. Timer ไม่ทำงาน**
```cpp
// Check time
time_t now;
time(&now);
struct tm* timeinfo = localtime(&now);
ESP_LOGI(TAG, "Time: %02d:%02d Day: %d", 
         timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_wday);

// Check timer active
int minutes = timeinfo->tm_hour * 60 + timeinfo->tm_min;
int day = AutomationApiClient::getDayOfWeek(timeinfo);
bool active = AutomationApiClient::isTimerActive(0, 0, minutes, day);
ESP_LOGI(TAG, "Timer active: %d", active);
```

**3. Sensor ไม่ทำงาน**
```cpp
// Check sensor value
float temp = readTemperature();
ESP_LOGI(TAG, "Current temp: %.2f", temp);

// Check trigger
bool shouldTurnOn = false;
AutomationApiClient::checkSensorTrigger(0, "temperature", temp, &shouldTurnOn);
ESP_LOGI(TAG, "Should turn on: %d", shouldTurnOn);
```

---

## 📝 Summary

### Checklist

- [x] เพิ่ม AutomationApiClient.h/cpp
- [x] Initialize ใน setup()
- [x] Sync เมื่อเริ่มต้น
- [x] Check Timer ทุก 1 นาที
- [x] Check Sensor ทุก 5 นาที
- [x] Full Sync ทุก 10 นาที
- [x] Update Status เมื่อมีการเปลี่ยนแปลง
- [x] Log Events
- [x] Handle Manual Override
- [x] Integration กับ MQTT

### Key Functions

| Function | Purpose |
|----------|---------|
| `syncFromAPI()` | ดึงการตั้งค่าทั้งหมด |
| `isTimerActive()` | ตรวจสอบ Timer |
| `checkSensorTrigger()` | ตรวจสอบ Sensor |
| `triggerRelay()` | สั่งเปิด/ปิด Relay |
| `updateStatus()` | อัพเดทสถานะ |
| `setManualOverride()` | สั่งงานด้วยตัวเอง |
| `logEvent()` | บันทึก Log |

---

**🎉 ตอนนี้ระบบพร้อมใช้งาน Three-Way Sync แล้ว!**

