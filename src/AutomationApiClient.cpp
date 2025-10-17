#include <stdint.h>
#include "AutomationApiClient.h"
#include "ApiClient.h"
#include <WiFi.h>
#include <time.h>

static const char* TAG = "AutomationAPI";

// ------------------ Helpers for robust string handling ------------------
static void toLowercase(char* s) {
    if (!s) return;
    for (char* p = s; *p; ++p) {
        if (*p >= 'A' && *p <= 'Z') *p = *p - 'A' + 'a';
    }
}

static void normalizeActionString(const char* src, char* dst, size_t dstSize) {
    if (!dst || dstSize == 0) return;
    if (!src || !*src) {
        strncpy(dst, "turn_on", dstSize - 1);
        dst[dstSize - 1] = '\0';
        return;
    }
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
    toLowercase(dst);
    for (char* p = dst; *p; ++p) {
        if (*p == '-' || *p == ' ') *p = '_';
    }
    if (strcmp(dst, "on") == 0 || strcmp(dst, "turnon") == 0) {
        strncpy(dst, "turn_on", dstSize - 1);
    } else if (strcmp(dst, "off") == 0 || strcmp(dst, "turnoff") == 0) {
        strncpy(dst, "turn_off", dstSize - 1);
    }
    dst[dstSize - 1] = '\0';
}

static bool isActionTurnOff(const char* action) {
    if (!action) return false;
    return (strcmp(action, "turn_off") == 0 || strcmp(action, "off") == 0);
}

static int sensorTypeToIndex(const char* sensor_type) {
    if (!sensor_type) return -1;
    if (strcmp(sensor_type, "temperature") == 0) return 0;
    if (strcmp(sensor_type, "soil_moisture") == 0) return 1;
    if (strcmp(sensor_type, "humidity") == 0) return 2;
    if (strcmp(sensor_type, "light") == 0) return 3;
    return -1;
}

// Static member initialization
AutomationTimer AutomationApiClient::local_timers[12] = {};
int AutomationApiClient::local_timer_count = 0;

AutomationSensor AutomationApiClient::local_sensors[16] = {};
int AutomationApiClient::local_sensor_count = 0;

AutomationStatus AutomationApiClient::local_status[4] = {};

unsigned long AutomationApiClient::last_sync_time = 0;
char AutomationApiClient::sync_token[32] = "";

// ===================================================================
// Initialization
// ===================================================================

void AutomationApiClient::init() {
    ESP_LOGI(TAG, "AutomationApiClient initialized");
    ESP_LOGI(TAG, "Sync Interval: %d ms", AUTOMATION_SYNC_INTERVAL);
    ESP_LOGI(TAG, "Timer Check: %d ms", TIMER_CHECK_INTERVAL);
    ESP_LOGI(TAG, "Sensor Check: %d ms", SENSOR_CHECK_INTERVAL);
    
    // Initialize status for all relays
    for(int i = 0; i < 4; i++) {
        local_status[i].relay_id = i;
        local_status[i].current_state = false;
        strcpy(local_status[i].control_mode, "off");
        local_status[i].timer_active = false;
        local_status[i].sensor_active = false;
        local_status[i].manual_override = false;
        local_status[i].override_until = 0;
    }
}

// ===================================================================
// Sync Management
// ===================================================================

bool AutomationApiClient::syncFromAPI() {
    if (!AUTOMATION_API_ENABLE) {
        ESP_LOGD(TAG, "Automation API is disabled");
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        ESP_LOGW(TAG, "WiFi not connected");
        return false;
    }

    ESP_LOGI(TAG, "Syncing automation data from API...");

    String response;
    char endpoint[128];
    snprintf(endpoint, sizeof(endpoint), "%s?userId=%s", 
             ENDPOINT_AUTOMATION_SYNC, USER_ID);

    if (!sendGetRequest(endpoint, response)) {
        ESP_LOGE(TAG, "Sync request failed");
        return false;
    }

    const size_t capacity = JSON_OBJECT_SIZE(10) + JSON_ARRAY_SIZE(20) * 2 + 2048;
    DynamicJsonDocument doc(capacity);

    DeserializationError error = deserializeJson(doc, response);
    if (error) {
        ESP_LOGE(TAG, "JSON parse error: %s", error.c_str());
        return false;
    }

    if (!doc["success"].as<bool>()) {
        ESP_LOGW(TAG, "Sync failed: %s", doc["error"]["message"].as<const char*>());
        return false;
    }

    const char* token = doc["data"]["syncToken"];
    if (token && strcmp(token, sync_token) == 0) {
        ESP_LOGI(TAG, "[AUTO] Sync token unchanged, skip reload timers/sensors");
        last_sync_time = millis();
        return true;
    }
    if (token) {
        strncpy(sync_token, token, sizeof(sync_token) - 1);
    }

    JsonArray timers = doc["data"]["timers"].as<JsonArray>();
    if (timers.size() == 0) {
        clearLocalCache();
        ESP_LOGI(TAG, "No timers from API, local cache cleared");
    }
    local_timer_count = 0;
    for (JsonObject timer : timers) {
        if (local_timer_count >= 12) break;

        AutomationTimer& t = local_timers[local_timer_count];
        // Use API-provided relayId as-is (API/web UI uses 0-based relay IDs)
        t.relay_id = timer["relayId"];
        t.timer_id = timer["timerId"];
        t.enabled = timer["enabled"];

        JsonArray days = timer["days"].as<JsonArray>();
        for (int i = 0; i < 7 && i < days.size(); i++) {
            t.days[i] = days[i].as<int>() == 1;
        }

        const char* time_on = timer["timeOn"];
        const char* time_off = timer["timeOff"];
        // Parse time strings into minutes
        t.time_on = timeStringToMinutes(time_on);
        t.time_off = timeStringToMinutes(time_off);

        local_timer_count++;
    }

    ESP_LOGI(TAG, "Loaded %d timers from API", local_timer_count);

    JsonArray sensors = doc["data"]["sensors"].as<JsonArray>();
    local_sensor_count = 0;
    for (JsonObject sensor : sensors) {
        if (local_sensor_count >= 16) break;

        AutomationSensor& s = local_sensors[local_sensor_count];
        // Use API-provided relayId as-is (API/web UI uses 0-based relay IDs)
        s.relay_id = sensor["relayId"];
        strncpy(s.sensor_type, sensor["sensorType"].as<const char*>(), sizeof(s.sensor_type) - 1);
        s.sensor_type[sizeof(s.sensor_type) - 1] = '\0';
        s.enabled = sensor["enabled"];
        s.min_value = sensor["minValue"];
        s.max_value = sensor["maxValue"];
        strncpy(s.control_mode, sensor["controlMode"].as<const char*>(), sizeof(s.control_mode) - 1);
        s.control_mode[sizeof(s.control_mode) - 1] = '\0';
        // actionOnTrigger can be 'turn_on' or 'turn_off' (default turn_on)
        const char* actionOnTrigger = sensor["actionOnTrigger"];
        normalizeActionString(actionOnTrigger, s.action, sizeof(s.action));
        s.hysteresis = sensor["hysteresis"];

        // Log enabled sensors at INFO so it's easy to spot during runtime
        if (s.enabled) {
            ESP_LOGI(TAG, "Loaded ENABLED sensor [%d] apiRelay=%d storedRelay=%d type=%s min=%.2f max=%.2f action=%s hysteresis=%.2f",
                     local_sensor_count, sensor["relayId"].as<int>(), s.relay_id, s.sensor_type, s.min_value, s.max_value, s.action, s.hysteresis);
        } else {
            ESP_LOGD(TAG, "Loaded sensor [%d] apiRelay=%d storedRelay=%d type=%s enabled=%d min=%.2f max=%.2f action=%s hysteresis=%.2f",
                     local_sensor_count, sensor["relayId"].as<int>(), s.relay_id, s.sensor_type, s.enabled, s.min_value, s.max_value, s.action, s.hysteresis);
        }
        local_sensor_count++;
    }

    ESP_LOGI(TAG, "Loaded %d sensors from API", local_sensor_count);

    last_sync_time = millis();
    return true;
}

bool AutomationApiClient::confirmSync(const char* token, int* timerIds, int timerCount, 
                                      int* sensorIds, int sensorCount) {
    const size_t capacity = JSON_OBJECT_SIZE(10) + JSON_ARRAY_SIZE(20) + 512;
    DynamicJsonDocument doc(capacity);
    
    doc["userId"] = USER_ID;  // เปลี่ยนเป็น USER_ID
    doc["siteId"] = SITE_ID;
    doc["roomId"] = ROOM_ID;
    doc["syncToken"] = token;
    
    JsonObject synced = doc.createNestedObject("syncedItems");
    JsonArray timers = synced.createNestedArray("timers");
    for (int i = 0; i < timerCount; i++) {
        timers.add(timerIds[i]);
    }
    
    JsonArray sensors = synced.createNestedArray("sensors");
    for (int i = 0; i < sensorCount; i++) {
        sensors.add(sensorIds[i]);
    }
    
    String payload;
    serializeJson(doc, payload);
    
    String response;
    return sendPostRequest("/api/automation/sync/confirm", payload.c_str(), response);
}

// ===================================================================
// Timer Management
// ===================================================================

bool AutomationApiClient::getTimers() {
    String response;
    char endpoint[128];
    snprintf(endpoint, sizeof(endpoint), "%s?userId=%s", 
             ENDPOINT_AUTOMATION_TIMERS, USER_ID);  // เปลี่ยนเป็น USER_ID
    
    return sendGetRequest(endpoint, response);
}

int AutomationApiClient::getLocalTimerCount() {
    return local_timer_count;
}

AutomationTimer* AutomationApiClient::getLocalTimer(int relay_id, int timer_id) {
    for (int i = 0; i < local_timer_count; i++) {
        if (local_timers[i].relay_id == relay_id && 
            local_timers[i].timer_id == timer_id) {
            return &local_timers[i];
        }
    }
    return nullptr;
}

bool AutomationApiClient::isTimerActive(int relay_id, int timer_id, 
                                        int current_minutes, int day_of_week) {
    AutomationTimer* timer = getLocalTimer(relay_id, timer_id);
    if (!timer || !timer->enabled) {
        return false;
    }
    
    // Check if disabled (3000 = disabled marker)
    if (IS_TIMER_DISABLED(timer->time_on, timer->time_off)) {
        return false;
    }
    
    // Check day of week (0=Monday, 6=Sunday)
    if (day_of_week < 0 || day_of_week > 6 || !timer->days[day_of_week]) {
        return false;
    }
    
    // Check time range
    return (current_minutes >= timer->time_on && current_minutes < timer->time_off);
}

// ===================================================================
// Sensor Management
// ===================================================================

bool AutomationApiClient::getSensors() {
    String response;
    char endpoint[128];
    snprintf(endpoint, sizeof(endpoint), "%s?userId=%s", 
             ENDPOINT_AUTOMATION_SENSORS, USER_ID);  // เปลี่ยนเป็น USER_ID
    
    return sendGetRequest(endpoint, response);
}

int AutomationApiClient::getLocalSensorCount() {
    return local_sensor_count;
}

AutomationSensor* AutomationApiClient::getLocalSensor(int relay_id, const char* sensor_type) {
    for (int i = 0; i < local_sensor_count; i++) {
        if (local_sensors[i].relay_id == relay_id && 
            strcmp(local_sensors[i].sensor_type, sensor_type) == 0) {
            return &local_sensors[i];
        }
    }
    return nullptr;
}

bool AutomationApiClient::checkSensorTrigger(int relay_id, const char* sensor_type,
                                             float current_value, bool* should_turn_on) {
    AutomationSensor* sensor = getLocalSensor(relay_id, sensor_type);
    if (!sensor || !sensor->enabled) {
        if (should_turn_on) *should_turn_on = false;
        return false;
    }
    // Maintain last known 'active' flag per (relay, sensorType) for hysteresis smoothing
    static bool lastActive[4][4] = {{false}}; // [relay][sensorIndex]
    int sIdx = sensorTypeToIndex(sensor_type);
    if (sIdx < 0) sIdx = 0;

    // Normalize action for robust comparison
    char normalizedAction[16];
    normalizeActionString(sensor->action, normalizedAction, sizeof(normalizedAction));

    float h = sensor->hysteresis > 0.0f ? sensor->hysteresis : 1.0f;
    bool active = false;

    if (strcmp(sensor->control_mode, "min_trigger") == 0) {
        if (current_value < sensor->min_value - h) active = true;
        else if (current_value > sensor->min_value + h) active = false;
        else active = lastActive[relay_id][sIdx];
    }
    else if (strcmp(sensor->control_mode, "max_trigger") == 0) {
        if (current_value > sensor->max_value + h) active = true;
        else if (current_value < sensor->max_value - h) active = false;
        else active = lastActive[relay_id][sIdx];
    }
    else if (strcmp(sensor->control_mode, "range") == 0) {
        if (current_value < sensor->min_value - h || current_value > sensor->max_value + h) active = true;
        else if (current_value >= sensor->min_value + h && current_value <= sensor->max_value - h) active = false;
        else active = lastActive[relay_id][sIdx];
    }

    lastActive[relay_id][sIdx] = active;

    if (!active) {
        ESP_LOGD(TAG, "Sensor inactive relay=%d type=%s value=%.2f action=%s", relay_id, sensor_type, current_value, normalizedAction);
        if (should_turn_on) *should_turn_on = false;
        return false;
    }

    bool turnOffOnTrigger = isActionTurnOff(normalizedAction);
    if (should_turn_on) *should_turn_on = turnOffOnTrigger ? false : true;
    ESP_LOGI(TAG, "Sensor ACTIVE relay=%d type=%s value=%.2f action=%s shouldTurnOn=%d", 
             relay_id, sensor_type, current_value, normalizedAction, should_turn_on ? (*should_turn_on) : -1);
    return true;
}

// ===================================================================
// Relay Control
// ===================================================================

bool AutomationApiClient::triggerRelay(int relay_id, bool turn_on, 
                                       const char* control_mode, const char* trigger_type,
                                       int timer_id, float trigger_value, 
                                       float threshold_value, const char* message) {
    const size_t capacity = JSON_OBJECT_SIZE(15) + 512;
    DynamicJsonDocument doc(capacity);
    
    doc["userId"] = USER_ID;  // เปลี่ยนเป็น USER_ID
    doc["siteId"] = SITE_ID;
    doc["roomId"] = ROOM_ID;
    doc["action"] = turn_on ? "turn_on" : "turn_off";
    doc["controlMode"] = control_mode;
    doc["triggerType"] = trigger_type;
    doc["source"] = "esp32";
    
    if (timer_id >= 0) {
        doc["timerId"] = timer_id;
    }
    
    if (trigger_value != 0.0f) {
        doc["triggerValue"] = trigger_value;
    }
    
    if (threshold_value != 0.0f) {
        doc["thresholdValue"] = threshold_value;
    }
    
    if (message) {
        doc["message"] = message;
    }
    
    String payload;
    serializeJson(doc, payload);
    
    char endpoint[128];
    snprintf(endpoint, sizeof(endpoint), "%s/%d/trigger", 
             ENDPOINT_AUTOMATION_RELAY_TRIGGER, relay_id);
    
    String response;
    bool success = sendPostRequest(endpoint, payload.c_str(), response);
    
    if (success) {
        ESP_LOGI(TAG, "Relay %d %s via %s", relay_id, 
                 turn_on ? "ON" : "OFF", trigger_type);
    }
    
    return success;
}

bool AutomationApiClient::controlRelay(int relay_id, bool turn_on, const char* reason) {
    const size_t capacity = JSON_OBJECT_SIZE(10) + 256;
    DynamicJsonDocument doc(capacity);
    
    doc["userId"] = USER_ID;  // เปลี่ยนเป็น USER_ID
    doc["action"] = turn_on ? "turn_on" : "turn_off";
    doc["source"] = "esp32";
    
    if (reason) {
        doc["reason"] = reason;
    }
    
    String payload;
    serializeJson(doc, payload);
    
    char endpoint[128];
    snprintf(endpoint, sizeof(endpoint), "%s/%d/control", 
             ENDPOINT_AUTOMATION_RELAY_TRIGGER, relay_id);
    
    String response;
    return sendPostRequest(endpoint, payload.c_str(), response);
}

// ===================================================================
// Status Management
// ===================================================================

bool AutomationApiClient::updateStatus(int relay_id, bool current_state, 
                                       const char* control_mode, bool timer_active, 
                                       bool sensor_active, const char* trigger_type) {
    const size_t capacity = JSON_OBJECT_SIZE(12) + 256;
    DynamicJsonDocument doc(capacity);
    
    doc["userId"] = USER_ID;  // เปลี่ยนเป็น USER_ID
    doc["siteId"] = SITE_ID;
    doc["roomId"] = ROOM_ID;
    doc["relayId"] = relay_id;
    doc["currentState"] = current_state;
    doc["controlMode"] = control_mode;
    doc["timerActive"] = timer_active;
    doc["sensorActive"] = sensor_active;
    
    if (trigger_type) {
        doc["triggerType"] = trigger_type;
    }
    
    String payload;
    serializeJson(doc, payload);
    
    String response;
    bool success = sendPostRequest(ENDPOINT_AUTOMATION_STATUS, payload.c_str(), response);
    
    // Update local cache
    if (success && relay_id >= 0 && relay_id < 4) {
        local_status[relay_id].current_state = current_state;
        strncpy(local_status[relay_id].control_mode, control_mode, sizeof(local_status[relay_id].control_mode) - 1);
        local_status[relay_id].timer_active = timer_active;
        local_status[relay_id].sensor_active = sensor_active;
    }
    
    return success;
}

bool AutomationApiClient::getStatus() {
    String response;
    char endpoint[128];
    snprintf(endpoint, sizeof(endpoint), "%s?userId=%s", 
             ENDPOINT_AUTOMATION_STATUS, USER_ID);  // เปลี่ยนเป็น USER_ID
    
    return sendGetRequest(endpoint, response);
}

AutomationStatus* AutomationApiClient::getLocalStatus(int relay_id) {
    if (relay_id >= 0 && relay_id < 4) {
        return &local_status[relay_id];
    }
    return nullptr;
}

// ===================================================================
// Override Management
// ===================================================================

bool AutomationApiClient::setManualOverride(int relay_id, bool turn_on, 
                                            int duration_minutes, const char* reason) {
    const size_t capacity = JSON_OBJECT_SIZE(10) + 256;
    DynamicJsonDocument doc(capacity);
    
    doc["userId"] = USER_ID;  // เปลี่ยนเป็น USER_ID
    doc["siteId"] = SITE_ID;
    doc["roomId"] = ROOM_ID;
    doc["relayId"] = relay_id;
    doc["action"] = turn_on ? "turn_on" : "turn_off";
    doc["durationMinutes"] = duration_minutes;
    
    if (reason) {
        doc["reason"] = reason;
    }
    
    String payload;
    serializeJson(doc, payload);
    
    String response;
    bool success = sendPostRequest("/api/automation/override", payload.c_str(), response);
    
    if (success && relay_id >= 0 && relay_id < 4) {
        local_status[relay_id].manual_override = true;
        local_status[relay_id].override_until = millis() + (duration_minutes * 60000UL);
    }
    
    return success;
}

bool AutomationApiClient::cancelOverride(int relay_id) {
    const size_t capacity = JSON_OBJECT_SIZE(5) + 128;
    DynamicJsonDocument doc(capacity);
    
    doc["userId"] = USER_ID;  // เปลี่ยนเป็น USER_ID
    doc["relayId"] = relay_id;
    
    String payload;
    serializeJson(doc, payload);
    
    String response;
    bool success = sendPostRequest("/api/automation/override/cancel", payload.c_str(), response);
    
    if (success && relay_id >= 0 && relay_id < 4) {
        local_status[relay_id].manual_override = false;
        local_status[relay_id].override_until = 0;
    }
    
    return success;
}

bool AutomationApiClient::isOverrideActive(int relay_id) {
    if (relay_id < 0 || relay_id >= 4) return false;
    
    if (!local_status[relay_id].manual_override) return false;
    
    // Check if override expired
    if (local_status[relay_id].override_until > 0 && 
        millis() > local_status[relay_id].override_until) {
        local_status[relay_id].manual_override = false;
        local_status[relay_id].override_until = 0;
        return false;
    }
    
    return true;
}

// ===================================================================
// Logging
// ===================================================================

bool AutomationApiClient::logEvent(int relay_id, const char* event_type, 
                                   const char* event_source, bool old_state, bool new_state,
                                   int timer_id, float trigger_value, const char* message) {
    const size_t capacity = JSON_OBJECT_SIZE(15) + 512;
    DynamicJsonDocument doc(capacity);
    
    doc["userId"] = USER_ID;  // เปลี่ยนเป็น USER_ID
    doc["siteId"] = SITE_ID;
    doc["roomId"] = ROOM_ID;
    doc["relayId"] = relay_id;
    doc["eventType"] = event_type;
    doc["eventSource"] = event_source;
    doc["oldState"] = old_state;
    doc["newState"] = new_state;
    
    if (timer_id >= 0) {
        doc["timerId"] = timer_id;
    }
    
    if (trigger_value != 0.0f) {
        doc["triggerValue"] = trigger_value;
    }
    
    if (message) {
        doc["message"] = message;
    }
    
    String payload;
    serializeJson(doc, payload);
    
    String response;
    bool success = sendPostRequest(ENDPOINT_AUTOMATION_LOGS, payload.c_str(), response);
    if (success) {
        ESP_LOGI(TAG, "logEvent sent: relay=%d type=%s source=%s old=%d new=%d", relay_id, event_type, event_source, old_state, new_state);
    } else {
        ESP_LOGW(TAG, "logEvent failed: relay=%d type=%s source=%s", relay_id, event_type, event_source);
        ESP_LOGV(TAG, "Payload: %s", payload.c_str());
        ESP_LOGV(TAG, "Response: %s", response.c_str());
    }
    return success;
}

// ===================================================================
// Query APIs
// ===================================================================

bool AutomationApiClient::checkActiveTimer(int relay_id, int current_minutes, 
                                           int day_of_week, bool* is_active) {
    char time_str[16];
    minutesToTimeString(current_minutes, time_str);
    
    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), 
             "/api/automation/timers/active?userId=%s&relayId=%d&time=%s&day=%d",
             USER_ID, relay_id, time_str, day_of_week);
    
    String response;
    if (!sendGetRequest(endpoint, response)) {
        return false;
    }
    
    DynamicJsonDocument doc(512);
    deserializeJson(doc, response);
    
    *is_active = doc["data"]["isActive"];
    return true;
}

bool AutomationApiClient::checkSensorAPI(int relay_id, const char* sensor_type, 
                                        float current_value, bool* should_trigger) {
    char endpoint[256];
    snprintf(endpoint, sizeof(endpoint), 
             "/api/automation/sensors/check?userId=%s&relayId=%d&sensorType=%s&currentValue=%.2f",
             USER_ID, relay_id, sensor_type, current_value);
    
    String response;
    if (!sendGetRequest(endpoint, response)) {
        return false;
    }
    
    DynamicJsonDocument doc(512);
    deserializeJson(doc, response);
    
    *should_trigger = doc["data"]["shouldTrigger"];
    return true;
}

// ===================================================================
// Helper Functions
// ===================================================================

int AutomationApiClient::timeStringToMinutes(const char* time_str) {
    // "08:00:00" -> 480
    int hour = 0, minute = 0;
    if (sscanf(time_str, "%d:%d", &hour, &minute) == 2) {
        return hour * 60 + minute;
    }
    return 0;
}

void AutomationApiClient::minutesToTimeString(int minutes, char* output) {
    // 480 -> "08:00:00"
    int hour = minutes / 60;
    int min = minutes % 60;
    snprintf(output, 16, "%02d:%02d:00", hour, min);
}

int AutomationApiClient::getDayOfWeek(struct tm* timeinfo) {
    // tm_wday: 0=Sunday, 1=Monday, ..., 6=Saturday
    // Convert to: 0=Monday, 1=Tuesday, ..., 6=Sunday
    int day = timeinfo->tm_wday - 1;
    if (day < 0) day = 6;  // Sunday
    return day;
}

void AutomationApiClient::clearLocalCache() {
    local_timer_count = 0;
    local_sensor_count = 0;
    memset(local_timers, 0, sizeof(local_timers));
    memset(local_sensors, 0, sizeof(local_sensors));
    ESP_LOGI(TAG, "Local cache cleared");
}

// ===================================================================
// Private Helper Functions
// ===================================================================

String AutomationApiClient::buildFullUrl(const char* endpoint) {
    String fullUrl = String(DOTNET_BASE_URL);  // จาก ApiClient.h
    fullUrl += String(endpoint);
    return fullUrl;
}

bool AutomationApiClient::sendGetRequest(const char* endpoint, String& response) {
    if (WiFi.status() != WL_CONNECTED) {
        ESP_LOGW(TAG, "WiFi not connected");
        return false;
    }

    HTTPClient http;
    String fullUrl = buildFullUrl(endpoint);
    
    ESP_LOGD(TAG, "GET %s", fullUrl.c_str());
    
    http.begin(fullUrl);
    http.setTimeout(5000);
    http.addHeader("X-API-KEY", DOTNET_API_KEY);  // จาก ApiClient.h
    
    int httpCode = http.GET();
    
    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
            response = http.getString();
            http.end();
            return true;
        } else {
            ESP_LOGW(TAG, "HTTP Error: %d", httpCode);
        }
    } else {
        ESP_LOGE(TAG, "HTTP Error: %s", http.errorToString(httpCode).c_str());
    }
    
    http.end();
    return false;
}

bool AutomationApiClient::sendPostRequest(const char* endpoint, const char* payload, String& response) {
    if (WiFi.status() != WL_CONNECTED) {
        ESP_LOGW(TAG, "WiFi not connected");
        return false;
    }

    HTTPClient http;
    String fullUrl = buildFullUrl(endpoint);
    
    ESP_LOGD(TAG, "POST %s", fullUrl.c_str());
    ESP_LOGV(TAG, "Payload: %s", payload);
    
    http.begin(fullUrl);
    http.setTimeout(5000);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-API-KEY", DOTNET_API_KEY);
    
    int httpCode = http.POST(payload);
    
    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
            response = http.getString();
            ESP_LOGD(TAG, "Response: %s", response.c_str());
            http.end();
            return true;
        } else {
            ESP_LOGW(TAG, "HTTP Error: %d", httpCode);
            response = http.getString();
            ESP_LOGW(TAG, "Response: %s", response.c_str());
        }
    } else {
        ESP_LOGE(TAG, "HTTP Error: %s", http.errorToString(httpCode).c_str());
    }
    
    http.end();
    return false;
}

// ตรวจสอบว่ามี automation (timer/sensor) อย่างน้อย 1 รายการหรือไม่
bool AutomationApiClient::isAnyAutomationActive() {
    // Only consider automation active when there is at least one enabled timer or sensor
    for (int i = 0; i < AutomationApiClient::local_timer_count; i++) {
        if (AutomationApiClient::local_timers[i].enabled) return true;
    }
    for (int i = 0; i < AutomationApiClient::local_sensor_count; i++) {
        if (AutomationApiClient::local_sensors[i].enabled) return true;
    }
    return false;
}

