// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

/*
 * This is an Arduino-based Azure IoT Hub sample for ESPRESSIF ESP32 boards.
 * It uses our Azure Embedded SDK for C to help interact with Azure IoT.
 * For reference, please visit https://github.com/azure/azure-sdk-for-c.
 *
 * To connect and work with Azure IoT Hub you need an MQTT client, connecting, subscribing
 * and publishing to specific topics to use the messaging features of the hub.
 * Our azure-sdk-for-c is an MQTT client support library, helping composing and parsing the
 * MQTT topic names and messages exchanged with the Azure IoT Hub.
 *
 * This sample performs the following tasks:
 * - Synchronize the device clock with a NTP server;
 * - Initialize our "az_iot_hub_client" (struct for data, part of our azure-sdk-for-c);
 * - Initialize the MQTT client (here we use ESPRESSIF's esp_mqtt_client, which also handle the tcp
 * connection and TLS);
 * - Connect the MQTT client (using server-certificate validation, SAS-tokens for client
 * authentication);
 * - Periodically send telemetry data to the Azure IoT Hub.
 *
 * To properly connect to your Azure IoT Hub, please fill the information in the `iot_configs.h`
 * file.
 */

// C99 libraries
#include <cstdlib>
#include <string.h>
#include <time.h>

#include <Wire.h>
#include <DHT.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>

// Libraries for MQTT client and WiFi connection
#include <WiFi.h>
#include <mqtt_client.h>

// Azure IoT SDK for C includes
#include <az_core.h>
#include <az_iot.h>
#include <azure_ca.h>

// Additional sample headers
#include "AzIoTSasToken.h"
#include "SerialLogger.h"
#include "iot_configs.h"

// When developing for your own Arduino-based platform,
// please follow the format '(ard;<platform>)'.
#define AZURE_SDK_CLIENT_USER_AGENT "c%2F" AZ_SDK_VERSION_STRING "(ard;esp32)"

// Utility macros and defines
#define sizeofarray(a) (sizeof(a) / sizeof(a[0]))
#define NTP_SERVERS "pool.ntp.org", "time.nist.gov"
#define MQTT_QOS1 1
#define DO_NOT_RETAIN_MSG 0
#define SAS_TOKEN_DURATION_IN_MINUTES 60
#define UNIX_TIME_NOV_13_2017 1510592825

#define PST_TIME_ZONE -8
#define PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF 1

#define GMT_OFFSET_SECS (PST_TIME_ZONE * 3600)
#define GMT_OFFSET_SECS_DST ((PST_TIME_ZONE + PST_TIME_ZONE_DAYLIGHT_SAVINGS_DIFF) * 3600)

#define DHT_PIN 4
#define DHT_TYPE DHT22
#define MQ135_PIN 34
#define MQ6_DO_PIN 35
#define FAN1_PIN 32
#define FAN2_PIN 33

static DHT dht(DHT_PIN, DHT_TYPE);
static RTC_DS3231 rtc;
static LiquidCrystal_I2C lcd(0x27, 20, 4);

static bool rtc_found = false;
static int fan_stage_command = 0;
static String current_label = "UNLABELED";
static bool manual_fan_override = false;

static const int BASELINE_SAMPLES_REQUIRED = 20;
static const int STAGE_PERSISTENCE_REQUIRED = 3;
static const unsigned long MINIMUM_FAN_HOLD_MS = 15000UL;
static const float SENSOR_EMA_ALPHA = 0.35f;
static const float SLOPE_EMA_ALPHA = 0.25f;
static const float BASELINE_ADAPT_ALPHA = 0.005f;

static bool baseline_ready = false;
static int baseline_sample_count = 0;
static float mq135_filtered = NAN;
static float humidity_filtered = NAN;
static float mq135_baseline = NAN;
static float humidity_baseline = NAN;
static float mq135_slope_per_minute = 0.0f;
static float humidity_slope_per_minute = 0.0f;
static float mq135_projected_5min = NAN;
static float humidity_projected_5min = NAN;
static float previous_mq135_filtered = NAN;
static float previous_humidity_filtered = NAN;
static unsigned long previous_prediction_sample_ms = 0;
static unsigned long fan_hold_until_ms = 0;
static int predicted_fan_stage = 0;
static int pending_fan_stage = 0;
static int pending_fan_stage_count = 0;
static String prediction_state = "CALIBRATING";
static String prediction_reason = "WARMUP";

static void printLcdLine(uint8_t row, String text);

static void setFanStage(int stage)
{
  stage = constrain(stage, 0, 2);

  digitalWrite(FAN1_PIN, stage >= 1 ? HIGH : LOW);
  digitalWrite(FAN2_PIN, stage >= 2 ? HIGH : LOW);
  fan_stage_command = stage;

  Logger.Info("Fan stage set to " + String(fan_stage_command));
}

static void handleSerialFanCommands()
{
  while (Serial.available() > 0)
  {
    char command = Serial.read();

    if (command >= '0' && command <= '2')
    {
      manual_fan_override = true;
      setFanStage(command - '0');
      printLcdLine(3, "Manual fan stage: " + String(fan_stage_command));
    }
    else
    {
      if (command == 'a' || command == 'A')
      {
        manual_fan_override = false;
        pending_fan_stage_count = 0;
        Logger.Info("Automatic predictive control enabled");
        printLcdLine(3, "Control: AUTO");
        continue;
      }
      else if (command == 'n' || command == 'N') current_label = "NORMAL";
      else if (command == 'h' || command == 'H') current_label = "HUMIDITY";
      else if (command == 's' || command == 'S') current_label = "SMOKE";
      else if (command == 'g' || command == 'G') current_label = "BUTANE";
      else if (command == 'u' || command == 'U') current_label = "UNLABELED";
      else continue;

      Logger.Info("Data label set to " + current_label);
      printLcdLine(3, "Label: " + current_label);
    }
  }
}

static void printLcdLine(uint8_t row, String text)
{
  while (text.length() < 20)
  {
    text += " ";
  }

  lcd.setCursor(0, row);
  lcd.print(text.substring(0, 20));
}

static void applyAutomaticFanStage(int desired_stage, bool immediate_override)
{
  predicted_fan_stage = constrain(desired_stage, 0, 2);
  unsigned long now_ms = millis();

  if (predicted_fan_stage > 0)
  {
    fan_hold_until_ms = now_ms + MINIMUM_FAN_HOLD_MS;
  }

  if (immediate_override)
  {
    pending_fan_stage_count = 0;
    if (fan_stage_command != predicted_fan_stage)
    {
      setFanStage(predicted_fan_stage);
    }
    return;
  }

  if (manual_fan_override)
  {
    return;
  }

  if (predicted_fan_stage == fan_stage_command)
  {
    pending_fan_stage_count = 0;
    return;
  }

  if (predicted_fan_stage < fan_stage_command && now_ms < fan_hold_until_ms)
  {
    return;
  }

  if (pending_fan_stage != predicted_fan_stage)
  {
    pending_fan_stage = predicted_fan_stage;
    pending_fan_stage_count = 1;
  }
  else
  {
    pending_fan_stage_count++;
  }

  if (pending_fan_stage_count >= STAGE_PERSISTENCE_REQUIRED)
  {
    setFanStage(predicted_fan_stage);
    pending_fan_stage_count = 0;
  }
}

static void updatePredictiveController(
  int mq135_raw,
  bool mq6_triggered,
  float humidity
)
{
  bool mq135_valid = mq135_raw > 20;
  bool humidity_valid = !isnan(humidity);

  if (mq135_valid)
  {
    if (isnan(mq135_filtered)) mq135_filtered = mq135_raw;
    else mq135_filtered += SENSOR_EMA_ALPHA * (mq135_raw - mq135_filtered);
  }

  if (humidity_valid)
  {
    if (isnan(humidity_filtered)) humidity_filtered = humidity;
    else humidity_filtered += SENSOR_EMA_ALPHA * (humidity - humidity_filtered);
  }

  unsigned long now_ms = millis();

  if (previous_prediction_sample_ms > 0 &&
      now_ms > previous_prediction_sample_ms &&
      !isnan(previous_mq135_filtered) &&
      !isnan(previous_humidity_filtered))
  {
    float elapsed_minutes =
      (now_ms - previous_prediction_sample_ms) / 60000.0f;

    if (elapsed_minutes >= 0.01f)
    {
      float mq135_instant_slope =
        (mq135_filtered - previous_mq135_filtered) / elapsed_minutes;

      float humidity_instant_slope =
        (humidity_filtered - previous_humidity_filtered) / elapsed_minutes;

      mq135_slope_per_minute += SLOPE_EMA_ALPHA *
        (mq135_instant_slope - mq135_slope_per_minute);

      humidity_slope_per_minute += SLOPE_EMA_ALPHA *
        (humidity_instant_slope - humidity_slope_per_minute);
    }
  }

  if (!isnan(mq135_filtered) && !isnan(humidity_filtered))
  {
    previous_mq135_filtered = mq135_filtered;
    previous_humidity_filtered = humidity_filtered;
    previous_prediction_sample_ms = now_ms;
  }

  if (!baseline_ready)
  {
    prediction_state = "CALIBRATING";
    prediction_reason = "WARMUP";

    // Ignore the MQ-6 module's LOW state while the shared 5 V MQ supply is off.
    if (mq6_triggered && mq135_valid)
    {
      prediction_state = "ALERT_NOW";
      prediction_reason = "MQ6_TRIGGER";
      applyAutomaticFanStage(2, true);
      return;
    }

    bool stable_for_baseline =
      fabs(mq135_slope_per_minute) < 60.0f &&
      fabs(humidity_slope_per_minute) < 2.0f;

    if (mq135_valid && humidity_valid && stable_for_baseline &&
        !isnan(mq135_filtered) && !isnan(humidity_filtered))
    {
      baseline_sample_count++;

      if (baseline_sample_count == 1)
      {
        mq135_baseline = mq135_filtered;
        humidity_baseline = humidity_filtered;
      }
      else
      {
        mq135_baseline +=
          (mq135_filtered - mq135_baseline) / baseline_sample_count;

        humidity_baseline +=
          (humidity_filtered - humidity_baseline) / baseline_sample_count;
      }

      if (baseline_sample_count >= BASELINE_SAMPLES_REQUIRED)
      {
        baseline_ready = true;
        mq135_slope_per_minute = 0.0f;
        humidity_slope_per_minute = 0.0f;
        prediction_state = "NORMAL";
        prediction_reason = "BASELINE_READY";
        Logger.Info("Predictive baseline ready");
      }
    }
    else if (baseline_sample_count > 0)
    {
      baseline_sample_count = 0;
      mq135_baseline = NAN;
      humidity_baseline = NAN;
    }

    applyAutomaticFanStage(0, false);
    return;
  }

  float mq135_delta = mq135_filtered - mq135_baseline;
  float humidity_delta = humidity_filtered - humidity_baseline;

  float limited_mq135_slope =
    constrain(mq135_slope_per_minute, -20.0f, 20.0f);

  float limited_humidity_slope =
    constrain(humidity_slope_per_minute, -1.5f, 1.5f);

  mq135_projected_5min =
    mq135_filtered + limited_mq135_slope * 5.0f;

  humidity_projected_5min =
    humidity_filtered + limited_humidity_slope * 5.0f;

  float projected_mq135_delta = mq135_projected_5min - mq135_baseline;
  float projected_humidity_delta =
    humidity_projected_5min - humidity_baseline;

  int desired_stage = 0;
  bool immediate_override = false;
  prediction_state = "NORMAL";
  prediction_reason = "STABLE";

  if (mq6_triggered && mq135_valid)
  {
    desired_stage = 2;
    immediate_override = true;
    prediction_state = "ALERT_NOW";
    prediction_reason = "MQ6_TRIGGER";
  }
  else if (mq135_delta >= 150.0f)
  {
    desired_stage = 2;
    prediction_state = "ALERT_NOW";
    prediction_reason = "MQ135_HIGH";
  }
  else if (mq135_delta >= 70.0f)
  {
    desired_stage = 1;
    prediction_state = "ALERT_NOW";
    prediction_reason = "MQ135_RISE";
  }
  else if (humidity_delta >= 3.0f)
  {
    desired_stage = 1;
    prediction_state = "ALERT_NOW";
    prediction_reason = "HUMIDITY_RISE";
  }
  else if (mq135_delta >= 25.0f &&
           mq135_slope_per_minute > 3.0f &&
           projected_mq135_delta >= 70.0f)
  {
    desired_stage = 1;
    prediction_state = "PREDICTED_5MIN";
    prediction_reason = "MQ135_TREND";
  }
  else if (humidity_delta >= 0.8f &&
           humidity_slope_per_minute > 0.20f &&
           projected_humidity_delta >= 3.0f)
  {
    desired_stage = 1;
    prediction_state = "PREDICTED_5MIN";
    prediction_reason = "HUMIDITY_TREND";
  }

  if (desired_stage == 0 && fan_stage_command == 0 &&
      fabs(mq135_delta) < 20.0f && fabs(humidity_delta) < 1.5f)
  {
    mq135_baseline += BASELINE_ADAPT_ALPHA *
      (mq135_filtered - mq135_baseline);

    humidity_baseline += BASELINE_ADAPT_ALPHA *
      (humidity_filtered - humidity_baseline);
  }

  applyAutomaticFanStage(desired_stage, immediate_override);
}

static void initializeSensors()
{
  // External NPN transistor drivers: HIGH = relay/fan ON, LOW = OFF.
  pinMode(FAN1_PIN, OUTPUT);
  pinMode(FAN2_PIN, OUTPUT);
  digitalWrite(FAN1_PIN, LOW);
  digitalWrite(FAN2_PIN, LOW);

  pinMode(MQ6_DO_PIN, INPUT);

  Wire.begin(21, 22);
  dht.begin();

  rtc_found = rtc.begin();

  lcd.init();
  lcd.backlight();
  lcd.clear();

  printLcdLine(0, "AsapGuard Starting");
  printLcdLine(1, "Sensors initialized");
  printLcdLine(2, rtc_found ? "RTC: OK" : "RTC: FAILED");
  printLcdLine(3, "Azure: connecting");
}

// Translate iot_configs.h defines into variables used by the sample
static const char* ssid = IOT_CONFIG_WIFI_SSID;
static const char* password = IOT_CONFIG_WIFI_PASSWORD;
static const char* host = IOT_CONFIG_IOTHUB_FQDN;
static const char* mqtt_broker_uri = "mqtts://" IOT_CONFIG_IOTHUB_FQDN;
static const char* device_id = IOT_CONFIG_DEVICE_ID;
static const int mqtt_port = AZ_IOT_DEFAULT_MQTT_CONNECT_PORT;

// Memory allocated for the sample's variables and structures.
static esp_mqtt_client_handle_t mqtt_client;
static az_iot_hub_client client;

static char mqtt_client_id[128];
static char mqtt_username[128];
static char mqtt_password[200];
static uint8_t sas_signature_buffer[256];
static unsigned long next_telemetry_send_time_ms = 0;
static char telemetry_topic[128];
static uint32_t telemetry_send_count = 0;
static String telemetry_payload = "{}";

#define INCOMING_DATA_BUFFER_SIZE 128
static char incoming_data[INCOMING_DATA_BUFFER_SIZE];

// Auxiliary functions
#ifndef IOT_CONFIG_USE_X509_CERT
static AzIoTSasToken sasToken(
    &client,
    AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_KEY),
    AZ_SPAN_FROM_BUFFER(sas_signature_buffer),
    AZ_SPAN_FROM_BUFFER(mqtt_password));
#endif // IOT_CONFIG_USE_X509_CERT

static void connectToWiFi()
{
  Logger.Info("Connecting to WIFI SSID " + String(ssid));

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");

  Logger.Info("WiFi connected, IP address: " + WiFi.localIP().toString());
}

static void initializeTime()
{
  Logger.Info("Setting time using SNTP");

  configTime(GMT_OFFSET_SECS, GMT_OFFSET_SECS_DST, NTP_SERVERS);
  time_t now = time(NULL);
  while (now < UNIX_TIME_NOV_13_2017)
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");
  Logger.Info("Time initialized!");
}

void receivedCallback(char* topic, byte* payload, unsigned int length)
{
  Logger.Info("Received [");
  Logger.Info(topic);
  Logger.Info("]: ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println("");
}

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
  (void)handler_args;
  (void)base;
  (void)event_id;

  esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
#else // ESP_ARDUINO_VERSION_MAJOR
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
#endif // ESP_ARDUINO_VERSION_MAJOR
  switch (event->event_id)
  {
    int i, r;

    case MQTT_EVENT_ERROR:
      Logger.Info("MQTT event MQTT_EVENT_ERROR");
      break;
    case MQTT_EVENT_CONNECTED:
      Logger.Info("MQTT event MQTT_EVENT_CONNECTED");

      r = esp_mqtt_client_subscribe(mqtt_client, AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC, 1);
      if (r == -1)
      {
        Logger.Error("Could not subscribe for cloud-to-device messages.");
      }
      else
      {
        Logger.Info("Subscribed for cloud-to-device messages; message id:" + String(r));
      }

      break;
    case MQTT_EVENT_DISCONNECTED:
      Logger.Info("MQTT event MQTT_EVENT_DISCONNECTED");
      break;
    case MQTT_EVENT_SUBSCRIBED:
      Logger.Info("MQTT event MQTT_EVENT_SUBSCRIBED");
      break;
    case MQTT_EVENT_UNSUBSCRIBED:
      Logger.Info("MQTT event MQTT_EVENT_UNSUBSCRIBED");
      break;
    case MQTT_EVENT_PUBLISHED:
      Logger.Info("MQTT event MQTT_EVENT_PUBLISHED");
      break;
    case MQTT_EVENT_DATA:
      Logger.Info("MQTT event MQTT_EVENT_DATA");

      for (i = 0; i < (INCOMING_DATA_BUFFER_SIZE - 1) && i < event->topic_len; i++)
      {
        incoming_data[i] = event->topic[i];
      }
      incoming_data[i] = '\0';
      Logger.Info("Topic: " + String(incoming_data));

      for (i = 0; i < (INCOMING_DATA_BUFFER_SIZE - 1) && i < event->data_len; i++)
      {
        incoming_data[i] = event->data[i];
      }
      incoming_data[i] = '\0';
      Logger.Info("Data: " + String(incoming_data));

      break;
    case MQTT_EVENT_BEFORE_CONNECT:
      Logger.Info("MQTT event MQTT_EVENT_BEFORE_CONNECT");
      break;
    default:
      Logger.Error("MQTT event UNKNOWN");
      break;
  }

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
#else // ESP_ARDUINO_VERSION_MAJOR
  return ESP_OK;
#endif // ESP_ARDUINO_VERSION_MAJOR
}

static void initializeIoTHubClient()
{
  az_iot_hub_client_options options = az_iot_hub_client_options_default();
  options.user_agent = AZ_SPAN_FROM_STR(AZURE_SDK_CLIENT_USER_AGENT);

  if (az_result_failed(az_iot_hub_client_init(
          &client,
          az_span_create((uint8_t*)host, strlen(host)),
          az_span_create((uint8_t*)device_id, strlen(device_id)),
          &options)))
  {
    Logger.Error("Failed initializing Azure IoT Hub client");
    return;
  }

  size_t client_id_length;
  if (az_result_failed(az_iot_hub_client_get_client_id(
          &client, mqtt_client_id, sizeof(mqtt_client_id) - 1, &client_id_length)))
  {
    Logger.Error("Failed getting client id");
    return;
  }

  if (az_result_failed(az_iot_hub_client_get_user_name(
          &client, mqtt_username, sizeofarray(mqtt_username), NULL)))
  {
    Logger.Error("Failed to get MQTT clientId, return code");
    return;
  }

  Logger.Info("Client ID: " + String(mqtt_client_id));
  Logger.Info("Username: " + String(mqtt_username));
}

static int initializeMqttClient()
{
#ifndef IOT_CONFIG_USE_X509_CERT
  if (sasToken.Generate(SAS_TOKEN_DURATION_IN_MINUTES) != 0)
  {
    Logger.Error("Failed generating SAS token");
    return 1;
  }
#endif

  esp_mqtt_client_config_t mqtt_config;
  memset(&mqtt_config, 0, sizeof(mqtt_config));

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    mqtt_config.broker.address.uri = mqtt_broker_uri;
    mqtt_config.broker.address.port = mqtt_port;
    mqtt_config.credentials.client_id = mqtt_client_id;
    mqtt_config.credentials.username = mqtt_username;

  #ifdef IOT_CONFIG_USE_X509_CERT
    LogInfo("MQTT client using X509 Certificate authentication");
    mqtt_config.credentials.authentication.certificate = IOT_CONFIG_DEVICE_CERT;
    mqtt_config.credentials.authentication.certificate_len = (size_t)sizeof(IOT_CONFIG_DEVICE_CERT);
    mqtt_config.credentials.authentication.key = IOT_CONFIG_DEVICE_CERT_PRIVATE_KEY;
    mqtt_config.credentials.authentication.key_len = (size_t)sizeof(IOT_CONFIG_DEVICE_CERT_PRIVATE_KEY);
  #else // Using SAS key
    mqtt_config.credentials.authentication.password = (const char*)az_span_ptr(sasToken.Get());
  #endif

    mqtt_config.session.keepalive = 30;
    mqtt_config.session.disable_clean_session = 0;
    mqtt_config.network.disable_auto_reconnect = false;
    mqtt_config.broker.verification.certificate = (const char*)ca_pem;
    mqtt_config.broker.verification.certificate_len = (size_t)ca_pem_len;
#else // ESP_ARDUINO_VERSION_MAJOR
  mqtt_config.uri = mqtt_broker_uri;
  mqtt_config.port = mqtt_port;
  mqtt_config.client_id = mqtt_client_id;
  mqtt_config.username = mqtt_username;

#ifdef IOT_CONFIG_USE_X509_CERT
  Logger.Info("MQTT client using X509 Certificate authentication");
  mqtt_config.client_cert_pem = IOT_CONFIG_DEVICE_CERT;
  mqtt_config.client_key_pem = IOT_CONFIG_DEVICE_CERT_PRIVATE_KEY;
#else // Using SAS key
  mqtt_config.password = (const char*)az_span_ptr(sasToken.Get());
#endif

  mqtt_config.keepalive = 30;
  mqtt_config.disable_clean_session = 0;
  mqtt_config.disable_auto_reconnect = false;
  mqtt_config.event_handle = mqtt_event_handler;
  mqtt_config.user_context = NULL;
  mqtt_config.cert_pem = (const char*)ca_pem;
#endif // ESP_ARDUINO_VERSION_MAJOR

  mqtt_client = esp_mqtt_client_init(&mqtt_config);

  if (mqtt_client == NULL)
  {
    Logger.Error("Failed creating mqtt client");
    return 1;
  }

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_mqtt_client_register_event(mqtt_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
#endif // ESP_ARDUINO_VERSION_MAJOR

  esp_err_t start_result = esp_mqtt_client_start(mqtt_client);

  if (start_result != ESP_OK)
  {
    Logger.Error("Could not start mqtt client; error code:" + start_result);
    return 1;
  }
  else
  {
    Logger.Info("MQTT client started");
    return 0;
  }
}

/*
 * @brief           Gets the number of seconds since UNIX epoch until now.
 * @return uint32_t Number of seconds.
 */
static uint32_t getEpochTimeInSecs() { return (uint32_t)time(NULL); }

static void establishConnection()
{
  connectToWiFi();
  initializeTime();
  initializeIoTHubClient();
  (void)initializeMqttClient();
}

static void generateTelemetryPayload()
{
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  int mq135Raw = analogRead(MQ135_PIN);
  bool mq6Triggered = digitalRead(MQ6_DO_PIN) == LOW;

  updatePredictiveController(mq135Raw, mq6Triggered, humidity);

  uint32_t sequence = telemetry_send_count++;
  uint32_t timestampUtc = getEpochTimeInSecs();

  String rtcTimestamp = "null";

  if (rtc_found)
  {
    DateTime now = rtc.now();

    char rtcBuffer[24];
    snprintf(
      rtcBuffer,
      sizeof(rtcBuffer),
      "%04d-%02d-%02dT%02d:%02d:%02d",
      now.year(),
      now.month(),
      now.day(),
      now.hour(),
      now.minute(),
      now.second()
    );

    rtcTimestamp = "\"" + String(rtcBuffer) + "\"";
  }

  String temperatureJson =
    isnan(temperature) ? "null" : String(temperature, 1);

  String humidityJson =
    isnan(humidity) ? "null" : String(humidity, 1);

  String mq135FilteredJson =
    isnan(mq135_filtered) ? "null" : String(mq135_filtered, 1);

  String mq135BaselineJson =
    isnan(mq135_baseline) ? "null" : String(mq135_baseline, 1);

  String mq135DeltaJson =
    (!baseline_ready || isnan(mq135_filtered) || isnan(mq135_baseline))
      ? "null"
      : String(mq135_filtered - mq135_baseline, 1);

  String mq135ProjectedJson =
    isnan(mq135_projected_5min) ? "null" : String(mq135_projected_5min, 1);

  String humidityFilteredJson =
    isnan(humidity_filtered) ? "null" : String(humidity_filtered, 1);

  String humidityBaselineJson =
    isnan(humidity_baseline) ? "null" : String(humidity_baseline, 1);

  String humidityDeltaJson =
    (!baseline_ready || isnan(humidity_filtered) || isnan(humidity_baseline))
      ? "null"
      : String(humidity_filtered - humidity_baseline, 1);

  String humidityProjectedJson =
    isnan(humidity_projected_5min)
      ? "null"
      : String(humidity_projected_5min, 1);

  telemetry_payload = "{";
  telemetry_payload += "\"sequence\":" + String(sequence) + ",";
  telemetry_payload += "\"timestampUtc\":" + String(timestampUtc) + ",";
  telemetry_payload += "\"rtcLocal\":" + rtcTimestamp + ",";
  telemetry_payload += "\"temperatureC\":" + temperatureJson + ",";
  telemetry_payload += "\"humidityPct\":" + humidityJson + ",";
  telemetry_payload += "\"mq135Raw\":" + String(mq135Raw) + ",";
  telemetry_payload += "\"mq135Filtered\":" + mq135FilteredJson + ",";
  telemetry_payload += "\"mq135Baseline\":" + mq135BaselineJson + ",";
  telemetry_payload += "\"mq135Delta\":" + mq135DeltaJson + ",";
  telemetry_payload += "\"mq135SlopePerMin\":" +
    String(mq135_slope_per_minute, 2) + ",";
  telemetry_payload += "\"mq135Projected5Min\":" + mq135ProjectedJson + ",";
  telemetry_payload += "\"humidityFiltered\":" + humidityFilteredJson + ",";
  telemetry_payload += "\"humidityBaseline\":" + humidityBaselineJson + ",";
  telemetry_payload += "\"humidityDelta\":" + humidityDeltaJson + ",";
  telemetry_payload += "\"humiditySlopePerMin\":" +
    String(humidity_slope_per_minute, 2) + ",";
  telemetry_payload += "\"humidityProjected5Min\":" +
    humidityProjectedJson + ",";
  telemetry_payload += "\"mq6Triggered\":";
  telemetry_payload += mq6Triggered ? "true," : "false,";
  telemetry_payload += "\"label\":\"" + current_label + "\",";
  telemetry_payload += "\"baselineReady\":";
  telemetry_payload += baseline_ready ? "true," : "false,";
  telemetry_payload += "\"baselineSamples\":" +
    String(baseline_sample_count) + ",";
  telemetry_payload += "\"predictionState\":\"" +
    prediction_state + "\",";
  telemetry_payload += "\"predictionReason\":\"" +
    prediction_reason + "\",";
  telemetry_payload += "\"predictedFanStage\":" +
    String(predicted_fan_stage) + ",";
  telemetry_payload += "\"controlMode\":\"" +
    String(manual_fan_override ? "MANUAL" : "AUTO_RULES") + "\",";
  telemetry_payload += "\"fanStageCommand\":" + String(fan_stage_command) + ",";
  telemetry_payload += "\"fan1On\":";
  telemetry_payload += fan_stage_command >= 1 ? "true," : "false,";
  telemetry_payload += "\"fan2On\":";
  telemetry_payload += fan_stage_command >= 2 ? "true," : "false,";
  telemetry_payload += "\"fanControlAvailable\":true,";
  telemetry_payload += "\"demoMode\":true,";
  telemetry_payload += "\"logicVersion\":\"rules-v1\",";
  telemetry_payload += "\"firmwareVersion\":\"0.4.0-predictive-rules\"";
  telemetry_payload += "}";

  if (isnan(temperature) || isnan(humidity))
  {
    printLcdLine(1, "DHT22: FAILED");
  }
  else
  {
    printLcdLine(
      1,
      "T:" + String(temperature, 1) +
      " H:" + String(humidity, 1)
    );
  }

  if (!baseline_ready)
  {
    printLcdLine(
      2,
      "Cal " + String(baseline_sample_count) +
      "/" + String(BASELINE_SAMPLES_REQUIRED) +
      " MQ:" + String(mq135Raw)
    );
  }
  else
  {
    printLcdLine(
      2,
      "MQ:" + String((int)mq135_filtered) +
      " D:" + String((int)(mq135_filtered - mq135_baseline))
    );
  }

  if (manual_fan_override)
  {
    printLcdLine(3, "MANUAL Fan S" + String(fan_stage_command));
  }
  else
  {
    printLcdLine(
      3,
      "P5:S" + String(predicted_fan_stage) +
      " " + prediction_reason
    );
  }

  Logger.Info("Payload: " + telemetry_payload);
}

static void sendTelemetry()
{
  Logger.Info("Sending telemetry ...");

  uint8_t property_buffer[64];
  az_iot_message_properties properties;

  if (az_result_failed(az_iot_message_properties_init(
          &properties, AZ_SPAN_FROM_BUFFER(property_buffer), 0)) ||
      az_result_failed(az_iot_message_properties_append(
          &properties,
          AZ_SPAN_FROM_STR(AZ_IOT_MESSAGE_PROPERTIES_CONTENT_TYPE),
          AZ_SPAN_LITERAL_FROM_STR("application%2Fjson"))) ||
      az_result_failed(az_iot_message_properties_append(
          &properties,
          AZ_SPAN_FROM_STR(AZ_IOT_MESSAGE_PROPERTIES_CONTENT_ENCODING),
          AZ_SPAN_LITERAL_FROM_STR("UTF-8"))))
  {
    Logger.Error("Failed creating JSON telemetry properties");
    return;
  }

  // The topic could be obtained just once during setup,
  // however if properties are used the topic need to be generated again to reflect the
  // current values of the properties.
  if (az_result_failed(az_iot_hub_client_telemetry_get_publish_topic(
          &client, &properties, telemetry_topic, sizeof(telemetry_topic), NULL)))
  {
    Logger.Error("Failed az_iot_hub_client_telemetry_get_publish_topic");
    return;
  }

    generateTelemetryPayload();



  if (esp_mqtt_client_publish(
          mqtt_client,
          telemetry_topic,
          (const char*)telemetry_payload.c_str(),
          telemetry_payload.length(),
          MQTT_QOS1,
          DO_NOT_RETAIN_MSG)
      == 0)
  {
    Logger.Error("Failed publishing");
  }
  else
  {
    Logger.Info("Message published successfully");
  }
}

// Arduino setup and loop main functions.

void setup()
{
  initializeSensors();
  establishConnection();
  printLcdLine(3, "Azure: connected");
}

void loop()
{
  handleSerialFanCommands();

  if (WiFi.status() != WL_CONNECTED)
  {
    connectToWiFi();
  }
#ifndef IOT_CONFIG_USE_X509_CERT
  else if (sasToken.IsExpired())
  {
    Logger.Info("SAS token expired; reconnecting with a new one.");
    (void)esp_mqtt_client_destroy(mqtt_client);
    initializeMqttClient();
  }
#endif
  else if (millis() > next_telemetry_send_time_ms)
  {
    sendTelemetry();
    next_telemetry_send_time_ms = millis() + TELEMETRY_FREQUENCY_MILLISECS;
  }
}
