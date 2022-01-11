
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

#include <SPI.h>
#include <Wire.h>

#include "Adafruit_SHT31.h"
#include "Adafruit_CCS811.h"

#include "Settings.h"
#include "WLAN_Credentials.h"
#include "config.h"

//<<<<<<<<<<<<<<<<
// current settings

struct PersistentState
{
  bool hasBaseline = false;
  uint16_t Baseline = 0;
  float TempOffset = 0.0f;
  float HumOffset = 0.0f;
};

PersistentState g_state;

// will be computed as "<HOSTNAME>_<MAC-ADDRESS>"
String Hostname;

// define sensors & values
Adafruit_CCS811 ccs;
Adafruit_SHT31 sht31 = Adafruit_SHT31();
float Temp = 25;
float Hum = 60;
float eCO2 = 400;
float TVOC = 0;
int Baseline = 0;
long lastBaselineUpdate = 0;
bool heater = false;

int WiFi_reconnect = 0;

// for WiFi
WiFiClient myClient;
long lastReconnectAttempt = 0;

// for MQTT
PubSubClient client(myClient);
long Mqtt_lastSend = 0;
int Mqtt_reconnect = 0;

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
    Serial.println("An error has occurred while mounting LittleFS");
  }
  else
  {
    Settings::load(&g_state);
  }
  Serial.println("LittleFS mounted successfully");
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
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
}

String getOutputStates()
{
  JSONVar myArray;

  // system
  myArray["cards"][0]["c_text"] = String(Hostname) + "   /   " + String(VERSION);
  myArray["cards"][1]["c_text"] = String(WiFi.RSSI());
  myArray["cards"][2]["c_text"] = String(MQTT_INTERVAL) + "ms";
  myArray["cards"][3]["c_text"] = String(My_time);
  myArray["cards"][4]["c_text"] = "WiFi = " + String(WiFi_reconnect) + "   MQTT = " + String(Mqtt_reconnect);
  myArray["cards"][5]["c_text"] = " to reboot click ok";

  // sensors
  myArray["cards"][6]["c_text"] = String(Temp);
  myArray["cards"][7]["c_text"] = String(Hum);
  myArray["cards"][8]["c_text"] = String(eCO2);
  myArray["cards"][9]["c_text"] = String(TVOC);
  myArray["cards"][10]["c_text"] = String(Baseline);
  myArray["cards"][11]["c_text"] = String(heater ? "true" : "false");

  // configuration
  myArray["cards"][12]["c_text"] = String(g_state.TempOffset);
  myArray["cards"][13]["c_text"] = String(g_state.HumOffset);

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

    Serial.println("Data received: ");
    Serial.printf("%s\n", data);

    JSONVar json = JSON.parse((const char *)data);
    if (json == nullptr)
    {
      Serial.println("Request is not valid json, ignoring");
      return;
    }
    if (!json.hasOwnProperty("action"))
    {
      Serial.println("Request is not valid json, ignoring");
      return;
    }
    if (!strcmp(json["action"], "states"))
    {
      notifyClients(getOutputStates());
    }
    else if (!strcmp(json["action"], "reboot"))
    {
      Serial.println("Reset..");
      ESP.restart();
    }
    else if (!strcmp(json["action"], "settings"))
    {
      if (!json.hasOwnProperty("data"))
      {
        Serial.println("Settings request is missing data, ignoring");
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
        Settings::save(&g_state);
      }
    }
  }

  Mqtt_lastSend = now - MQTT_INTERVAL - 10; // --> MQTT send !!
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
  {
    Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    break;
  }
  case WS_EVT_DISCONNECT:
    Serial.printf("WebSocket client #%u disconnected\n", client->id());
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
  Serial.printf("%s\n", "WiFi try reconnect");
  WiFi.begin();
  delay(500);
  if (WiFi.status() == WL_CONNECTED)
  {
    lastReconnectAttempt = 0;
    WiFi_reconnect = WiFi_reconnect + 1;
    // Once connected, publish an announcement...
    Serial.printf("%s\n", "WiFi reconnected");
  }
}

// This functions reconnects your ESP32 to your MQTT broker

void reconnect_mqtt()
{
  String willTopic = Hostname + "/LWT";
#if defined CREDENTIALS_MQTT_USER && defined CREDENTIALS_MQTT_PASSWORD
  if (client.connect(Hostname.c_str(), CREDENTIALS_MQTT_USER, CREDENTIALS_MQTT_PASSWORD, willTopic.c_str(), 0, true, "Offline"))
#else
  if (client.connect(Hostname.c_str(), willTopic.c_str(), 0, true, "Offline"))
#endif
  {
    lastReconnectAttempt = 0;
    Serial.printf("%s\n", "connected");

    client.publish(willTopic.c_str(), "Online", true);

    Mqtt_reconnect = Mqtt_reconnect + 1;
  }
}

// receive MQTT messages
void MQTT_callback(char *topic, byte *message, unsigned int length)
{
  Serial.printf("%s", "Message arrived on topic: ");
  Serial.printf("%s\n", topic);
  Serial.printf("%s", "Data : ");

  String MQTT_message;
  for (size_t i = 0; i < length; i++)
  {
    MQTT_message += (char)message[i];
  }
  Serial.println(MQTT_message);

  notifyClients(getOutputStates());
}

void MQTTsend()
{
  JSONVar mqtt_data;

  String mqtt_tag = Hostname + "/SENSOR";
  Serial.printf("%s\n", mqtt_tag.c_str());

  mqtt_data["Time"] = My_time;
  mqtt_data["RSSI"] = WiFi.RSSI();

  mqtt_data["Temp"] = Temp;
  mqtt_data["Hum"] = Hum;
  mqtt_data["eCO2"] = eCO2;
  mqtt_data["TVOC"] = TVOC;

  String mqtt_string = JSON.stringify(mqtt_data);

  Serial.printf("%s\n", mqtt_string.c_str());

  client.publish(mqtt_tag.c_str(), mqtt_string.c_str());

  notifyClients(getOutputStates());
}

void setup()
{
  // Serial port for debugging purposes
  Serial.begin(115200);
  delay(4000); // wait for serial log to be reday

  Serial.println("start init\n");
  initLittleFS();
  initWiFi();
  initWebSocket();

  Serial.println("init sensors\n");

  Serial.println("check for SHT30\n");
  if (!sht31.begin(0x44))
  {
    Serial.println("Couldn't find SHT30");
    while (1)
      delay(1);
  }
  Serial.println("found\n");

  Serial.println("check for CSS811\n");
  if (!ccs.begin())
  {
    Serial.println("Couldn't find CCS811");
    while (1)
      delay(1);
  }
  Serial.println("found\n");
  ccs.setDriveMode(CCS811_DRIVE_MODE_1SEC);

  Serial.println("calibrate CSS811\n");
  //calibrate temperature sensor
  while (!ccs.available())
  {
    delay(3);
  }
  if (g_state.hasBaseline)
  {
    ccs.setBaseline(g_state.Baseline);
  }
  ccs.setEnvironmentalData(Hum, Temp);
  float temp = ccs.calculateTemperature();
  ccs.setTempOffset(temp - 25.0);
  Serial.println("done\n");

  Serial.print("Heater Enabled State: ");
  heater = sht31.isHeaterEnabled();
  if (heater)
  {
    Serial.println("ENABLED");
  }
  else
  {
    Serial.println("DISABLED");
  }
  Serial.println("sensor initialized");

  Serial.printf("setup MQTT\n");
  client.setServer(CREDENTIALS_MQTT_BROKER, 1883);

  // Route for root / web page
  Serial.printf("set Webpage\n");
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(LittleFS, "/index.html", "text/html", false); });

  AsyncStaticWebHandler &handler = server.serveStatic("/", LittleFS, "/");
  #if defined CREDENTIALS_WEB_USER && defined CREDENTIALS_WEB_PASSWORD
    handler.setAuthentication(CREDENTIALS_WEB_USER, CREDENTIALS_WEB_PASSWORD);
  #endif

  // init NTP
  Serial.printf("init NTP\n");
  timeClient.begin();
  timeClient.setTimeOffset(0);

  // Start ElegantOTA
  Serial.printf("start Elegant OTA\n");
  AsyncElegantOTA.begin(&server);

  // Start server
  Serial.printf("start server\n");
  server.begin();

  Serial.printf("setup finished\n");
}

void loop()
{
  AsyncElegantOTA.loop();
  ws.cleanupClients();

  // update UPCtime
  timeClient.update();
  My_time = timeClient.getEpochTime();

  // LED blinken
  now = millis();

  if (now - LEDblink > LED_BLINK_INTERVAL)
  {
    LEDblink = now;
    if (led == 0)
    {
      digitalWrite(GPIO_LED, 1);
      led = 1;
    }
    else
    {
      digitalWrite(GPIO_LED, 0);
      led = 0;
    }
  }
  // CCS811 lesen
  if (ccs.available())
  {

    // pass in recent env data to increase precision
    ccs.setEnvironmentalData(Hum, Temp);

    // seems like this call is needed to get reasonable values
    ccs.calculateTemperature();

    if (ccs.readData())
    {
      Serial.print("eCO2: ");
      eCO2 = ccs.geteCO2();
      Serial.print(eCO2);
      Serial.print(" ppm, TVOC: ");
      TVOC = ccs.getTVOC();
      Serial.print(TVOC);
      Serial.println(" ppb");
      if (now - lastBaselineUpdate > BASELINE_INTERVAL)
      {
        lastBaselineUpdate = now;
        Baseline = ccs.getBaseline();
        if (!g_state.hasBaseline || g_state.Baseline != Baseline)
        {
          Serial.println("Updated CCS baseline");
          g_state.Baseline = Baseline;
          g_state.hasBaseline = true;
          Settings::save(&g_state);
        }
      }
    }
    else
    {
      Serial.println("ERROR!");
    }
  }

  // SHT30 lesen
  Temp = sht31.readTemperature() + g_state.TempOffset;
  Hum = sht31.readHumidity() + g_state.HumOffset;
  heater = sht31.isHeaterEnabled();
  
  if (!isnan(Temp))
  { // check if 'is not a number'
    Serial.print("Temp *C = ");
    Serial.print(Temp);
    Serial.print("\t\t");
  }
  else
  {
    Serial.println("Failed to read temperature");
  }

  if (!isnan(Hum))
  { // check if 'is not a number'
    Serial.print("Hum. % = ");
    Serial.println(Hum);
  }
  else
  {
    Serial.println("Failed to read humidity");
  }

  // check WiFi
  if (WiFi.status() != WL_CONNECTED)
  {
    // try reconnect every 5 seconds
    if (now - lastReconnectAttempt > RECONNECT_INTERVAL)
    {
      lastReconnectAttempt = now;
      // Attempt to reconnect
      Serial.printf("WiFi reconnect");
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
        Serial.printf("MQTT reconnect");
        reconnect_mqtt();
      }
    }
    else
    {
      // Client connected
      client.loop();

      // send data to MQTT broker
      if (now - Mqtt_lastSend > MQTT_INTERVAL)
      {
        Mqtt_lastSend = now;
        MQTTsend();
      }
    }
  }

  // update Webpage and wait
  notifyClients(getOutputStates());
  delay(2000);
}