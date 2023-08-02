
// C99 libraries
#include <cstdlib>
#include <string.h>
#include <time.h>
#include <RealTimeClock.h>
#include <SPI.h>

// Libraries for MQTT client and WiFi connection
#include <WiFi.h>
#include <mqtt_client.h>

// Azure IoT SDK for C includes
#include <az_core.h>
#include <az_iot.h>
#include <azure_ca.h>

// Additional sample headers
#include <AzIoTSasToken.h>
#include <SerialLogger.h>
#include "iot_configs.h"

// Bluetooth scanning and SD card libraries
#include <Arduino.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <NimBLEDevice.h>
#include <NimBLEAdvertisedDevice.h>

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

#define MAC_ADDRESS_SIZE 6

// Translate iot_configs.h defines into variables used by the sample
static const char* ssid = IOT_CONFIG_WIFI_SSID;
static const char* password = IOT_CONFIG_WIFI_PASSWORD;
static const char* host = IOT_CONFIG_IOTHUB_FQDN;
// static const char* mqtt_broker_uri = "mqtts://" IOT_CONFIG_IOTHUB_FQDN;
static const char* device_id = IOT_CONFIG_DEVICE_ID;
// static const int mqtt_port = AZ_IOT_DEFAULT_MQTT_CONNECT_PORT;
static const int mqtt_port = 443;  // Use Port 443 instead of 8883
static const char* mqtt_broker_uri = "wss://" IOT_CONFIG_IOTHUB_FQDN; // Communicate over websockets

// Memory allocated for the sample's variables and structures.
static esp_mqtt_client_handle_t mqtt_client;
static az_iot_hub_client client;

static char mqtt_client_id[128];
static char mqtt_username[128];
static char mqtt_password[200]; 
static uint8_t sas_signature_buffer[256];
static char telemetry_topic[128];


#define INCOMING_DATA_BUFFER_SIZE 128
static char incoming_data[INCOMING_DATA_BUFFER_SIZE];

File dataFile;
char macChars[18];

// Auxiliary functions
#ifndef IOT_CONFIG_USE_X509_CERT
static AzIoTSasToken sasToken(
    &client,
    AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_KEY),
    AZ_SPAN_FROM_BUFFER(sas_signature_buffer),
    AZ_SPAN_FROM_BUFFER(mqtt_password));
#endif // IOT_CONFIG_USE_X509_CERT

// Function to connect ESP32 to wifi
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

// Function to initialize time for the serial logger, which logs events into the serial monitor
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

// Function that is called when the ESP32 receives a message from IoT Hub
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

// Event handler for when ESP32 connects to the IoT Hub MQTT Broker
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
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

  return ESP_OK;
}

// Initialize the IoT Hub Client
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

// Initialize MQTT Client on the ESP32
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

  mqtt_client = esp_mqtt_client_init(&mqtt_config);

  if (mqtt_client == NULL)
  {
    Logger.Error("Failed creating mqtt client");
    return 1;
  }

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
// static uint32_t getEpochTimeInSecs() { return (uint32_t)time(NULL); }

static void establishConnection()
{
  connectToWiFi();
  initializeTime();
  initializeIoTHubClient();
  (void)initializeMqttClient();
}



// Bluetooth scanning, SD card functions

// Function to get the Mac Address of the ESP32. This will be used as the Receptor ID
int ExtractMacAddress() {
    uint8_t mac[MAC_ADDRESS_SIZE];// 17 chars for MAC address + 1 for null character

    // Get MAC address
    esp_efuse_mac_get_default(mac);

    // Convert MAC address to string
    int result = snprintf(macChars, sizeof(macChars), "%02x:%02x:%02x:%02x:%02x:%02x",
                          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    if (result > 0 && result < sizeof(macChars)) {
        printf("MAC Address: %s\n", macChars);
    } else {
        printf("Error occurred while converting MAC Address to string\n");
    }
    return 0;
}


// Function to send the telemetry to IoT Hub
#define MAX_JSON_SIZE 512 // Define a suitable size based on your requirements
static void sendTelemetry(String deviceMac, int rssi, String payloadString, String timestamp)
{

  char telemetry_payload[MAX_JSON_SIZE];

  snprintf(telemetry_payload, MAX_JSON_SIZE, 
    "{"
    "\"macChars\": \"%s\", "
    "\"timestamp\": \"%s\", "
    "\"deviceAddress\": \"%s\", "
    "\"rssi\": %d, "
    "\"payloadString\": \"%s\""
    "}", macChars, timestamp.c_str(), deviceMac.c_str(), rssi, payloadString.c_str());

  if (az_result_failed(az_iot_hub_client_telemetry_get_publish_topic(
          &client, NULL, telemetry_topic, sizeof(telemetry_topic), NULL)))
  {
    Logger.Error("Failed az_iot_hub_client_telemetry_get_publish_topic");
    return;
  }

  if (esp_mqtt_client_publish(
        mqtt_client,
        telemetry_topic,
        telemetry_payload,
        strlen(telemetry_payload),
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

// Get the Timestamp from the DS3231 component
String getTimestamp() {
  DateTime detectedTime = rtc.now();
  char timestamp[20];
  sprintf(timestamp, "%04d-%02d-%02d %02d:%02d:%02d",
            detectedTime.year(), detectedTime.month(), detectedTime.day(), detectedTime.hour(), detectedTime.minute(), detectedTime.second());

  return String(timestamp);
}

// Function to initialize SD card, create a file and write the header lines
void setupSDCard() {
  if (!SD.begin(5)) { // replace 5 with your actual CS pin if it's different
      Serial.println("SD card initialization failed!");
      return;
  }
    String timestamp = getTimestamp();
    String filename = "/data_" + timestamp + ".csv";
    
    dataFile = SD.open(filename.c_str(), FILE_WRITE);
    if (!dataFile) {
      Serial.println("Error opening " + filename);
      return;
    }
    
    dataFile.println("receptor_id,timestamp,mac_address,RSSI,payload");
}

// Function to write the bluetooth data to the SD card
void logScanResults(String deviceMac, int rssi, String payloadString, String timestamp) {
    if (dataFile) {
 
        int bytesWritten = dataFile.printf("%s, %s, %s, %s, %d, %s\n", 
                                            macChars, 
                                            timestamp, 
                                            deviceMac, 
                                            rssi, 
                                            payloadString.c_str());
        if (bytesWritten <= 0) {
            Serial.println("Failed to write to file");
        }
        dataFile.flush(); // make sure the data gets written
    } else {
        Serial.println("File not open for writing");
    }
}


void startBLEscan() {
  NimBLEScanResults scanResults = NimBLEDevice::getScan()->start(3, false);
  NimBLEAdvertisedDevice device;

  for (int i = 0; i < scanResults.getCount(); i++) {
    device = scanResults.getDevice(i);
    
    int rssi = device.getRSSI();

    if (rssi > -76) {
      uint8_t* payload = device.getPayload();
      int payloadLength = device.getPayloadLength();
      String payloadString;
      String timestamp = getTimestamp();
      String deviceMac = device.getAddress().toString().c_str();
      for(int i = 0; i < payloadLength; i++) {
        char str[3];
        sprintf(str, "%02X", payload[i]);
        payloadString += str;
    }

    sendTelemetry(deviceMac, rssi, payloadString, timestamp);
    // logScanResults(deviceMac, rssi, payloadString, timestamp);
    }
    
  }
}

// Arduino setup and loop main functions.

void setup() { 
  ExtractMacAddress();
  Serial.begin(115200);
  establishConnection();
  setupRealTimeClock();
  Serial.println("Starting BLE scan");
  NimBLEDevice::init("");
 }


void loop()
{
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
  startBLEscan();
  delay(60000);

}
