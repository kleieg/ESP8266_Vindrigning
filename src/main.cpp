
// Import required libraries
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "LittleFS.h"
#include <Arduino_JSON.h>
#include <AsyncElegantOTA.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>

#include <SoftwareSerial.h>

#include "MHZ19.h"

#include <SPI.h>
#include <Wire.h>

#include "Adafruit_SHT31.h"
#include <BME280I2C.h>

#include "VindriktningPM25.h"
#include "Settings.h"
#include "WLAN_Credentials.h"
#include "config.h"
#include "log.h"

#include "utils.h"

//<<<<<<<<<<<<<<<<
// current settings

struct PersistentState
{
  float TempOffset = 0.0f;
  float HumOffset = 0.0f;
  float PressureOffset = 0.0f;
};

PersistentState g_state;

// will be computed as "<HOSTNAME>_<MAC-ADDRESS>"
String Hostname;

// define sensors & values
Adafruit_SHT31 sht31 = Adafruit_SHT31();

BME280I2C::Settings settings(
   BME280::OSR_X1,
   BME280::OSR_X1,
   BME280::OSR_X1,
   BME280::Mode_Forced,
   BME280::StandbyTime_1000ms,
   BME280::Filter_Off,
   BME280::SpiEnable_False,
   BME280I2C::I2CAddr_0x76 // I2C address. I2C specific.
);

BME280I2C bme(settings);
bool hasSHT31 = false;

float Temp = 0;
float Hum = 0;
float Pressure = 0;
int CO2 = 0;
float Temp_mhz19 = 0;
bool heater = false;
VindriktningPM25::SensorState pm25;
int WiFi_reconnect = 0;
bool notify = false;

MHZ19 mhz19;
SoftwareSerial mhz19Serial(GPIO_MHZ19_RX, GPIO_MHZ19_TX);

long lastSensorRead = 0;

// for WiFi
WiFiClient myClient;
long lastReconnectAttempt = 0;

// for MQTT
PubSubClient client(myClient);
int Mqtt_reconnect = 0;
bool Mqtt_refresh = false;

// NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
long My_time = 0;

// Timers auxiliar variables
long now = millis();
int LEDblink = 0;
bool led = 1;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Create a WebSocket object
AsyncWebSocket ws("/ws");

// end of variables -----------------------------------------------------

// Initialize LittleFS
void initLittleFS()
{
  if (!LittleFS.begin())
  {
    LOG_PRINTLN("An error has occurred while mounting LittleFS");
  }
  else
  {
    Settings::load(g_state);
  }
  LOG_PRINTLN("LittleFS mounted successfully");
}

// Initialize WiFi
void initWiFi()
{
  // dynamically determine hostname
  Hostname = HOSTNAME;
  Hostname += "_";
  Hostname += WiFi.macAddress();
  Hostname.replace(":", "");

  WiFi.mode(WIFI_STA);
  WiFi.hostname(Hostname);
  WiFi.begin(ssid, password);
  LOG_PRINT("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED)
  {
    LOG_PRINT('.');
    delay(1000);
  }
  LOG_PRINTLN(WiFi.localIP());
}

String getOutputStates()
{
  JSONVar myArray;

  // system
  myArray["cards"][0]["c_text"] = String(Hostname) + "   /   " + String(VERSION);
  myArray["cards"][1]["c_text"] = String(WiFi.RSSI());
  myArray["cards"][2]["c_text"] = String(SENSOR_INTERVAL) + "ms";
  myArray["cards"][3]["c_text"] = String(My_time);
  myArray["cards"][4]["c_text"] = "WiFi = " + String(WiFi_reconnect) + "   MQTT = " + String(Mqtt_reconnect);
  myArray["cards"][5]["c_text"] = " to reboot click ok";

  // sensors
  myArray["cards"][6]["c_text"] = String(Temp);
  myArray["cards"][7]["c_text"] = String(Hum);
  myArray["cards"][8]["c_text"] = String(CO2);
  myArray["cards"][9]["c_text"] = String(pm25.avgPM25);
  myArray["cards"][10]["c_text"] = String(Pressure);

  // configuration
  myArray["cards"][11]["c_text"] = String(g_state.TempOffset);
  myArray["cards"][12]["c_text"] = String(g_state.HumOffset);
  myArray["cards"][13]["c_text"] = String(g_state.PressureOffset);

  // notify
  myArray["cards"][14]["c_text"] = String(notify);

  String jsonString = JSON.stringify(myArray);
  return jsonString;
}

void notifyClients(String state)
{
  ws.textAll(state);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  {
    // according to AsyncWebServer documentation this is ok
    data[len] = 0;

    LOG_PRINTLN("Data received: ");
    LOG_PRINTF("%s\n", data);

    JSONVar json = JSON.parse((const char *)data);
    if (json == nullptr)
    {
      LOG_PRINTLN("Request is not valid json, ignoring");
      return;
    }
    if (!json.hasOwnProperty("action"))
    {
      LOG_PRINTLN("Request is not valid json, ignoring");
      return;
    }
    if (!strcmp(json["action"], "states"))
    {
      notifyClients(getOutputStates());
    }
    else if (!strcmp(json["action"], "reboot"))
    {
      LOG_PRINTLN("Reset..");
      ESP.restart();
    }
    else if (!strcmp(json["action"], "settings"))
    {
      if (!json.hasOwnProperty("data"))
      {
        LOG_PRINTLN("Settings request is missing data, ignoring");
        return;
      }
      bool updated = false;
      if (json["data"].hasOwnProperty("TempOffset"))
      {
        g_state.TempOffset = (double)json["data"]["TempOffset"];
        updated = true;
      }
      if (json["data"].hasOwnProperty("HumOffset"))
      {
        g_state.HumOffset = (double)json["data"]["HumOffset"];
        updated = true;
      }
      if (updated)
      {
        Settings::save(g_state);
      }
    }
  }

  Mqtt_refresh = true;
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
  {
    LOG_PRINTF("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    break;
  }
  case WS_EVT_DISCONNECT:
    LOG_PRINTF("WebSocket client #%u disconnected\n", client->id());
    break;
  case WS_EVT_DATA:
    handleWebSocketMessage(arg, data, len);
    break;
  case WS_EVT_PONG:
  case WS_EVT_ERROR:
    break;
  }
}

void initWebSocket()
{
  ws.onEvent(onEvent);
#if defined CREDENTIALS_WEB_USER && defined CREDENTIALS_WEB_PASSWORD
  ws.setAuthentication(CREDENTIALS_WEB_USER, CREDENTIALS_WEB_PASSWORD);
#endif
  server.addHandler(&ws);
}

// reconnect to WiFi
void reconnect_wifi()
{
  LOG_PRINTF("%s\n", "WiFi try reconnect");
  WiFi.begin();
  delay(500);
  if (WiFi.status() == WL_CONNECTED)
  {
    lastReconnectAttempt = 0;
    WiFi_reconnect = WiFi_reconnect + 1;
    // Once connected, publish an announcement...
    LOG_PRINTF("%s\n", "WiFi reconnected");
  }
}

// This functions reconnects your ESP32 to your MQTT broker

void reconnect_mqtt()
{
  String willTopic = Hostname + "/LWT";
  String cmdTopic = Hostname + "/CMD/+";

#if defined CREDENTIALS_MQTT_USER && defined CREDENTIALS_MQTT_PASSWORD
  if (client.connect(Hostname.c_str(), CREDENTIALS_MQTT_USER, CREDENTIALS_MQTT_PASSWORD, willTopic.c_str(), 0, true, "Offline"))
#else
  if (client.connect(Hostname.c_str(), willTopic.c_str(), 0, true, "Offline"))
#endif
  {
    lastReconnectAttempt = 0;
    LOG_PRINTF("%s\n", "connected");

    client.publish(willTopic.c_str(), "Online", true);

    client.subscribe(cmdTopic.c_str());

    Mqtt_reconnect = Mqtt_reconnect + 1;
  }
}

// receive MQTT messages
void MQTT_callback(char *topic, byte *message, unsigned int length)
{
  LOG_PRINTF("%s", "Message arrived on topic: ");
  LOG_PRINTF("%s\n", topic);
  LOG_PRINTF("%s", "Data : ");

  String MQTT_message;
  for (size_t i = 0; i < length; i++)
  {
    MQTT_message += (char)message[i];
  }
  LOG_PRINTLN(MQTT_message);

  String notifyTopic = Hostname + "/CMD/Notify";
  String strTopic = String(topic);

  if (strTopic == notifyTopic)
  {
    if (MQTT_message == "true")
    {
      notify = true;
      digitalWrite(GPIO_LED_NOTIFY, HIGH);
    }
    else if (MQTT_message == "false")
    {
      notify = false;
      digitalWrite(GPIO_LED_NOTIFY, LOW);
    }
    Mqtt_refresh = true;
  }

  notifyClients(getOutputStates());
}

void MQTTsend()
{
  JSONVar mqtt_data, sensors, actuators;

  String mqtt_tag = Hostname + "/STATUS";
  LOG_PRINTF("%s\n", mqtt_tag.c_str());

  mqtt_data["Time"] = My_time;
  mqtt_data["RSSI"] = WiFi.RSSI();

  if(Temp > 0) {
    sensors["Temp"] = Temp;
  }
  if(Hum > 0) {
    sensors["Hum"] = Hum;
  }
  if (Pressure > 0)
  {
    sensors["Pressure"] = Pressure;
  }
  sensors["PM25"] = pm25.avgPM25;
  if(CO2 > 0) {
    sensors["CO2"] = CO2;
  }
  
  actuators["Notify"] = notify;

  mqtt_data["Sensors"] = sensors;
  mqtt_data["Actuators"] = actuators;

  String mqtt_string = JSON.stringify(mqtt_data);

  LOG_PRINTF("%s\n", mqtt_string.c_str());

  client.publish(mqtt_tag.c_str(), mqtt_string.c_str());

  notifyClients(getOutputStates());
}

void setup()
{
  // Serial port for debugging purposes
  LOG_INIT();

  delay(4000); // wait for serial log to be reday

  pinMode(GPIO_LED_NOTIFY, OUTPUT);
  digitalWrite(GPIO_LED_NOTIFY, LOW);

  LOG_PRINTLN("start init\n");
  initLittleFS();
  initWiFi();
  initWebSocket();

  LOG_PRINTLN("init sensors\n");

  mhz19Serial.begin(9600);

  mhz19.begin(mhz19Serial);
  mhz19.autoCalibration();

  VindriktningPM25::setup();

  LOG_PRINTLN("check for SHT31\n");
  if (!sht31.begin(0x44))
  {
    LOG_PRINTLN("Couldn't find SHT31");

    LOG_PRINTLN("check for BME280\n");
    if (!bme.begin())
    {
      LOG_PRINTLN("Couldn't find bme280");
      while (1)
        delay(1);
    }
    LOG_PRINTLN("found\n");
  }
  else
  {
    hasSHT31 = true;
    LOG_PRINTLN("found\n");

    LOG_PRINT("Heater Enabled State: ");
    heater = sht31.isHeaterEnabled();
    if (heater)
    {
      LOG_PRINTLN("ENABLED");
    }
    else
    {
      LOG_PRINTLN("DISABLED");
    }
  }
  LOG_PRINTLN("sensor initialized");

  LOG_PRINTF("setup MQTT\n");
  client.setServer(CREDENTIALS_MQTT_BROKER, 1883);
  client.setCallback(MQTT_callback);

  // Route for root / web page
  LOG_PRINTF("set Webpage\n");
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(LittleFS, "/index.html", "text/html", false); });

  AsyncStaticWebHandler &handler = server.serveStatic("/", LittleFS, "/");
#if defined CREDENTIALS_WEB_USER && defined CREDENTIALS_WEB_PASSWORD
  handler.setAuthentication(CREDENTIALS_WEB_USER, CREDENTIALS_WEB_PASSWORD);
#endif

  // init NTP
  LOG_PRINTF("init NTP\n");
  timeClient.begin();
  timeClient.setTimeOffset(0);

  // Start ElegantOTA
  LOG_PRINTF("start Elegant OTA\n");
  AsyncElegantOTA.begin(&server);

  // Start server
  LOG_PRINTF("start server\n");
  server.begin();

  LOG_PRINTF("setup finished\n");
}

void loop()
{
  AsyncElegantOTA.loop();
  ws.cleanupClients();

  // update UPCtime
  timeClient.update();
  My_time = timeClient.getEpochTime();

  now = millis();

  /*
  // LED blinken
  if (now - LEDblink > LED_BLINK_INTERVAL)
  {
    LEDblink = now;
    if (led == 0)
    {
      digitalWrite(GPIO_LED_INTERN, 1);
      led = 1;
    }
    else
    {
      digitalWrite(GPIO_LED_INTERN, 0);
      led = 0;
    }
  }
*/

  bool sendMQTT = false;

  // Vindriktning pm25 sensor lesen - unabhaengig von anderen sensoren, da durch externes board gesteuert
  if(VindriktningPM25::handleUart(pm25)) {
    sendMQTT = true;
  }

  if (now - lastSensorRead > SENSOR_INTERVAL)
  {
    lastSensorRead = now;
    sendMQTT = true;

    // MHZ19 lesen
    CO2 = mhz19.getCO2();
    Temp_mhz19 = mhz19.getTemperature();

    float tempRaw, humRaw;
    
    // SHT30 lesen
    if (hasSHT31)
    {
      tempRaw = sht31.readTemperature();
      humRaw = sht31.readHumidity();
      heater = sht31.isHeaterEnabled();
    }
    else
    {
      bme.read(Pressure, tempRaw, humRaw);
      heater = false;
    }

    Pressure += g_state.PressureOffset;
    Temp = tempRaw + g_state.TempOffset;
    Hum = calculateRelHumidity(Temp, calculateAbsHumidity(tempRaw, humRaw)) + g_state.HumOffset;
    
    if (!isnan(Temp))
    { // check if 'is not a number'
      LOG_PRINT("Temp *C = ");
      LOG_PRINT(Temp);
      LOG_PRINT("\t\t");
    }
    else
    {
      LOG_PRINTLN("Failed to read temperature");
    }

    if (!isnan(Hum))
    { // check if 'is not a number'
      LOG_PRINT("Hum. % = ");
      LOG_PRINTLN(Hum);
    }
    else
    {
      LOG_PRINTLN("Failed to read humidity");
    }
  }

  // check WiFi
  if (WiFi.status() != WL_CONNECTED)
  {
    // try reconnect every 5 seconds
    if (now - lastReconnectAttempt > RECONNECT_INTERVAL)
    {
      lastReconnectAttempt = now;
      // Attempt to reconnect
      LOG_PRINTF("WiFi reconnect");
      reconnect_wifi();
    }
  }
  else
  {
    // check if MQTT broker is still connected
    if (!client.connected())
    {
      // try reconnect every 5 seconds
      if (now - lastReconnectAttempt > RECONNECT_INTERVAL)
      {
        lastReconnectAttempt = now;
        // Attempt to reconnect
        LOG_PRINTF("MQTT reconnect");
        reconnect_mqtt();
      }
    }
    else
    {
      // Client connected
      client.loop();

      // send data to MQTT broker
      if (sendMQTT || Mqtt_refresh)
      {
        Mqtt_refresh = false;
        MQTTsend();
      }
    }
  }

  // update Webpage and wait
  notifyClients(getOutputStates());
  delay(2000);
}