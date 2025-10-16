# 🔄 MQTT to API Integration Summary

## ปัจจุบันมีการส่งข้อมูลจาก MQTT ไป API อยู่แล้ว:

### ✅ ข้อมูลที่ส่งอยู่แล้ว:

#### 1. **Switch/Relay State** (ทุกครั้งที่เปลี่ยนแปลง)
```
MQTT Topic: @private/led0-3
    ↓
ControlRelay_Bymanual() ทำงาน
    ↓
updateSwitchStateToAPI()
    ↓
PUT /api/switch/{id}
```

#### 2. **Sensor Data** (ทุก 10 วินาที)
```
Sensor Reading (temp, humidity, light, soil)
    ↓
UpdateData_To_Server()
    ↓
ApiClient::sendTelemetryToDotNetAPI()
    ↓
POST /api/telemetry
```

---

## 🆕 ข้อมูลเพิ่มเติมที่ควรส่ง:

### ❌ ยังไม่ได้ส่ง:

1. **Timer Settings** - การตั้งเวลาเปิด/ปิด
2. **Min/Max Soil** - ค่าความชื้นดิน Min/Max
3. **Min/Max Temperature** - ค่าอุณหภูมิ Min/Max

---

## คุณต้องการให้เพิ่ม API สำหรับข้อมูลไหนครับ?

### ตัวเลือก:

**A. Timer Settings API**
```json
POST /api/timer
{
  "relay_id": 1,
  "day_of_week": 1,
  "timer_index": 0,
  "time_open": 1080,  // 18:00
  "time_close": 1320  // 22:00
}
```

**B. Min/Max Settings API**
```json
POST /api/settings
{
  "relay_id": 1,
  "type": "soil",  // or "temp"
  "min_value": 30,
  "max_value": 80
}
```

**C. Device Status API**
```json
POST /api/device/status
{
  "device_id": "ESP32",
  "uptime": 86400,
  "wifi_rssi": -65,
  "free_heap": 234567,
  "last_restart": "2025-10-13T10:00:00"
}
```

---

## หรือคุณหมายถึงอย่างอื่น?

กรุณาบอกรายละเอียดเพิ่มเติมว่าต้องการให้:
1. ส่งข้อมูลอะไรไป API
2. เมื่อไหร่ควรส่ง (ทุกครั้งที่เปลี่ยนแปลง? หรือตามเวลา?)
3. Format ของ API ที่ต้องการ

ผมจะช่วยเพิ่ม API Integration ให้ทันทีครับ! 🚀
