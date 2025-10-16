#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// API Configuration
#define API_ENABLE_DOTNET       1  // Enable/Disable .NET API
#define API_ENABLE_NETPIE       1  // Enable/Disable NETPIE MQTT

// .NET API Settings
#define DOTNET_BASE_URL         "http://203.159.93.240/minapi/v1"
#define DOTNET_API_KEY          "DD5B523CF73EF3386DB2DE4A7AEDD"
#define DOTNET_API_TIMEOUT      5000  // 5 seconds

// .NET API Endpoints
#define ENDPOINT_TELEMETRY      "/api/telemetry"
// เพิ่ม endpoints อื่นๆ ตามต้องการ เช่น:
// #define ENDPOINT_CONFIG         "/api/config"
// #define ENDPOINT_STATUS         "/api/status"
// #define ENDPOINT_ALERTS         "/api/alerts"
// #define ENDPOINT_CONTROL        "/api/control"

// Device Configuration
#define SITE_ID                 "site1"
#define ROOM_ID                 "roomA"
#define DEVICE_NAME             "ESP32"
#define USER_ID                 "2"      // User ID สำหรับ Automation API

class ApiClient {
public:
    static void init();
    static bool sendTelemetryToDotNetAPI(
        float temp_c,
        float hum_rh,
        float hum_dirt,
        float light_lux,
        float water_delta_l = 0.0,
        float energy_delta_kwh = 0.0
    );
    
    // สำหรับขยาย API อื่นๆ ในอนาคต
    static bool sendToCustomAPI(
        const char* url,
        const char* apiKey,
        const char* jsonPayload
    );
    
    // Helper function สำหรับส่งข้อมูลไปยัง endpoint ต่างๆ
    static bool sendToDotNetEndpoint(
        const char* endpoint,
        const char* jsonPayload
    );

private:
    static String buildDotNetPayload(
        float temp_c,
        float hum_rh,
        float hum_dirt,
        float light_lux,
        float water_delta_l,
        float energy_delta_kwh
    );
    static String buildFullUrl(const char* endpoint);
    static String getCurrentTimestamp();
    static int getWiFiRSSI();
};
