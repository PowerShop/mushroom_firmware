#include "SwitchApiClient.h"
#include <esp_log.h>

static const char* TAG = "SwitchAPI";

// ======================== SwitchApiClient Implementation ========================

String SwitchApiClient::buildUrl(const String& endpoint) {
    return String(SWITCH_API_BASE_URL) + endpoint;
}

String SwitchApiClient::stateToString(int state) {
    return (state == 1) ? "on" : "off";
}

int SwitchApiClient::stringToState(const String& stateStr) {
    return (stateStr == "on") ? 1 : 0;
}

int SwitchApiClient::sendRequest(const String& method, const String& endpoint, 
                                 const String& payload, String* response) {
    if (!WiFi.isConnected()) {
        ESP_LOGW(TAG, "WiFi not connected");
        return -1;
    }

    HTTPClient http;
    String url = buildUrl(endpoint);
    
    ESP_LOGD(TAG, "Request: %s %s", method.c_str(), url.c_str());
    if (payload.length() > 0) {
        ESP_LOGV(TAG, "Payload: %s", payload.c_str());
    }

    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-API-KEY", SWITCH_API_KEY);
    http.setTimeout(5000); // 5 seconds timeout

    int httpCode = -1;
    
    if (method == "GET") {
        httpCode = http.GET();
    } else if (method == "PUT") {
        httpCode = http.PUT(payload);
    } else if (method == "POST") {
        httpCode = http.POST(payload);
    } else if (method == "PATCH") {
        httpCode = http.PATCH(payload);
    }

    if (httpCode > 0) {
        ESP_LOGD(TAG, "HTTP Response code: %d", httpCode);
        if (response) {
            *response = http.getString();
            ESP_LOGV(TAG, "Response: %s", response->c_str());
        }
    } else {
        ESP_LOGE(TAG, "HTTP Request failed: %s", http.errorToString(httpCode).c_str());
    }

    http.end();
    return httpCode;
}

bool SwitchApiClient::getAllSwitchStates(int* states) {
    if (!API_ENABLE_SWITCH || !states) {
        return false;
    }

    String response;
    int httpCode = sendRequest("GET", SWITCH_API_ENDPOINT, "", &response);

    if (httpCode == 200) {
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, response);

        if (error) {
            ESP_LOGE(TAG, "JSON parsing failed: %s", error.c_str());
            return false;
        }

        if (doc["success"] == true && doc["data"].is<JsonArray>()) {
            JsonArray data = doc["data"].as<JsonArray>();
            
            // Parse all 4 switches
            for (JsonObject item : data) {
                int id = item["id"];
                String state = item["state"];
                
                if (id >= 1 && id <= 4) {
                    states[id - 1] = stringToState(state);
                    ESP_LOGD(TAG, "Switch %d: %s", id, state.c_str());
                }
            }
            
            return true;
        } else {
            ESP_LOGW(TAG, "API returned success=false or invalid data");
            return false;
        }
    } else if (httpCode == 401) {
        ESP_LOGE(TAG, "Unauthorized - Invalid API Key");
    }

    return false;
}

bool SwitchApiClient::getSwitchState(int id, int* state) {
    if (!API_ENABLE_SWITCH || !state || id < 1 || id > 4) {
        return false;
    }

    String endpoint = String(SWITCH_API_ENDPOINT) + "/" + String(id);
    String response;
    int httpCode = sendRequest("GET", endpoint, "", &response);

    if (httpCode == 200) {
        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, response);

        if (error) {
            ESP_LOGE(TAG, "JSON parsing failed: %s", error.c_str());
            return false;
        }

        if (doc["success"] == true) {
            String stateStr = doc["data"]["state"];
            *state = stringToState(stateStr);
            ESP_LOGD(TAG, "Switch %d state: %s", id, stateStr.c_str());
            return true;
        }
    }

    return false;
}

bool SwitchApiClient::updateSwitchState(int id, int state) {
    if (!API_ENABLE_SWITCH || id < 1 || id > 4) {
        return false;
    }

    // Build JSON payload
    DynamicJsonDocument doc(128);
    doc["id"] = id;
    doc["state"] = stateToString(state);

    String payload;
    serializeJson(doc, payload);

    // Send PUT request
    String endpoint = String(SWITCH_API_ENDPOINT) + "/" + String(id);
    String response;
    int httpCode = sendRequest("PUT", endpoint, payload, &response);

    if (httpCode == 200) {
        ESP_LOGI(TAG, "Switch %d updated to %s successfully", id, stateToString(state).c_str());
        return true;
    } else if (httpCode == 400) {
        ESP_LOGW(TAG, "Bad Request - Invalid state or ID mismatch");
    } else if (httpCode == 404) {
        ESP_LOGW(TAG, "Switch ID %d not found", id);
    }

    return false;
}

// ======================== SwitchManager Implementation ========================

unsigned long SwitchManager::lastPollTime = 0;
int SwitchManager::previousStates[4] = {-1, -1, -1, -1};  // -1 = uninitialized
bool SwitchManager::initialized = false;
bool SwitchManager::autoSyncEnabled = true;

void SwitchManager::begin() {
    ESP_LOGI(TAG, "Switch Manager initializing...");
    
    // Initialize previous states
    for (int i = 0; i < 4; i++) {
        previousStates[i] = -1;
    }
    
    // First sync from API
    if (syncFromAPI()) {
        ESP_LOGI(TAG, "Switch Manager initialized successfully");
        initialized = true;
    } else {
        ESP_LOGW(TAG, "Failed to sync from API during initialization");
        // Set default states
        for (int i = 0; i < 4; i++) {
            previousStates[i] = 0;
        }
        initialized = true;
    }
    
    lastPollTime = millis();
}

void SwitchManager::update() {
    if (!initialized || !autoSyncEnabled) {
        return;
    }

    unsigned long currentTime = millis();
    
    // Check if it's time to poll (every 500ms)
    if (currentTime - lastPollTime >= SWITCH_POLL_INTERVAL) {
        lastPollTime = currentTime;
        
        // Get current states from API
        int currentStates[4];
        if (SwitchApiClient::getAllSwitchStates(currentStates)) {
            // Check for changes
            for (int i = 0; i < 4; i++) {
                if (previousStates[i] != currentStates[i]) {
                    ESP_LOGI(TAG, "Switch %d state changed: %s -> %s", 
                             i + 1,
                             SwitchApiClient::stateToString(previousStates[i]).c_str(),
                             SwitchApiClient::stateToString(currentStates[i]).c_str());
                    
                    // Update previous state
                    previousStates[i] = currentStates[i];
                    
                    // TODO: Apply to hardware relay
                    // This will be integrated with existing relay control
                }
            }
        } else {
            ESP_LOGW(TAG, "Failed to poll switch states");
        }
    }
}

bool SwitchManager::forceUpdate(int switchId, int state) {
    if (!initialized || switchId < 1 || switchId > 4) {
        return false;
    }

    if (SwitchApiClient::updateSwitchState(switchId, state)) {
        previousStates[switchId - 1] = state;
        ESP_LOGI(TAG, "Force updated switch %d to %s", switchId, 
                 SwitchApiClient::stateToString(state).c_str());
        return true;
    }

    return false;
}

bool SwitchManager::syncFromAPI() {
    int states[4];
    if (SwitchApiClient::getAllSwitchStates(states)) {
        for (int i = 0; i < 4; i++) {
            previousStates[i] = states[i];
        }
        ESP_LOGI(TAG, "Synced states from API");
        return true;
    }
    return false;
}

void SwitchManager::setAutoSync(bool enable) {
    autoSyncEnabled = enable;
    ESP_LOGI(TAG, "Auto sync %s", enable ? "enabled" : "disabled");
}
