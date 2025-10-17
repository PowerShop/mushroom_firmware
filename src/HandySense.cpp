#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <NTPClient.h>
#include <FS.h>
#include <SPIFFS.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <EEPROM.h>
#include "ArtronShop_RTC.h"
#include "Sensor.h"
#include "UI.h"
#include "PinConfigs.h"
#include "ApiClient.h"
#include "SwitchApiClient.h"
#include "AutomationApiClient.h"

// ป้องกัน loop toggle ระหว่าง sensor กับ API sync
static bool ignoreNextSync[4] = {false, false, false, false};
// **[แก้ไข]** ใช้ RelayStatus เป็นตัวแปรหลักในการจดจำสถานะของรีเลย์ (0=OFF, 1=ON)
int RelayStatus[4] = {0, 0, 0, 0};

// Forward declarations
static bool isAutomationEnabledForRelay(int relayId);
static void timmer_setting(String topic, byte *payload, unsigned int length);
static void SoilMaxMin_setting(String topic, String message, unsigned int length);
void TaskWifiStatus(void *pvParameters);
void TaskWaitSerial(void *WaitSerial);
static void sent_dataTimer(String topic, String message);
static void ControlRelay_Bytimmer();
/* relay control function removed - timer now only updates time for UI */
static void TempMaxMin_setting(String topic, String message, unsigned int length);
void ControlRelay_Bymanual(String topic, String message, unsigned int length);
static bool updateSwitchStateToAPI(int relayId, int state);
// static void syncAllRelaysToAPI();
int check_sendData_status = 0;

  // Push all relay states to Switch API (map 0-3 -> switch 1-4)
  // for (int i = 0; i < 4; i++)
  // {
  //   int state = RelayStatus[i];
  //   int switchId = RELAY_ID_TO_SWITCH_ID(i);
  //   ESP_LOGD(TAG, "syncAllRelaysToAPI: relay %d -> switch %d state=%d", i, switchId, state);
  //   updateSwitchStateToAPI(i, state);
  // }

static const char *TAG = "HandySense";

#define CONFIG_FILE "/configs.json"

// ประกาศใช้เวลาบน Internet
const char *ntpServer = "pool.ntp.org";
const char *nistTime = "time.nist.gov";
const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0;
int hourNow, minuteNow, secondNow, dayNow, monthNow, yearNow, weekdayNow;
int currentTimerNow = 0;
int dayOfWeekNow = 0;

struct tm timeinfo;
String _data; // อาจจะไม่ได้ใช้

// ประกาศตัวแปรสื่อสารกับ web App
byte STX = 02;
byte ETX = 03;
uint8_t START_PATTERN[3] = {0, 111, 222};
const size_t capacity = JSON_OBJECT_SIZE(7) + 320;
DynamicJsonDocument jsonDoc(capacity);

String mqtt_server,
    mqtt_Client,
    mqtt_password,
    mqtt_username,
    password,
    mqtt_port,
    ssid;

String client_old;

// ประกาศใช้ rtc
ArtronShop_RTC rtc(&Wire, PCF8563);

// ประกาศใช้ WiFiClient
WiFiClient espClient;
PubSubClient client(espClient);

// ประกาศใช้ WiFiUDP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
int curentTimerError = 0;

const unsigned long eventInterval = 1 * 1000;            // อ่านค่า temp และ soil sensor ทุก ๆ 1 วินาที
const unsigned long eventInterval_brightness = 6 * 1000; // อ่านค่า brightness sensor ทุก ๆ 6 วินาที
unsigned long previousTime_Temp_soil = 0;
unsigned long previousTime_brightness = 0;

// ประกาศตัวแปรกำหนดการนับเวลาเริ่มต้น
unsigned long previousTime_Update_data = 0;
const unsigned long eventInterval_publishData = 10 * 1000; // ส่งทุก 10 วินาที

float difference_soil = 20.00, // ค่าความชื้นดินแตกต่างกัน +-20 % เมื่อไรส่งค่าขึ้น Web app ทันที
    difference_temp = 4.00;    // ค่าอุณหภูมิแตกต่างกัน +- 4 C เมื่อไรส่งค่าขึ้น Web app ทันที

// ประกาศตัวแปรสำหรับเก็บค่าเซ็นเซอร์
float temp = 0;
float humidity = 0;
float lux_44009 = 0;
float soil = 0;

// Array สำหรับทำ Movie Arg. ของค่าเซ็นเซอร์ทุก ๆ ค่า
static float ma_temp[5];
static float ma_hum[5];
static float ma_soil[5];
static float ma_lux[5];

// สำหรับเก็บค่าเวลาจาก Web App
int t[20];
#define state_On_Off_relay t[0]
#define value_monday_from_Web t[1]
#define value_Tuesday_from_Web t[2]
#define value_Wednesday_from_Web t[3]
#define value_Thursday_from_Web t[4]
#define value_Friday_from_Web t[5]
#define value_Saturday_from_Web t[6]
#define value_Sunday_from_Web t[7]
#define value_hour_Open t[8]
#define value_min_Open t[9]
#define value_hour_Close t[11]
#define value_min_Close t[12]

#define OPEN 1
#define CLOSE 0

/* new PCB Red */
int relay_pin[4] = {O1_PIN, O2_PIN, O3_PIN, O4_PIN};

// **[แก้ไข]** สร้างฟังก์ชันกลางสำหรับควบคุมรีเลย์ทั้งหมดในที่เดียว
void setRelayState(int relayId, bool turnOn, const char *source)
{
  if (relayId < 0 || relayId >= 4)
    return;

  // ตรวจสอบสถานะปัจจุบันก่อนสั่งงานเสมอ
  // อ่านสถานะฮาร์ดแวร์จากพินจริง เพื่อให้สถานะอ้างอิงเป็นจริงเสมอ
  int hwLevel = digitalRead(relay_pin[relayId]);
  bool hwOn = (hwLevel == HIGH);

  // ถ้าสถานะฮาร์ดแวร์ตรงกับสิ่งที่ต้องการแล้ว ให้ซิงค์ RelayStatus และไม่สั่งซ้ำ
  if (hwOn == turnOn)
  {
    if (RelayStatus[relayId] != (turnOn ? 1 : 0)) {
      ESP_LOGW(TAG, "Relay %d status mismatch: RelayStatus=%d but hardware=%d, correcting RelayStatus", relayId, RelayStatus[relayId], hwOn ? 1 : 0);
      RelayStatus[relayId] = hwOn ? 1 : 0;
    }
    return;
  }

  // ถ้าสถานะไม่เหมือนเดิม ให้สั่งงานและอัปเดตสถานะ
  ESP_LOGI(TAG, "Source: [%s] -> Changing Relay %d to %s (hwBefore=%d)", source, relayId, turnOn ? "ON" : "OFF", hwOn ? 1 : 0);

  // Use explicit HIGH/LOW to avoid ambiguity
  digitalWrite(relay_pin[relayId], turnOn ? HIGH : LOW);
  UI_updateOutputStatus(relayId, turnOn);
  bool oldState = (RelayStatus[relayId] == 1);
  RelayStatus[relayId] = turnOn ? 1 : 0;
  check_sendData_status = 1; // ตั้งค่าสถานะเพื่อส่งข้อมูลไป MQTT

  // ส่ง log กลับไปที่ Automation API (ถ้าเปิดใช้งาน)
#if defined(AUTOMATION_API_ENABLE) && AUTOMATION_API_ENABLE
  AutomationApiClient::logEvent(relayId,
                                turnOn ? "turn_on" : "turn_off",
                                source,
                                oldState,
                                RelayStatus[relayId],
                                -1,
                                0.0f,
                                NULL);
#endif

// ถ้าใช้ API Control Mode 2 (Hybrid), ให้อัปเดตสถานะไปที่ API ด้วย
#if USE_SWITCH_API_CONTROL >= 1
  // Prevent immediate loop: ignore the next API sync for this relay
  ignoreNextSync[relayId] = true;
  // Update Switch API (map relay 0..3 -> switch 1..4)
  updateSwitchStateToAPI(relayId, RelayStatus[relayId]);
#endif
}

// **[แก้ไข]** เปลี่ยน Macro เดิมให้มาเรียกใช้ฟังก์ชันใหม่ เพื่อให้ทุกการควบคุมผ่านฟังก์ชันกลาง
#define Open_relay(j, source) setRelayState(j, true, source)
#define Close_relay(j, source) setRelayState(j, false, source)

#define connect_WifiStatusToBox -1
#define status_sht31_error -1
#define status_max44009_error -1
#define status_soil_error -1

// ตัวแปรเก็บค่าการตั้งเวลาทำงานอัตโนมัติ
unsigned int time_open[4][7][3] = {{{2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}},
                                   {{2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}},
                                   {{2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}},
                                   {{2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}}};
unsigned int time_close[4][7][3] = {{{2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}},
                                    {{2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}},
                                    {{2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}},
                                    {{2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}, {2000, 2000, 2000}}};

float Max_Soil[4], Min_Soil[4];
float Max_Temp[4], Min_Temp[4];

unsigned int status_manual[4];

TaskHandle_t WifiStatus, WaitSerial;
unsigned int oldTimer;

// ===================== Switch API Control Variables =====================
#define USE_SWITCH_API_CONTROL 2
// AUTOMATION_API_ENABLE is defined in AutomationApiClient.h - don't redefine here
// ================================================================

int lastKnownSwitchStates[4] = {0, 0, 0, 0};
// =====================================================================

// สถานะการเชื่อมต่อ wifi
#define cannotConnect 0
#define wifiConnected 1
#define serverConnected 2
#define editDeviceWifi 3
int connectWifiStatus = cannotConnect;

int check_sendData_SoilMinMax = 0;
int check_sendData_tempMinMax = 0;

#define INTERVAL_MESSAGE 600000   // 10 นาที
#define INTERVAL_MESSAGE2 1500000 // 1 ชม
unsigned long time_restart = 0;

/* --------- Callback function get data from web ---------- */
static void callback(String topic, byte *payload, unsigned int length)
{
  ESP_LOGV(TAG, "Message arrived [%s]", topic.c_str());

  String message;
  for (int i = 0; i < length; i++)
  {
    message = message + (char)payload[i];
  }

  /* ------- topic timer ------- */
  if (topic.substring(0, 14) == "@private/timer")
  {
    timmer_setting(topic, payload, length);
    sent_dataTimer(topic, message);
  }

// ========== MQTT Switch Control (ทำงานใน Mode 0 และ 2) ==========
#if USE_SWITCH_API_CONTROL != 1
  /* ------- topic manual_control relay ------- */
  else if (topic.substring(0, 12) == "@private/led")
  {
    status_manual[0] = 0;
    status_manual[1] = 0;
    status_manual[2] = 0;
    status_manual[3] = 0;
    ControlRelay_Bymanual(topic, message, length);

// ========== Hybrid Mode: ส่งสถานะไป API ด้วย ==========
#if USE_SWITCH_API_CONTROL == 2
    // ดึง Relay ID จาก topic (เช่น @private/led0 -> 0)
    int relayId = topic.substring(topic.length() - 1).toInt();
    if (relayId >= 0 && relayId <= 3)
    {
      int state = RelayStatus[relayId]; // 0 or 1
      ESP_LOGI(TAG, "MQTT changed relay %d to %d, syncing to API...", relayId, state);
      updateSwitchStateToAPI(relayId, state);
    }
#endif
    // =======================================================
  }
#endif
  // ================================================================

  /* ------- topic Soil min max ------- */
  else if (topic.substring(0, 17) == "@private/max_temp" || topic.substring(0, 17) == "@private/min_temp")
  {
    TempMaxMin_setting(topic, message, length);
  }
  /* ------- topic Temp min max ------- */
  else if (topic.substring(0, 17) == "@private/max_soil" || topic.substring(0, 17) == "@private/min_soil")
  {
    SoilMaxMin_setting(topic, message, length);
  }
  // After loading config, push current relay states to Switch API when WiFi available
  if (WiFi.status() == WL_CONNECTED)
  {
    // syncAllRelaysToAPI();
    // Sync all relay states to Switch API (MAP 0-3 -> switch 1-4)
    for (int i = 0; i < 4; i++)
    {
      int state = RelayStatus[i];
      int switchId = RELAY_ID_TO_SWITCH_ID(i);
      ESP_LOGD(TAG, "syncAllRelaysToAPI: relay %d -> switch %d state=%d", i, switchId, state);
      updateSwitchStateToAPI(i, state);
    }
  }
}

/* ----------------------- Sent Timer --------------------------- */
static void sent_dataTimer(String topic, String message)
{
  String _numberTimer = topic.substring(topic.length() - 2).c_str();
  String _payload = "{\"data\":{\"value_timer";
  _payload += _numberTimer;
  _payload += "\":\"";
  _payload += message;
  _payload += "\"}}";
  ESP_LOGV(TAG, "incoming : %s", _payload.c_str());
  client.publish("@shadow/data/update", (char *)_payload.c_str());
}

/* --------- UpdateData_To_Server --------- */
static void UpdateData_To_Server()
{
#if API_ENABLE_DOTNET
  // ถ้า humidity == -99.0 หรือ temp == 0.0 ไม่ต้องส่งค่าไป API
  if (humidity == -99.0f || temp == 0.0f)
  {
    ESP_LOGW(TAG, "Skip sending to .NET API: humidity=%.1f temp=%.1f", humidity, temp);
    return;
  }
  bool apiResult = ApiClient::sendTelemetryToDotNetAPI(
      temp,      // temp_c
      humidity,  // hum_rh
      soil,      // hum_dirt (ความชื้นในดิน)
      lux_44009, // light_lux (already in Klux from sensor)
      0.0,       // water_delta_l (waiting for sensor)
      0.0        // energy_delta_kwh (waiting for sensor)
  );

  if (apiResult)
  {
    ESP_LOGV(TAG, " Send Data Complete (.NET API) ");
  }
  else
  {
    ESP_LOGW(TAG, " Send Data Failed (.NET API) ");
  }
#endif
}

/* --------- sendStatus_RelaytoWeb --------- */
static void sendStatus_RelaytoWeb()
{
#if USE_SWITCH_API_CONTROL == 0 || USE_SWITCH_API_CONTROL == 2
  // ทำงานใน Mode 0 (MQTT Only) และ Mode 2 (Hybrid)
  String _payload;
  if (check_sendData_status == 1)
  {
    _payload = "{\"data\": {\"led0\":\"" + String(RelayStatus[0]) +
               "\",\"led1\":\"" + String(RelayStatus[1]) +
               "\",\"led2\":\"" + String(RelayStatus[2]) +
               "\",\"led3\":\"" + String(RelayStatus[3]) + "\"}}";
    ESP_LOGV(TAG, "_payload : %s", _payload.c_str());
    if (client.publish("@shadow/data/update", _payload.c_str()))
    {
      check_sendData_status = 0;
      ESP_LOGV(TAG, "Send Complete Relay ");
    }
  }
#endif
}

/* --------- syncSwitchStatesFromAPI --------- */
// ดึงสถานะ Switch จาก API และอัปเดต Relay Hardware
static void syncSwitchStatesFromAPI()
{
#if USE_SWITCH_API_CONTROL
  static unsigned long lastSyncTime = 0;
  unsigned long currentTime = millis();
  // ดึงสถานะทุก 500ms (0.5 วินาที)
  if (currentTime - lastSyncTime >= SWITCH_POLL_INTERVAL)
  {
    lastSyncTime = currentTime;
    int apiStates[4];
    if (SwitchApiClient::getAllSwitchStates(apiStates))
    {
      for (int i = 0; i < 4; i++)
      {
        if (ignoreNextSync[i])
        {
          ESP_LOGD(TAG, "[LOOP-PROTECT] Ignore API sync for relay %d", i);
          ignoreNextSync[i] = false;
          lastKnownSwitchStates[i] = apiStates[i];
          continue;
        }
        if (lastKnownSwitchStates[i] != apiStates[i])
        {
          ESP_LOGI(TAG, "Switch %d changed: %s -> %s",
                   i + 1,
                   lastKnownSwitchStates[i] ? "ON" : "OFF",
                   apiStates[i] ? "ON" : "OFF");
          lastKnownSwitchStates[i] = apiStates[i];
          // เช็คว่าไม่มี automation ที่ enabled สำหรับ relay นี้ก่อน
          if (!isAutomationEnabledForRelay(i))
          {
            if (apiStates[i] == 1)
            {
              Open_relay(i, "API_SYNC");
            }
            else
            {
              Close_relay(i, "API_SYNC");
            }
          }
          else
          {
            ESP_LOGI(TAG, "Relay %d: Automation enabled, ignore API Switch command", i);
          }
        }
      }
    }
    else
    {
      ESP_LOGW(TAG, "Failed to sync switch states from API");
    }
  }
#endif
}

// Helper: ตรวจสอบว่ามี automation (timer/sensor) ที่ enabled สำหรับ relayId หรือไม่
static bool isAutomationEnabledForRelay(int relayId)
{
  // เช็ค timer
  for (int timerId = 0; timerId < 3; timerId++)
  {
    AutomationTimer *timer = AutomationApiClient::getLocalTimer(relayId, timerId);
    if (timer && timer->enabled)
    {
      return true;
    }
  }
  // เช็ค sensor
  const char *sensorTypes[] = {"temperature", "soil_moisture", "humidity", "light"};
  for (int i = 0; i < 4; i++)
  {
    AutomationSensor *sensor = AutomationApiClient::getLocalSensor(relayId, sensorTypes[i]);
    if (sensor && sensor->enabled)
    {
      return true;
    }
  }
  return false;
}

/* --------- updateSwitchStateToAPI --------- */
// Push a single relay state to the Switch API (maps relay 0-3 -> switch 1-4).
static bool updateSwitchStateToAPI(int relayId, int state)
{
#if USE_SWITCH_API_CONTROL >= 1
  int mappedSwitchId = RELAY_ID_TO_SWITCH_ID(relayId);
  ESP_LOGD(TAG, "Syncing relay %d -> switch %d state=%d", relayId, mappedSwitchId, state);
  if (SwitchApiClient::updateSwitchState(mappedSwitchId, state))
  {
    lastKnownSwitchStates[mappedSwitchId - 1] = state;
    ESP_LOGI(TAG, "Updated switch %d to API: %s", mappedSwitchId, state ? "ON" : "OFF");
    return true;
  }
  // retry once
  ESP_LOGW(TAG, "Retry update switch %d", mappedSwitchId);
  if (SwitchApiClient::updateSwitchState(mappedSwitchId, state))
  {
    lastKnownSwitchStates[mappedSwitchId - 1] = state;
    ESP_LOGI(TAG, "Updated switch %d to API on retry", mappedSwitchId);
    return true;
  }
  ESP_LOGW(TAG, "Failed to update switch %d to API after retry", mappedSwitchId);
  return false;
#else
  (void)relayId; (void)state; return false;
#endif
}
/* --------- Respone soilMinMax toWeb --------- */
static void send_soilMinMax()
{
  String soil_payload;
  if (check_sendData_SoilMinMax == 1)
  {
    soil_payload = "{\"data\": {\"min_soil0\":" + String(Min_Soil[0]) + ",\"max_soil0\":" + String(Max_Soil[0]) +
                   ",\"min_soil1\":" + String(Min_Soil[1]) + ",\"max_soil1\":" + String(Max_Soil[1]) +
                   ",\"min_soil2\":" + String(Min_Soil[2]) + ",\"max_soil2\":" + String(Max_Soil[2]) +
                   ",\"min_soil3\":" + String(Min_Soil[3]) + ",\"max_soil3\":" + String(Max_Soil[3]) + "}}";
    ESP_LOGV(TAG, "_payload : %s", soil_payload.c_str());
    if (client.publish("@shadow/data/update", soil_payload.c_str()))
    {
      check_sendData_SoilMinMax = 0;
      ESP_LOGV(TAG, "Send Complete min max ");
    }
  }
}

/* --------- Respone tempMinMax toWeb --------- */
static void send_tempMinMax()
{
  String temp_payload;
  if (check_sendData_tempMinMax == 1)
  {
    temp_payload = "{\"data\": {\"min_temp0\":" + String(Min_Temp[0]) + ",\"max_temp0\":" + String(Max_Temp[0]) +
                   ",\"min_temp1\":" + String(Min_Temp[1]) + ",\"max_temp1\":" + String(Max_Temp[1]) +
                   ",\"min_temp2\":" + String(Min_Temp[2]) + ",\"max_temp2\":" + String(Max_Temp[2]) +
                   ",\"min_temp3\":" + String(Min_Temp[3]) + ",\"max_temp3\":" + String(Max_Temp[3]) + "}}";
    ESP_LOGV(TAG, "_payload : %s", temp_payload.c_str());
    if (client.publish("@shadow/data/update", temp_payload.c_str()))
    {
      check_sendData_tempMinMax = 0;
    }
  }
}

/* ----------------------- Setting Timer --------------------------- */
void timmer_setting(String topic, byte *payload, unsigned int length)
{
  int timer, relay;
  char *str;
  unsigned int count = 0;
  char message_time[50];
  timer = topic.substring(topic.length() - 1).toInt();
  relay = topic.substring(topic.length() - 2, topic.length() - 1).toInt();
  ESP_LOGV(TAG, "Timer: %d\tRelay: %d", timer, relay);
  for (int i = 0; i < length; i++)
  {
    message_time[i] = (char)payload[i];
  }
  ESP_LOGV(TAG, "%s", message_time);
  str = strtok(message_time, " ,,,:");
  while (str != NULL)
  {
    t[count] = atoi(str);
    count++;
    str = strtok(NULL, " ,,,:");
  }
  if (state_On_Off_relay == 1)
  {
    for (int k = 0; k < 7; k++)
    {
      if (t[k + 1] == 1)
      {
        time_open[relay][k][timer] = (value_hour_Open * 60) + value_min_Open;
        time_close[relay][k][timer] = (value_hour_Close * 60) + value_min_Close;
      }
      else
      {
        time_open[relay][k][timer] = 3000;
        time_close[relay][k][timer] = 3000;
      }
      int address = ((((relay * 7 * 3) + (k * 3) + timer) * 2) * 2) + 2100;
      EEPROM.write(address, time_open[relay][k][timer] / 256);
      EEPROM.write(address + 1, time_open[relay][k][timer] % 256);
      EEPROM.write(address + 2, time_close[relay][k][timer] / 256);
      EEPROM.write(address + 3, time_close[relay][k][timer] % 256);
      EEPROM.commit();
      ESP_LOGV(TAG, "time_open: %d\ntime_close: %d", time_open[relay][k][timer], time_close[relay][k][timer]);
    }
  }
  else if (state_On_Off_relay == 0)
  {
    for (int k = 0; k < 7; k++)
    {
      time_open[relay][k][timer] = 3000;
      time_close[relay][k][timer] = 3000;
      int address = ((((relay * 7 * 3) + (k * 3) + timer) * 2) * 2) + 2100;
      EEPROM.write(address, time_open[relay][k][timer] / 256);
      EEPROM.write(address + 1, time_open[relay][k][timer] % 256);
      EEPROM.write(address + 2, time_close[relay][k][timer] / 256);
      EEPROM.write(address + 3, time_close[relay][k][timer] % 256);
      EEPROM.commit();
      ESP_LOGV(TAG, "time_open: %d\ntime_close: %d", time_open[relay][k][timer], time_close[relay][k][timer]);
    }
  }
  else
  {
    ESP_LOGV(TAG, "Not enabled timer, Day !!!");
  }
  UI_updateTimer();
}

void sendUpdateTimerToServer(uint8_t relay, uint8_t timer)
{
  uint8_t enable = 0;
  uint8_t day_enable[7];
  uint8_t time_on_hour = 0, time_on_min = 0, time_off_hour = 0, time_off_min = 0;
  for (int k = 0; k < 7; k++)
  {
    day_enable[k] = ((time_open[relay][k][timer] <= ((23 * 60) + 59)) && (time_close[relay][k][timer] <= ((23 * 60) + 59))) ? 1 : 0;
    if (day_enable[k])
    {
      enable = 1;
      time_on_hour = time_open[relay][k][timer] / 60;
      time_on_min = time_open[relay][k][timer] % 60;
      time_off_hour = time_close[relay][k][timer] / 60;
      time_off_min = time_close[relay][k][timer] % 60;
    }
  }

  char payload[64];
  memset(payload, 0, sizeof(payload));
  sprintf(payload, "{\"data\":{\"value_timer%d%d\":\"%d,%d,%d,%d,%d,%d,%d,%d,%02d:%02d:00,%02d:%02d:00\"}}",
          relay, timer, enable, day_enable[0], day_enable[1], day_enable[2], day_enable[3], day_enable[4], day_enable[5], day_enable[6], time_on_hour, time_on_min, time_off_hour, time_off_min);
  ESP_LOGV(TAG, "update shadow : %s", payload);
  client.publish("@shadow/data/update", payload);
}

void HandySense_updateTimeInTimer(uint8_t relay, uint8_t timer, bool isTimeOn, uint16_t time)
{
  bool found_enable = false;
  for (int k = 0; k < 7; k++)
  {
    unsigned int on_time = time_open[relay][k][timer];
    unsigned int off_time = time_close[relay][k][timer];
    if ((on_time < ((23 * 60) + 59)) && off_time < ((23 * 60) + 59))
    {
      if (isTimeOn)
      {
        time_open[relay][k][timer] = time;
        if (time_close[relay][k][timer] > ((23 * 60) + 59))
        {
          time_close[relay][k][timer] = time + 1;
        }
      }
      else
      {
        if (time_open[relay][k][timer] > ((23 * 60) + 59))
        {
          time_open[relay][k][timer] = time;
          time_close[relay][k][timer] = time + 1;
        }
        else
        {
          time_close[relay][k][timer] = time;
        }
      }

      int address = ((((relay * 7 * 3) + (k * 3) + timer) * 2) * 2) + 2100;
      EEPROM.write(address, time_open[relay][k][timer] / 256);
      EEPROM.write(address + 1, time_open[relay][k][timer] % 256);
      EEPROM.write(address + 2, time_close[relay][k][timer] / 256);
      EEPROM.write(address + 3, time_close[relay][k][timer] % 256);
      EEPROM.commit();
      ESP_LOGV(TAG, "time_open: %d\ntime_close: %d", time_open[relay][k][timer], time_close[relay][k][timer]);

      found_enable = true;
    }
  }

  if (!found_enable)
  {
    // Set all timer to same time
    for (int k = 0; k < 7; k++)
    {
      if (isTimeOn)
      {
        time_open[relay][k][timer] = time;
        if ((time_close[relay][k][timer] > ((23 * 60) + 59)) || (time_close[relay][k][timer] <= time))
        {
          time_close[relay][k][timer] = time + 1;
        }
      }
      else
      {
        if ((time_open[relay][k][timer] > ((23 * 60) + 59)) || (time_open[relay][k][timer] >= time))
        {
          time_open[relay][k][timer] = time;
          time_close[relay][k][timer] = time + 1;
        }
        else
        {
          time_close[relay][k][timer] = time;
        }
      }

      int address = ((((relay * 7 * 3) + (k * 3) + timer) * 2) * 2) + 2100;
      EEPROM.write(address, time_open[relay][k][timer] / 256);
      EEPROM.write(address + 1, time_open[relay][k][timer] % 256);
      EEPROM.write(address + 2, time_close[relay][k][timer] / 256);
      EEPROM.write(address + 3, time_close[relay][k][timer] % 256);
      EEPROM.commit();
      ESP_LOGV(TAG, "time_open: %d\ntime_close: %d", time_open[relay][k][timer], time_close[relay][k][timer]);
    }
  }

  UI_updateTimer();
  sendUpdateTimerToServer(relay, timer);
}

void HandySense_updateDayEnableInTimer(uint8_t relay, uint8_t timer, uint8_t day, bool enable)
{
  int address = ((((relay * 7 * 3) + (day * 3) + timer) * 2) * 2) + 2100;
  EEPROM.write(address, time_open[relay][day][timer] / 256);
  EEPROM.write(address + 1, time_open[relay][day][timer] % 256);
  EEPROM.write(address + 2, time_close[relay][day][timer] / 256);
  EEPROM.write(address + 3, time_close[relay][day][timer] % 256);
  EEPROM.commit();
  ESP_LOGV(TAG, "time_open: %d\ntime_close: %d", time_open[relay][day][timer], time_close[relay][day][timer]);

  UI_updateTimer();
  sendUpdateTimerToServer(relay, timer);
}

void HandySense_updateDisableTimer(uint8_t relay, uint8_t timer)
{
  // Set all timer to same time
  for (int k = 0; k < 7; k++)
  {
    time_open[relay][k][timer] = 3000;
    time_close[relay][k][timer] = 3000;

    int address = ((((relay * 7 * 3) + (k * 3) + timer) * 2) * 2) + 2100;
    EEPROM.write(address, time_open[relay][k][timer] / 256);
    EEPROM.write(address + 1, time_open[relay][k][timer] % 256);
    EEPROM.write(address + 2, time_close[relay][k][timer] / 256);
    EEPROM.write(address + 3, time_close[relay][k][timer] % 256);
    EEPROM.commit();
    ESP_LOGV(TAG, "time_open: %d\ntime_close: %d", time_open[relay][k][timer], time_close[relay][k][timer]);
  }

  UI_updateTimer();
  sendUpdateTimerToServer(relay, timer);
}

/* ------------ Control Relay By Timmer ------------- */
static void ControlRelay_Bytimmer()
{
  // Only update time variables and globals for UI/use elsewhere.
  bool getTimeFromInternet = false;
  if (WiFi.status() == WL_CONNECTED)
  {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer, nistTime);
    if (getLocalTime(&timeinfo))
    {
      getTimeFromInternet = true;
    }
  }
  if (!getTimeFromInternet)
  {
    rtc.read(&timeinfo);
  }
  else
  {
    rtc.write(&timeinfo); // update time in RTC
  }

  yearNow = timeinfo.tm_year + 1900;
  monthNow = timeinfo.tm_mon + 1;
  dayNow = timeinfo.tm_mday;
  weekdayNow = timeinfo.tm_wday;
  hourNow = timeinfo.tm_hour;
  minuteNow = timeinfo.tm_min;
  secondNow = timeinfo.tm_sec;

  // expose current minute-of-day and day-of-week for separate control logic
  currentTimerNow = (hourNow * 60) + minuteNow;
  dayOfWeekNow = weekdayNow - 1;
  if (dayOfWeekNow < 0)
    dayOfWeekNow = 6;

  if (currentTimerNow < 0 || currentTimerNow > 1440)
  {
    curentTimerError = 1;
  }
  else
  {
    curentTimerError = 0;
  }
}

// New: separate function that actually toggles relays based on the time globals.
static void ControlRelay_Bytimmer_Control()
{
  // Intentionally left empty: relay control removed. Timers only update time/UI now.
}

/* ----------------------- Manual Control --------------------------- */
void ControlRelay_Bymanual(String topic, String message, unsigned int length)
{
  String manual_message = message;
  int manual_relay = topic.substring(topic.length() - 1).toInt();
  ESP_LOGV(TAG, "manual_message : %s", manual_message.c_str());
  ESP_LOGV(TAG, "manual_relay   : %d", manual_relay);

  if (manual_message == "on")
  {
    Open_relay(manual_relay, "MANUAL_MQTT");
  }
  else if (manual_message == "off")
  {
    Close_relay(manual_relay, "MANUAL_MQTT");
  }

  // After handling manual MQTT command, ensure Switch API is updated (map relay 0-3 -> switch 1-4)
#if USE_SWITCH_API_CONTROL >= 1
  if (manual_relay >= 0 && manual_relay < 4)
  {
    int state = RelayStatus[manual_relay];
    ESP_LOGD(TAG, "Manual control: reporting relay %d state=%d to Switch API", manual_relay, state);
    // set loop-protect so that when API returns state we don't re-apply it
    ignoreNextSync[manual_relay] = true;
    // Update lastKnownSwitchStates and call API directly to be explicit
    int switchId = RELAY_ID_TO_SWITCH_ID(manual_relay);
    lastKnownSwitchStates[switchId - 1] = state;
    if (SwitchApiClient::updateSwitchState(switchId, state))
    {
      ESP_LOGI(TAG, "Manual -> Switch API updated switch %d to %s", switchId, state ? "ON" : "OFF");
    }
    else
    {
      ESP_LOGW(TAG, "Manual -> Switch API failed update for switch %d", switchId);
      // fallback: try our wrapper which retries
      updateSwitchStateToAPI(manual_relay, state);
    }
  }
#endif
}

/* ----------------------- SoilMaxMin_setting --------------------------- */
void SoilMaxMin_setting(String topic, String message, unsigned int length)
{
  String soil_message = message;
  String soil_topic = topic;
  int Relay_SoilMaxMin = topic.substring(topic.length() - 1).toInt();
  if (soil_topic.substring(9, 12) == "max")
  {
    Max_Soil[Relay_SoilMaxMin] = soil_message.toInt();
    EEPROM.write(Relay_SoilMaxMin + 2000, Max_Soil[Relay_SoilMaxMin]);
    EEPROM.commit();
    check_sendData_SoilMinMax = 1;
    ESP_LOGV(TAG, "Max_Soil : %f", Max_Soil[Relay_SoilMaxMin]);
  }
  else if (soil_topic.substring(9, 12) == "min")
  {
    Min_Soil[Relay_SoilMaxMin] = soil_message.toInt();
    EEPROM.write(Relay_SoilMaxMin + 2004, Min_Soil[Relay_SoilMaxMin]);
    EEPROM.commit();
    check_sendData_SoilMinMax = 1;
    ESP_LOGV(TAG, "Min_Soil : %f", Min_Soil[Relay_SoilMaxMin]);
  }
  UI_updateTempSoilMaxMin();
}

/* ----------------------- TempMaxMin_setting --------------------------- */
void TempMaxMin_setting(String topic, String message, unsigned int length)
{
  String temp_message = message;
  String temp_topic = topic;
  int Relay_TempMaxMin = topic.substring(topic.length() - 1).toInt();
  if (temp_topic.substring(9, 12) == "max")
  {
    Max_Temp[Relay_TempMaxMin] = temp_message.toInt();
    EEPROM.write(Relay_TempMaxMin + 2008, Max_Temp[Relay_TempMaxMin]);
    EEPROM.commit();
    check_sendData_tempMinMax = 1;
    ESP_LOGV(TAG, "Max_Temp : %f", Max_Temp[Relay_TempMaxMin]);
  }
  else if (temp_topic.substring(9, 12) == "min")
  {
    Min_Temp[Relay_TempMaxMin] = temp_message.toInt();
    EEPROM.write(Relay_TempMaxMin + 2012, Min_Temp[Relay_TempMaxMin]);
    EEPROM.commit();
    check_sendData_tempMinMax = 1;
    ESP_LOGV(TAG, "Min_Temp : %f", Min_Temp[Relay_TempMaxMin]);
  }
  UI_updateTempSoilMaxMin();
}

void HandySense_setTempMin(uint8_t relay, int value)
{
  Min_Temp[relay] = value;
  EEPROM.write(relay + 2012, Min_Temp[relay]);
  EEPROM.commit();
  ESP_LOGV(TAG, "Min_Temp : %f", Min_Temp[relay]);

  char payload[64];
  memset(payload, 0, sizeof(payload));
  sprintf(payload, "{\"data\":{\"min_temp%d\":%.0f}}",
          relay, Min_Temp[relay]);
  ESP_LOGV(TAG, "update shadow : %s", payload);
  client.publish("@shadow/data/update", payload);
}

void HandySense_setTempMax(uint8_t relay, int value)
{
  Max_Temp[relay] = value;
  EEPROM.write(relay + 2008, Max_Temp[relay]);
  EEPROM.commit();
  ESP_LOGV(TAG, "Max_Temp : %f", Max_Temp[relay]);

  char payload[64];
  memset(payload, 0, sizeof(payload));
  sprintf(payload, "{\"data\":{\"max_temp%d\":%.0f}}",
          relay, Max_Temp[relay]);
  ESP_LOGV(TAG, "update shadow : %s", payload);
  client.publish("@shadow/data/update", payload);
}

void HandySense_setSoilMin(uint8_t relay, int value)
{
  Min_Soil[relay] = value;
  EEPROM.write(relay + 2004, Min_Soil[relay]);
  EEPROM.commit();
  ESP_LOGV(TAG, "Min_Soil : %f", Min_Soil[relay]);

  char payload[64];
  memset(payload, 0, sizeof(payload));
  sprintf(payload, "{\"data\":{\"min_soil%d\":%.0f}}",
          relay, Min_Soil[relay]);
  ESP_LOGV(TAG, "update shadow : %s", payload);
  client.publish("@shadow/data/update", payload);
}

void HandySense_setSoilMax(uint8_t relay, int value)
{
  Max_Soil[relay] = value;
  EEPROM.write(relay + 2000, Max_Soil[relay]);
  EEPROM.commit();
  ESP_LOGV(TAG, "Max_Soil : %f", Max_Soil[relay]);

  char payload[64];
  memset(payload, 0, sizeof(payload));
  sprintf(payload, "{\"data\":{\"max_soil%d\":%.0f}}",
          relay, Max_Soil[relay]);
  ESP_LOGV(TAG, "update shadow : %s", payload);
  client.publish("@shadow/data/update", payload);
}

/* ----------------------- soilMinMax_ControlRelay --------------------------- */
// **[แก้ไข]** ฟังก์ชันนี้ถูกปรับปรุงให้ใช้ setRelayState เพื่อป้องกันการสั่งซ้ำ
static void ControlRelay_BysoilMinMax()
{
  for (int k = 0; k < 4; k++)
  {
    if (Min_Soil[k] != 0 && Max_Soil[k] != 0)
    {
      if (soil < Min_Soil[k])
      {
        Open_relay(k, "SOIL_SENSOR_LEGACY");
      }
      else if (soil > Max_Soil[k])
      {
        Close_relay(k, "SOIL_SENSOR_LEGACY");
      }
      // ถ้าค่าอยู่ระหว่างกลาง ไม่ต้องทำอะไร (จะคงสถานะเดิมไว้เพราะ setRelayState)
    }
  }
}

/* ----------------------- tempMinMax_ControlRelay --------------------------- */
// **[แก้ไข]** ฟังก์ชันนี้ถูกปรับปรุงให้ใช้ setRelayState เพื่อป้องกันการสั่งซ้ำ
void ControlRelay_BytempMinMax()
{
  for (int g = 0; g < 4; g++)
  {
    if (Min_Temp[g] != 0 && Max_Temp[g] != 0)
    {
      if (temp > Max_Temp[g])
      { // อากาศร้อน -> เปิด
        Open_relay(g, "TEMP_SENSOR_LEGACY");
      }
      else if (temp < Min_Temp[g])
      { // อากาศเย็น -> ปิด
        Close_relay(g, "TEMP_SENSOR_LEGACY");
      }
      // ถ้าค่าอยู่ระหว่างกลาง ไม่ต้องทำอะไร (จะคงสถานะเดิมไว้เพราะ setRelayState)
    }
  }
}

/* ----------------------- Set All Config --------------------------- */
void setAll_config()
{
  for (int b = 0; b < 4; b++)
  {
    Max_Soil[b] = EEPROM.read(b + 2000);
    Min_Soil[b] = EEPROM.read(b + 2004);
    Max_Temp[b] = EEPROM.read(b + 2008);
    Min_Temp[b] = EEPROM.read(b + 2012);
    if (Max_Soil[b] >= 255)
    {
      Max_Soil[b] = 0;
    }
    if (Min_Soil[b] >= 255)
    {
      Min_Soil[b] = 0;
    }
    if (Max_Temp[b] >= 255)
    {
      Max_Temp[b] = 0;
    }
    if (Min_Temp[b] >= 255)
    {
      Min_Temp[b] = 0;
    }
    ESP_LOGV(TAG, "Max_Soil   %d : %f", b, Max_Soil[b]);
    ESP_LOGV(TAG, "Min_Soil   %d : %f", b, Min_Soil[b]);
    ESP_LOGV(TAG, "Max_Temp   %d : %f", b, Max_Temp[b]);
    ESP_LOGV(TAG, "Min_Temp   %d : %f", b, Min_Temp[b]);
  }
  int count_in = 0;
  for (int eeprom_relay = 0; eeprom_relay < 4; eeprom_relay++)
  {
    for (int eeprom_timer = 0; eeprom_timer < 3; eeprom_timer++)
    {
      for (int dayinweek = 0; dayinweek < 7; dayinweek++)
      {
        int eeprom_address = ((((eeprom_relay * 7 * 3) + (dayinweek * 3) + eeprom_timer) * 2) * 2) + 2100;
        time_open[eeprom_relay][dayinweek][eeprom_timer] = (EEPROM.read(eeprom_address) * 256) + (EEPROM.read(eeprom_address + 1));
        time_close[eeprom_relay][dayinweek][eeprom_timer] = (EEPROM.read(eeprom_address + 2) * 256) + (EEPROM.read(eeprom_address + 3));

        if (time_open[eeprom_relay][dayinweek][eeprom_timer] >= 2000)
        {
          time_open[eeprom_relay][dayinweek][eeprom_timer] = 3000;
        }
        if (time_close[eeprom_relay][dayinweek][eeprom_timer] >= 2000)
        {
          time_close[eeprom_relay][dayinweek][eeprom_timer] = 3000;
        }
        ESP_LOGV(TAG, "cout       : %d", count_in);
        ESP_LOGV(TAG, "time_open  : %d", time_open[eeprom_relay][dayinweek][eeprom_timer]);
        ESP_LOGV(TAG, "time_close : %d", time_close[eeprom_relay][dayinweek][eeprom_timer]);
        count_in++;
      }
    }
  }
}

/* ----------------------- Delete All Config --------------------------- */
void Delete_All_config()
{
  for (int b = 2000; b < 4096; b++)
  {
    EEPROM.write(b, 255);
    EEPROM.commit();
  }
}

/* ----------------------- Add and Edit device || Edit Wifi --------------------------- */
void Edit_device_wifi()
{
  connectWifiStatus = editDeviceWifi;
  Serial.write(START_PATTERN, 3);
  Serial.flush();
  File configs_file = SPIFFS.open(CONFIG_FILE);
  deserializeJson(jsonDoc, configs_file);
  configs_file.close();
  client_old = jsonDoc["client"].as<String>();
  Serial.write(STX);
  serializeJsonPretty(jsonDoc, Serial);
  Serial.write(ETX);
  delay(1000);
  Serial.write(START_PATTERN, 3);
  Serial.flush();
  jsonDoc["server"] = NULL;
  jsonDoc["client"] = NULL;
  jsonDoc["pass"] = NULL;
  jsonDoc["user"] = NULL;
  jsonDoc["password"] = NULL;
  jsonDoc["port"] = NULL;
  jsonDoc["ssid"] = NULL;
  jsonDoc["command"] = NULL;
  Serial.write(STX);
  Serial.print("{}");
  Serial.write(ETX);
}

/* -------- webSerialJSON function ------- */
void webSerialJSON()
{
  while (Serial.available() > 0)
  {
    Serial.setTimeout(10000);
    DeserializationError err = deserializeJson(jsonDoc, Serial);
    if (err == DeserializationError::Ok)
    {
      String command = jsonDoc["command"].as<String>();
      bool isValidData = !jsonDoc["client"].isNull();
      if (command == "restart")
      {
        delay(100);
        ESP.restart();
      }
      if (isValidData)
      {
        /* ------------------WRITING----------------- */
        File configs_file = SPIFFS.open(CONFIG_FILE, FILE_WRITE);
        serializeJson(jsonDoc, configs_file);
        configs_file.close();
        if (client_old != jsonDoc["client"].as<String>())
        {
          Delete_All_config();
        }
        delay(100);
        ESP.restart();
      }
    }
    else
    {
      Serial.read();
    }
  }
}

void wifiConfig(String ssid, String password)
{
  jsonDoc.clear();
  {
    File configs_file = SPIFFS.open(CONFIG_FILE);
    deserializeJson(jsonDoc, configs_file);
    configs_file.close();
  }
  jsonDoc["ssid"] = ssid;
  jsonDoc["password"] = password;
  {
    File configs_file = SPIFFS.open(CONFIG_FILE, FILE_WRITE);
    serializeJson(jsonDoc, configs_file);
    configs_file.close();
  }
  delay(100);
  ESP.restart();
}

/* --------- อินเตอร์รัป แสดงสถานะการเชื่อม wifi ------------- */
void HandySense_init()
{
  EEPROM.begin(4096);

  Wire.begin();
  Wire.setClock(10000);
  rtc.begin();
  Sensor_init();

  ApiClient::init();
  AutomationApiClient::init();

  for (int i = 0; i < 4; i++)
  {
    pinMode(relay_pin[i], OUTPUT);
    digitalWrite(relay_pin[i], LOW);
    RelayStatus[i] = 0; // **[แก้ไข]** ทำให้แน่ใจว่าสถานะเริ่มต้นเป็น OFF
    // Initialize last known switch states and, if possible, push to API so server sees current state
    lastKnownSwitchStates[i] = RelayStatus[i];
#if USE_SWITCH_API_CONTROL >= 1
    if (WiFi.status() == WL_CONNECTED)
    {
      // Avoid immediate loop when API returns this state
      ignoreNextSync[i] = true;
      updateSwitchStateToAPI(i, RelayStatus[i]);
    }
#endif
  }

#if USE_SWITCH_API_CONTROL
  ESP_LOGI(TAG, "Switch API Control Enabled");
#else
  ESP_LOGI(TAG, "Using legacy MQTT Switch Control");
#endif

  if (!SPIFFS.begin(true))
  { // Format if fail
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  Edit_device_wifi();
  File configs_file = SPIFFS.open(CONFIG_FILE);
  if (configs_file && (!configs_file.isDirectory()))
  {
    deserializeJson(jsonDoc, configs_file);
    configs_file.close();
    if (!jsonDoc.isNull())
    {
      mqtt_server = jsonDoc["server"].as<String>();
      mqtt_Client = jsonDoc["client"].as<String>();
      mqtt_password = jsonDoc["pass"].as<String>();
      mqtt_username = jsonDoc["user"].as<String>();
      password = jsonDoc["password"].as<String>();
      mqtt_port = jsonDoc["port"].as<String>();
      ssid = jsonDoc["ssid"].as<String>();
    }
  }
  xTaskCreatePinnedToCore(TaskWifiStatus, "WifiStatus", 4096, NULL, 10, &WifiStatus, 1);
  xTaskCreatePinnedToCore(TaskWaitSerial, "WaitSerial", 8192, NULL, 10, &WaitSerial, 1);
  setAll_config();
}

bool wifi_ready = false;

// ===================================================================
// Automation Functions (API-based)
// ===================================================================

#if AUTOMATION_API_ENABLE

// Check และ Trigger Timers จาก API
void checkAndTriggerTimers()
{
  if (!AutomationApiClient::isAnyAutomationActive())
    return;

  time_t now;
  time(&now);
  struct tm *timeinfo = localtime(&now);

  int currentMinutes = timeinfo->tm_hour * 60 + timeinfo->tm_min;
  int dayOfWeek = AutomationApiClient::getDayOfWeek(timeinfo);

  for (int relayId = 0; relayId < 4; relayId++)
  {
    if (AutomationApiClient::isOverrideActive(relayId))
      continue;

    // Only act when this relay has at least one enabled timer configured.
    bool shouldBeOn = false;
    bool hasEnabledTimer = false;
    for (int timerId = 0; timerId < 3; timerId++)
    {
      AutomationTimer *timer = AutomationApiClient::getLocalTimer(relayId, timerId);
      if (timer && timer->enabled)
      {
        hasEnabledTimer = true;
        if (AutomationApiClient::isTimerActive(relayId, timerId, currentMinutes, dayOfWeek))
        {
          shouldBeOn = true;
          break;
        }
      }
    }

    if (!hasEnabledTimer)
    {
      ESP_LOGD(TAG, "Relay %d: no enabled timers configured, skipping timer control", relayId);
      continue; // don't force state for relays with no timers
    }

    if (shouldBeOn)
    {
      Open_relay(relayId, "AUTO_API_TIMER");
      // Ensure API is informed about this automation decision
#if USE_SWITCH_API_CONTROL >= 1
      ignoreNextSync[relayId] = true;
      updateSwitchStateToAPI(relayId, RelayStatus[relayId]);
#endif
    }
    else
    {
      Close_relay(relayId, "AUTO_API_TIMER");
      // Ensure API is informed about this automation decision
#if USE_SWITCH_API_CONTROL >= 1
      ignoreNextSync[relayId] = true;
      updateSwitchStateToAPI(relayId, RelayStatus[relayId]);
#endif
    }
  }
}

// Check และ Trigger Sensor ตัวเดียวจาก API
void checkSensorControl(int relayId, const char *sensorType, float currentValue)
{
  if (!AutomationApiClient::isAnyAutomationActive())
    return;
  if (AutomationApiClient::isOverrideActive(relayId))
    return;

  bool shouldTurnOn = false;
  bool hasTrigger = AutomationApiClient::checkSensorTrigger(relayId, sensorType, currentValue, &shouldTurnOn);
  if (hasTrigger)
  {
    ESP_LOGI(TAG, "Sensor decision relay=%d type=%s -> hasTrigger=1 shouldTurnOn=%d", relayId, sensorType, shouldTurnOn);
    if (shouldTurnOn)
    {
      Open_relay(relayId, "AUTO_API_SENSOR");
      // Ensure API is informed about this automation decision
#if USE_SWITCH_API_CONTROL >= 1
      ignoreNextSync[relayId] = true;
      updateSwitchStateToAPI(relayId, RelayStatus[relayId]);
#endif
    }
    else
    {
      // actionOnTrigger == turn_off -> attempt to close the relay if it's currently open
      if (relayId >= 0 && relayId < 4 && RelayStatus[relayId] == 1)
      {
        ESP_LOGI(TAG, "Sensor requests turn_off and relay %d is OPEN -> closing relay", relayId);
        Close_relay(relayId, "AUTO_API_SENSOR");
#if USE_SWITCH_API_CONTROL >= 1
        ignoreNextSync[relayId] = true;
        updateSwitchStateToAPI(relayId, RelayStatus[relayId]);
#endif
      }
      else
      {
        ESP_LOGD(TAG, "Sensor requests turn_off but relay %d already closed or invalid", relayId);
      }
    }
  }
}

// Check และ Trigger Sensors ทั้งหมดจาก API
void checkAndTriggerSensors()
{
  float currentTemp = temp;
  float currentSoil = soil;
  float currentHumidity = humidity;
  float currentLight = lux_44009;

  for (int relayId = 0; relayId < 4; relayId++)
  {
    // Only evaluate sensor control for sensor types that are actually configured & enabled
    AutomationSensor *sTemp = AutomationApiClient::getLocalSensor(relayId, "temperature");
    if (currentTemp != 0 && sTemp && sTemp->enabled)
    {
      ESP_LOGD(TAG, "Relay %d: temperature sensor enabled, checking...", relayId);
      checkSensorControl(relayId, "temperature", currentTemp);
    }

    AutomationSensor *sSoil = AutomationApiClient::getLocalSensor(relayId, "soil_moisture");
    if (currentSoil != 0 && sSoil && sSoil->enabled)
    {
      ESP_LOGD(TAG, "Relay %d: soil_moisture sensor enabled, checking...", relayId);
      checkSensorControl(relayId, "soil_moisture", currentSoil);
    }

    AutomationSensor *sHum = AutomationApiClient::getLocalSensor(relayId, "humidity");
    if (currentHumidity != 0 && sHum && sHum->enabled)
    {
      ESP_LOGD(TAG, "Relay %d: humidity sensor enabled, checking...", relayId);
      checkSensorControl(relayId, "humidity", currentHumidity);
    }

    AutomationSensor *sLight = AutomationApiClient::getLocalSensor(relayId, "light");
    if (currentLight != 0 && sLight && sLight->enabled)
    {
      ESP_LOGD(TAG, "Relay %d: light sensor enabled, checking...", relayId);
      checkSensorControl(relayId, "light", currentLight);
    }
  }
}

#endif // AUTOMATION_API_ENABLE

// ===================================================================
// Main Loop
// ===================================================================
void HandySense_loop()
{
  if (wifi_ready)
  {
    client.loop();
  }
  UI_loop();

#if USE_SWITCH_API_CONTROL == 1 || USE_SWITCH_API_CONTROL == 2
  if (wifi_ready)
  {
    syncSwitchStatesFromAPI();
  }
#endif

  unsigned long currentTime = millis();
  if (currentTime - previousTime_Temp_soil >= eventInterval)
  {
    float newTemp = 0, newSoil = 0;
    Sensor_getTemp(&newTemp);
    Sensor_getHumi(&humidity);
    Sensor_getSoil(&newSoil);

    bool update_to_server = (abs(newTemp - temp) >= difference_temp || abs(newSoil - soil) >= difference_soil);

    temp = newTemp;
    soil = newSoil;

// **[แก้ไข]** เลือกระบบควบคุมตามค่า #define ที่ตั้งไว้ข้างบน
#if AUTOMATION_API_ENABLE == 0
    // ถ้า Automation API ปิดอยู่ ให้ใช้ระบบควบคุมแบบเก่า (Legacy) ผ่าน MQTT
    ControlRelay_Bytimmer();
    ControlRelay_Bytimmer_Control();
    ControlRelay_BysoilMinMax();
    ControlRelay_BytempMinMax();
#endif
  ControlRelay_Bytimmer();
    if (wifi_ready && update_to_server)
    {
      UpdateData_To_Server();
      previousTime_Update_data = millis();
    }
    previousTime_Temp_soil = currentTime;
  }

  if (currentTime - previousTime_brightness >= eventInterval_brightness)
  {
    Sensor_getLight(&lux_44009);
    lux_44009 /= 1000.0;
    previousTime_brightness = currentTime;
  }

  unsigned long currentTime_Update_data = millis();
  if ((previousTime_Update_data == 0 || (currentTime_Update_data - previousTime_Update_data >= (eventInterval_publishData))) && wifi_ready)
  {
    UpdateData_To_Server();
    previousTime_Update_data = currentTime_Update_data;
  }

// ========== Automation Checks (ระบบใหม่ผ่าน API) ==========
#if AUTOMATION_API_ENABLE
  if (wifi_ready)
  {
    static unsigned long lastSync = 0;
    static unsigned long lastTimerCheck = 0;
    static unsigned long lastSensorCheck = 0;
    static unsigned long bootTime = millis();
    unsigned long now = millis();

    if (now - bootTime < 5000)
      return; // รอ 5 วินาทีหลัง Boot ก่อนเริ่มทำงาน

    // Full sync every 10 minutes
    if (now - lastSync > AUTOMATION_SYNC_INTERVAL)
    {
      ESP_LOGI(TAG, "[AUTO] Syncing automation from API...");
      if (AutomationApiClient::syncFromAPI())
      {
        ESP_LOGI(TAG, "[AUTO] Sync complete");
      }
      lastSync = now;
    }

    // Check timers every minute
    if (now - lastTimerCheck > TIMER_CHECK_INTERVAL)
    {
      // ESP_LOGD(TAG, "[AUTO] Checking timers...");
      checkAndTriggerTimers();
      lastTimerCheck = now;
    }

    // Check sensors every 5 seconds (ปรับจาก 5 นาทีเพื่อให้ตอบสนองเร็วขึ้น)
    if (now - lastSensorCheck > SENSOR_CHECK_INTERVAL)
    {
      // ESP_LOGD(TAG, "[AUTO] Checking sensors...");
      checkAndTriggerSensors();
      lastSensorCheck = now;
    }

    // Check for manual override expiration
    for (int i = 0; i < 4; i++)
    {
      if (AutomationApiClient::isOverrideActive(i))
      {
        AutomationStatus *status = AutomationApiClient::getLocalStatus(i);
        if (status && now > status->override_until)
        {
          ESP_LOGI(TAG, "[AUTO] Override expired for relay %d", i);
          AutomationApiClient::cancelOverride(i);
        }
      }
    }
  }
#endif
}

/* --------- Auto Connect Wifi and server and setup value init ------------- */
bool pause_wifi_task = false;

void TaskWifiStatus(void *pvParameters)
{
  while (1)
  {
    if (pause_wifi_task)
    {
      delay(10);
      continue;
    }

    connectWifiStatus = cannotConnect;
    if (!WiFi.isConnected())
    {
      if (ssid.length() > 0)
      {
        WiFi.begin(ssid.c_str(), password.c_str());
      }
    }

    while (WiFi.status() != WL_CONNECTED)
    {
      if ((millis() - time_restart) > INTERVAL_MESSAGE2)
      {
        time_restart = millis();
        ESP.restart();
      }
      delay(100);
    }

    connectWifiStatus = wifiConnected;
    client.setServer(mqtt_server.c_str(), mqtt_port.toInt());
    client.setCallback(callback);
    timeClient.begin();

    do
    {
      ESP_LOGV(TAG, "NETPIE2020 can not connect");
      client.connect(mqtt_Client.c_str(), mqtt_username.c_str(), mqtt_password.c_str());
      delay(100);
    } while (!client.connected());

    connectWifiStatus = serverConnected;
    ESP_LOGV(TAG, "NETPIE2020 connected");
    client.subscribe("@private/#");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer, nistTime);
    wifi_ready = true;

// ========== เริ่มต้น Switch Manager เมื่อ WiFi พร้อม ==========
#if USE_SWITCH_API_CONTROL >= 1
    static bool switchManagerInitialized = false;
    if (!switchManagerInitialized)
    {
      ESP_LOGI(TAG, "Initializing Switch Manager (Mode %d)...", USE_SWITCH_API_CONTROL);
      // SwitchManager::begin(); // Assuming this is handled elsewhere or not needed
      switchManagerInitialized = true;

      // ... (Initial state sync logic) ...
    }
#endif

// ========== เริ่มต้น Automation API เมื่อ WiFi พร้อม ==========
#if AUTOMATION_API_ENABLE
    static bool automationSyncInitialized = false;
    if (!automationSyncInitialized)
    {
      ESP_LOGI(TAG, "Initial Automation Sync...");
      if (AutomationApiClient::syncFromAPI())
      {
        ESP_LOGI(TAG, "Automation sync complete: %d timers, %d sensors",
                 AutomationApiClient::getLocalTimerCount(), AutomationApiClient::getLocalSensorCount());

        ESP_LOGI(TAG, "Checking automation status immediately after sync...");
        checkAndTriggerTimers();
        checkAndTriggerSensors();
      }
      else
      {
        ESP_LOGW(TAG, "Automation sync failed, will retry in sync interval");
      }
      automationSyncInitialized = true;
    }
#endif

    while (WiFi.status() == WL_CONNECTED && client.connected())
    {
#if USE_SWITCH_API_CONTROL == 0 || USE_SWITCH_API_CONTROL == 2
      sendStatus_RelaytoWeb();
#endif
      send_soilMinMax();
      send_tempMinMax();
      delay(500);
    }
    wifi_ready = false;
  }
}

/* --------- Auto Connect Serial ------------- */
void TaskWaitSerial(void *WaitSerial)
{
  while (1)
  {
    if (Serial.available())
      webSerialJSON();
    delay(500);
  }
}