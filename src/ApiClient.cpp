#include "ApiClient.h"
#include <WiFi.h>
#include <time.h>

static const char* TAG = "ApiClient";

void ApiClient::init() {
    ESP_LOGI(TAG, "ApiClient initialized");
    ESP_LOGI(TAG, "DotNet Base URL: %s", DOTNET_BASE_URL);
    ESP_LOGI(TAG, "Available endpoints:");
    ESP_LOGI(TAG, "  - Telemetry: %s", ENDPOINT_TELEMETRY);
    // Log endpoints อื่นๆ เมื่อเพิ่มในอนาคต
}

bool ApiClient::sendTelemetryToDotNetAPI(
    float temp_c,
    float hum_rh,
    float hum_dirt,
    float light_lux,
    float water_delta_l,
    float energy_delta_kwh
) {
    if (!API_ENABLE_DOTNET) {
        ESP_LOGD(TAG, "DotNet API is disabled");
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        ESP_LOGW(TAG, "WiFi not connected, cannot send to API");
        return false;
    }

    HTTPClient http;
    bool success = false;

    // Build JSON payload
    String payload = buildDotNetPayload(temp_c, hum_rh, hum_dirt, light_lux, water_delta_l, energy_delta_kwh);

    ESP_LOGD(TAG, "Sending telemetry to DotNet API...");
    ESP_LOGV(TAG, "Payload: %s", payload.c_str());

    // Use helper function to send to telemetry endpoint
    return sendToDotNetEndpoint(ENDPOINT_TELEMETRY, payload.c_str());
}

bool ApiClient::sendToCustomAPI(
    const char* url,
    const char* apiKey,
    const char* jsonPayload
) {
    if (WiFi.status() != WL_CONNECTED) {
        ESP_LOGW(TAG, "WiFi not connected");
        return false;
    }

    HTTPClient http;
    bool success = false;

    ESP_LOGD(TAG, "Sending to Custom API: %s", url);
    ESP_LOGV(TAG, "Payload: %s", jsonPayload);

    http.begin(url);
    http.setTimeout(DOTNET_API_TIMEOUT);
    http.addHeader("Content-Type", "application/json");
    
    if (apiKey != nullptr && strlen(apiKey) > 0) {
        http.addHeader("X-API-KEY", apiKey);
    }

    int httpCode = http.POST(jsonPayload);

    if (httpCode > 0) {
        ESP_LOGI(TAG, "HTTP Response code: %d", httpCode);
        
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED || httpCode == HTTP_CODE_ACCEPTED) {
            String response = http.getString();
            ESP_LOGD(TAG, "Response: %s", response.c_str());
            success = true;
        } else {
            String response = http.getString();
            ESP_LOGW(TAG, "HTTP Error: %d, Response: %s", httpCode, response.c_str());
        }
    } else {
        ESP_LOGE(TAG, "HTTP Error: %s", http.errorToString(httpCode).c_str());
    }

    http.end();
    return success;
}

String ApiClient::buildDotNetPayload(
    float temp_c,
    float hum_rh,
    float hum_dirt,
    float light_lux,
    float water_delta_l,
    float energy_delta_kwh
) {
    // Create JSON document
    const size_t capacity = JSON_OBJECT_SIZE(12) + 220;
    DynamicJsonDocument doc(capacity);

    doc["id"] = 0;
    doc["site_id"] = SITE_ID;
    doc["room_id"] = ROOM_ID;
    doc["ts"] = getCurrentTimestamp();
    doc["temp_c"] = round(temp_c * 10) / 10.0;  // Round to 1 decimal
    doc["hum_rh"] = round(hum_rh * 10) / 10.0;  // Round to 1 decimal
    doc["hum_dirt"] = round(hum_dirt * 10) / 10.0;  // Round to 1 decimal
    doc["light_lux"] = round(light_lux * 100) / 100.0;  // Round to 2 decimals
    doc["water_delta_l"] = water_delta_l;
    doc["energy_delta_kwh"] = energy_delta_kwh;
    doc["rssi"] = getWiFiRSSI();
    doc["device"] = DEVICE_NAME;

    String output;
    serializeJson(doc, output);
    return output;
}

String ApiClient::getCurrentTimestamp() {
    struct tm timeinfo;
    char buffer[30];
    
    if (!getLocalTime(&timeinfo)) {
        ESP_LOGW(TAG, "Failed to obtain time, using epoch");
        return "1970-01-01T00:00:00.000Z";
    }

    // Format: 2025-10-11T20:23:16.033Z
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &timeinfo);
    
    String timestamp = String(buffer);
    timestamp += ".000Z";  // Add milliseconds and timezone
    
    return timestamp;
}

int ApiClient::getWiFiRSSI() {
    if (WiFi.status() == WL_CONNECTED) {
        return WiFi.RSSI();
    }
    return 0;
}

bool ApiClient::sendToDotNetEndpoint(
    const char* endpoint,
    const char* jsonPayload
) {
    if (WiFi.status() != WL_CONNECTED) {
        ESP_LOGW(TAG, "WiFi not connected");
        return false;
    }

    HTTPClient http;
    bool success = false;

    // Build full URL
    String fullUrl = buildFullUrl(endpoint);
    
    ESP_LOGD(TAG, "Sending to: %s", fullUrl.c_str());
    ESP_LOGV(TAG, "Payload: %s", jsonPayload);

    http.begin(fullUrl);
    http.setTimeout(DOTNET_API_TIMEOUT);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-API-KEY", DOTNET_API_KEY);

    int httpCode = http.POST(jsonPayload);

    if (httpCode > 0) {
        ESP_LOGI(TAG, "HTTP Response code: %d", httpCode);
        
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED || httpCode == HTTP_CODE_ACCEPTED) {
            String response = http.getString();
            ESP_LOGD(TAG, "Response: %s", response.c_str());
            success = true;
        } else {
            String response = http.getString();
            ESP_LOGW(TAG, "HTTP Error: %d, Response: %s", httpCode, response.c_str());
        }
    } else {
        ESP_LOGE(TAG, "HTTP Error: %s", http.errorToString(httpCode).c_str());
    }

    http.end();
    return success;
}

String ApiClient::buildFullUrl(const char* endpoint) {
    String fullUrl = String(DOTNET_BASE_URL);
    fullUrl += String(endpoint);
    return fullUrl;
}
