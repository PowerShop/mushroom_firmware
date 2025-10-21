
#pragma once

#include <stdint.h> // <-- Add this line for uint8_t
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ===================================================================
// Automation API Client for ESP32
// Three-Way Sync: ESP32 â†” API â†” MQTT
// ===================================================================

// Configuration
#define AUTOMATION_API_ENABLE       1  // à¸›à¸´à¸”à¹ƒà¸Šà¹‰à¸‡à¸²à¸™à¸£à¸°à¸šà¸šà¸­à¸±à¸•à¹‚à¸™à¸¡à¸±à¸•à¸´à¸Šà¸±à¹ˆà¸§à¸„à¸£à¸²à¸§
#define AUTOMATION_SYNC_INTERVAL    10000   // 10 seconds (Sync à¸—à¸¸à¸ 10 à¸§à¸´à¸™à¸²à¸—à¸µ)
#define TIMER_CHECK_INTERVAL        10000   // 10 seconds (à¹€à¸Šà¹‡à¸„à¸—à¸¸à¸ 10 à¸§à¸´à¸™à¸²à¸—à¸µ - à¹à¸¡à¹ˆà¸™à¸¢à¸³à¸‚à¸¶à¹‰à¸™!)
#define SENSOR_CHECK_INTERVAL       10000   // 10 seconds

// API Endpoints
#define ENDPOINT_AUTOMATION_SYNC        "/api/automation/sync"
#define ENDPOINT_AUTOMATION_TIMERS      "/api/automation/timers"
#define ENDPOINT_AUTOMATION_SENSORS     "/api/automation/sensors"
#define ENDPOINT_AUTOMATION_STATUS      "/api/automation/status"
#define ENDPOINT_AUTOMATION_RELAY_TRIGGER "/api/automation/relay"
#define ENDPOINT_AUTOMATION_LOGS        "/api/automation/logs"

// Timer Structure (ESP32 local storage)
struct AutomationTimer {
    uint8_t relay_id;       // 0-3
    uint8_t timer_id;       // 0-2
    bool enabled;
    bool days[7];           // Mon-Sun (0=Monday, 6=Sunday)
    uint16_t time_on;       // Minutes from midnight (0-1439)
    uint16_t time_off;      // Minutes from midnight (0-1439)
    char description[64];
};

struct AutomationSensor {
    uint8_t relay_id;
    char sensor_type[20];
    bool enabled;
    float min_value;
    float max_value;
    char control_mode[20];
    float hysteresis;
    char action[10];
};

struct AutomationStatus {
    uint8_t relay_id;
    bool current_state;
    char control_mode[20];
    bool timer_active;
    bool sensor_active;
    bool manual_override;
    unsigned long override_until;
};

class AutomationApiClient {
public:
    static bool isAnyAutomationActive();
    static void init();
    static bool syncFromAPI();
    static bool confirmSync(const char* syncToken, int* timerIds, int timerCount, int* sensorIds, int sensorCount);
    static bool getTimers();
    static int getLocalTimerCount();
    static AutomationTimer* getLocalTimer(int relay_id, int timer_id);
    static bool isTimerActive(int relay_id, int timer_id, int current_minutes, int day_of_week);
    static bool getSensors();
    static int getLocalSensorCount();
    static AutomationSensor* getLocalSensor(int relay_id, const char* sensor_type);
    static bool checkSensorTrigger(int relay_id, const char* sensor_type, float current_value, bool* should_turn_on, String* action_on_trigger);
    static bool triggerRelay(
        int relay_id,
        bool turn_on,
        const char* control_mode,
        const char* trigger_type,
        int timer_id = -1,
        float trigger_value = 0.0,
        float threshold_value = 0.0,
        const char* message = nullptr
    );
    static bool controlRelay(int relay_id, bool turn_on, const char* reason = nullptr);
    static bool updateStatus(
        int relay_id,
        bool current_state,
        const char* control_mode,
        bool timer_active,
        bool sensor_active,
        const char* trigger_type = nullptr
    );
    static bool getStatus();
    static AutomationStatus* getLocalStatus(int relay_id);
    static bool setManualOverride(int relay_id, bool turn_on, int duration_minutes = 30, const char* reason = nullptr);
    static bool cancelOverride(int relay_id);
    static bool isOverrideActive(int relay_id);
    static bool logEvent(
        int relay_id,
        const char* event_type,
        const char* event_source,
        bool old_state,
        bool new_state,
        int timer_id = -1,
        float trigger_value = 0.0,
        const char* message = nullptr
    );
    static bool checkActiveTimer(int relay_id, int current_minutes, int day_of_week, bool* is_active);
    static bool checkSensorAPI(int relay_id, const char* sensor_type, float current_value, bool* should_trigger);
    static int timeStringToMinutes(const char* time_str);
    static void minutesToTimeString(int minutes, char* output);
    static int getDayOfWeek(struct tm* timeinfo);
    static void clearLocalCache();
private:
    static String buildFullUrl(const char* endpoint);
    static bool sendGetRequest(const char* endpoint, String& response);
    static bool sendPostRequest(const char* endpoint, const char* payload, String& response);
    static AutomationTimer local_timers[12];
    static int local_timer_count;
    static AutomationSensor local_sensors[16];
    static int local_sensor_count;
    static AutomationStatus local_status[4];
    static unsigned long last_sync_time;
    static char sync_token[32];
};

// ===================================================================
// Global Helper Macros
// ===================================================================
#define RELAY_ID_TO_SWITCH_ID(relay_id) ((relay_id) + 1)
#define SWITCH_ID_TO_RELAY_ID(switch_id) ((switch_id) - 1)

#define IS_TIMER_DISABLED(time_on, time_off) ((time_on) >= 3000 && (time_off) >= 3000)

#define LOG_AUTOMATION(tag, format, ...) ESP_LOGI(tag, "[AUTO] " format, ##__VA_ARGS__)
#define LOG_AUTOMATION_ERROR(tag, format, ...) ESP_LOGE(tag, "[AUTO] " format, ##__VA_ARGS__)


