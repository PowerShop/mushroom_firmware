#ifndef SWITCH_API_CLIENT_H
#define SWITCH_API_CLIENT_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>

// .NET Switch API Configuration
#define SWITCH_API_BASE_URL     "http://203.159.93.240/minapi/v1"
#define SWITCH_API_ENDPOINT     "/api/switch"
#define SWITCH_API_KEY          "DD5B523CF73EF3386DB2DE4A7AEDD"

// Enable/Disable Switch API
#ifndef API_ENABLE_SWITCH
#define API_ENABLE_SWITCH       1  // 1 = Enable, 0 = Disable
#endif

// Switch polling interval (milliseconds)
#define SWITCH_POLL_INTERVAL    1000  // 1 วินาที

/**
 * @brief Switch API Client สำหรับจัดการ Switch 4 ตัว
 * 
 * Features:
 * - ดึงสถานะ Switch ทั้งหมด (GET /api/switch)
 * - ดึงสถานะ Switch เฉพาะ ID (GET /api/switch/{id})
 * - อัปเดตสถานะ Switch (PUT /api/switch/{id})
 * - ตรวจสอบการเปลี่ยนแปลงและส่ง Auto Update
 */
class SwitchApiClient {
public:
    /**
     * @brief ดึงสถานะ Switch ทั้งหมด (4 ตัว)
     * @param states Array สำหรับเก็บสถานะ [4] (0=off, 1=on)
     * @return true ถ้าสำเร็จ, false ถ้าล้มเหลว
     */
    static bool getAllSwitchStates(int* states);

    /**
     * @brief ดึงสถานะ Switch เฉพาะ ID
     * @param id Switch ID (1-4)
     * @param state ตัวแปรสำหรับเก็บสถานะที่ได้ (0=off, 1=on)
     * @return true ถ้าสำเร็จ, false ถ้าล้มเหลว
     */
    static bool getSwitchState(int id, int* state);

    /**
     * @brief อัปเดตสถานะ Switch
     * @param id Switch ID (1-4)
     * @param state สถานะที่ต้องการตั้ง (0=off, 1=on)
     * @return true ถ้าสำเร็จ, false ถ้าล้มเหลว
     */
    static bool updateSwitchState(int id, int state);

    /**
     * @brief แปลงสถานะจาก int เป็น string ("on"/"off")
     * @param state 0=off, 1=on
     * @return "on" หรือ "off"
     */
    static String stateToString(int state);

    /**
     * @brief แปลงสถานะจาก string เป็น int
     * @param stateStr "on" หรือ "off"
     * @return 0=off, 1=on
     */
    static int stringToState(const String& stateStr);

private:
    /**
     * @brief สร้าง Full URL
     * @param endpoint เช่น "/api/switch" หรือ "/api/switch/1"
     * @return Full URL
     */
    static String buildUrl(const String& endpoint);

    /**
     * @brief ส่ง HTTP Request แบบ Generic
     * @param method "GET", "PUT", "POST", etc.
     * @param endpoint API endpoint
     * @param payload JSON payload (optional)
     * @param response ตัวแปรสำหรับเก็บ response
     * @return HTTP status code
     */
    static int sendRequest(const String& method, const String& endpoint, 
                          const String& payload, String* response);
};

/**
 * @brief Switch Manager สำหรับจัดการ Switch อัตโนมัติ
 * 
 * Features:
 * - Poll สถานะทุก 0.5 วินาที
 * - ตรวจสอบการเปลี่ยนแปลงและอัปเดต API
 * - Sync กับ Relay Hardware
 */
class SwitchManager {
public:
    /**
     * @brief เริ่มต้นการทำงาน
     */
    static void begin();

    /**
     * @brief Update loop - เรียกใน loop() หลัก
     */
    static void update();

    /**
     * @brief บังคับอัปเดตสถานะไปยัง API ทันที
     * @param switchId Switch ID (1-4)
     * @param state สถานะ (0=off, 1=on)
     * @return true ถ้าสำเร็จ
     */
    static bool forceUpdate(int switchId, int state);

    /**
     * @brief ดึงสถานะปัจจุบันจาก API
     * @return true ถ้าสำเร็จ
     */
    static bool syncFromAPI();

    /**
     * @brief เปิดใช้งาน/ปิดการทำงาน Auto Sync
     * @param enable true=เปิด, false=ปิด
     */
    static void setAutoSync(bool enable);

private:
    static unsigned long lastPollTime;
    static int previousStates[4];      // สถานะก่อนหน้า
    static bool initialized;
    static bool autoSyncEnabled;
};

#endif // SWITCH_API_CLIENT_H
