


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

#include "WLAN_Credentials.h"


//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<   Anpassungen !!!!
// set hostname used for MQTT tag and WiFi 
#define HOSTNAME "Vindrig_1"
#define VERSION "v 0.9.0"

// variables to connects to  MQTT broker
const char* mqtt_server = "192.168.178.15";
const char* willTopic = "tele/Vindrig_1/LWT";       // muss mit HOSTNAME passen !!!  tele/HOSTNAME/LWT    !!!

//<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<   Anpassungen Ende !!!!

// define sensors
Adafruit_CCS811 ccs;
Adafruit_SHT31 sht31 = Adafruit_SHT31();
float Temp;
float Hum;
float eCO2;
float TVOC;
float CCStemp;
char* CCSheater;


int WiFi_reconnect = 0;

// for MQTT
byte willQoS = 0;
const char* willMessage = "Offline";
boolean willRetain = true;
std::string mqtt_tag;
int Mqtt_sendInterval = 120000;   // in milliseconds = 2 minutes
long Mqtt_lastScan = 0;
long lastReconnectAttempt = 0;
int Mqtt_reconnect = 0;

// NTP
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
long My_time = 0;

// Initializes the espClient. 
WiFiClient myClient;
PubSubClient client(myClient);
// name used as Mqtt tag
std::string gateway = HOSTNAME ;  

// Timers auxiliar variables
long now = millis();
char strtime[8];
int LEDblink = 0;
bool led = 1;
int gpioLed = 2;
int LedBlinkTime = 500;
int RelayResetTime = 5000;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Create a WebSocket object
AsyncWebSocket ws("/ws");


// end of definitions -----------------------------------------------------


// Initialize LittleFS
void initLittleFS() {
  if (!LittleFS.begin()) {
    Serial.println("An error has occurred while mounting LittleFS");
  }
  Serial.println("LittleFS mounted successfully");
}

// Initialize WiFi
void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
}

String getOutputStates(){
  JSONVar myArray;

  myArray["cards"][0]["c_text"] = String(HOSTNAME) + "   /   " + String(VERSION);
  myArray["cards"][1]["c_text"] = willTopic;
  myArray["cards"][2]["c_text"] = String(WiFi.RSSI());
  myArray["cards"][3]["c_text"] = String(Mqtt_sendInterval) + "ms";
  myArray["cards"][4]["c_text"] = String(My_time);
  myArray["cards"][5]["c_text"] = "WiFi = " + String(WiFi_reconnect) + "   MQTT = " + String(Mqtt_reconnect);
  myArray["cards"][6]["c_text"] = String(CCSheater);
  myArray["cards"][7]["c_text"] = " to reboot click ok";

  myArray["cards"][8]["c_text"] = String(Temp);
  myArray["cards"][9]["c_text"] = String(Hum);
  myArray["cards"][10]["c_text"] = String(eCO2);
  myArray["cards"][11]["c_text"] = String(TVOC);
  myArray["cards"][12]["c_text"] = String(CCStemp);

  String jsonString = JSON.stringify(myArray);
  return jsonString;
}

void notifyClients(String state) {
  ws.textAll(state);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    
    data[len] = 0;
    char help[30];
    
    for (int i = 0; i <= len; i++){
      help[i] = data[i];
    }

    Serial.println("Data received: ");
    Serial.printf("%s\n",help);

    if (strcmp((char*)data, "states") == 0) {
      notifyClients(getOutputStates());
    }
    else{
      if (strcmp((char*)data, "Reboot") == 0) {
        Serial.println("Reset..");
        ESP.restart();
      }
      else {

      }
    }
  }

  Mqtt_lastScan = now - Mqtt_sendInterval - 10;  // --> MQTT send !!
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
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

void initWebSocket() {
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

// reconnect to WiFi 
void reconnect_wifi() {
  Serial.printf("%s\n","WiFi try reconnect"); 
  WiFi.begin();
  delay(500);
  if (WiFi.status() == WL_CONNECTED) {
    WiFi_reconnect = WiFi_reconnect + 1;
    // Once connected, publish an announcement...
    Serial.printf("%s\n","WiFi reconnected"); 
  }
}

// This functions reconnects your ESP32 to your MQTT broker

void reconnect_mqtt() {
  if (client.connect(gateway.c_str(), willTopic, willQoS, willRetain, willMessage)) {
    // Once connected, publish an announcement...
    Serial.printf("%s\n","Mqtt connected"); 
    mqtt_tag = gateway + "/connect";
    client.publish(mqtt_tag.c_str(),"connected");
    Serial.printf("%s",mqtt_tag.c_str());
    Serial.printf("%s\n","connected");
    mqtt_tag = "tele/" + gateway  + "/LWT";
    client.publish(mqtt_tag.c_str(),"Online",willRetain);
    Serial.printf("%s",mqtt_tag.c_str());
    Serial.printf("%s\n","Online");

    mqtt_tag = "cmnd/" + gateway + "/#";
    // client.subscribe(mqtt_tag.c_str());                                  nur falls MQTT-messages empfangen werden sollen
    Mqtt_reconnect = Mqtt_reconnect + 1;
  }
}

// receive MQTT messages
void MQTT_callback(char* topic, byte* message, unsigned int length) {
  
  Serial.printf("%s","Message arrived on topic: ");
  Serial.printf("%s\n",topic);
  Serial.printf("%s","Data : ");

  String MQTT_message;
  for (int i = 0; i < length; i++) {
    MQTT_message += (char)message[i];
  }
  Serial.println(MQTT_message);

  notifyClients(getOutputStates());

}

void MQTTsend () {
  JSONVar mqtt_data; 
  
  mqtt_tag = "tele/" + gateway + "/SENSOR";
  Serial.printf("%s\n",mqtt_tag.c_str());

  mqtt_data["Time"] = My_time;
  mqtt_data["RSSI"] = WiFi.RSSI();

  mqtt_data["Temp"] = Temp;
  mqtt_data["Hum"] = Hum;
  mqtt_data["eCO2"] = eCO2;
  mqtt_data["TVOC"] = TVOC;
  mqtt_data["CCStemp"] = CCStemp;


  String mqtt_string = JSON.stringify(mqtt_data);

  Serial.printf("%s\n",mqtt_string.c_str()); 

  client.publish(mqtt_tag.c_str(), mqtt_string.c_str());

  notifyClients(getOutputStates());
}

void setup(){
  // Serial port for debugging purposes
  Serial.begin(115200);
  delay (4000);                    // wait for serial log to be reday

  Serial.println("start init\n");
  initLittleFS();
  initWiFi();
  initWebSocket();

  Serial.println("init sensors\n");

  Serial.println("check for SHT30\n");
  if (! sht31.begin(0x44)) {   
    Serial.println("Couldn't find SHT30");
    while (1) delay(1);
  }
  Serial.println("found\n");

  Serial.println("check for CSS811\n");
  if(!ccs.begin()){
    Serial.println("Couldn't find CCS811");
    while(1) delay(1);
  }
  Serial.println("found\n");

  Serial.println("calibrate CSS811\n");
  //calibrate temperature sensor
  while(!ccs.available()) {
    delay(3);
  }
  float temp = ccs.calculateTemperature();
  ccs.setTempOffset(temp - 25.0);
  //ccs.setEnvironmentalData  besser TempOffset ?????=?
  Serial.println("done\n");

  Serial.print("Heater Enabled State: ");
  if (sht31.isHeaterEnabled()) {
    Serial.println("ENABLED");
    CCSheater = "enabled";
  }
  else {
    Serial.println("DISABLED");
    CCSheater = "disabled";
  }
  Serial.println("sensor initialized");

  Serial.printf("setup MQTT\n");
  client.setServer(mqtt_server, 1883);
  // client.setCallback(MQTT_callback);                                nur falls MQTT-messages empfangen werden sollen

  // Route for root / web page
  Serial.printf("set Webpage\n");
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html",false);
  });

  server.serveStatic("/", LittleFS, "/");

  // init NTP
  Serial.printf("init NTP\n");
  timeClient.begin();
  timeClient.setTimeOffset(0);

  // Start ElegantOTA
  Serial.printf("sstart Elegant OTA\n");
  AsyncElegantOTA.begin(&server);
  
  // Start server
  Serial.printf("start server\n");
  server.begin();

  Serial.printf("setup finished\n");
}

void loop() {
    AsyncElegantOTA.loop();
    ws.cleanupClients();

  // update UPCtime
    timeClient.update();
    My_time = timeClient.getEpochTime();

  // LED blinken
    now = millis();

    if (now - LEDblink > LedBlinkTime) {
      LEDblink = now;
      if(led == 0) {
       digitalWrite(gpioLed, 1);
       led = 1;
      }else{
       digitalWrite(gpioLed, 0);
       led = 0;
      }
    }
  // CCS811 lesen
    if(ccs.available()){

      CCStemp = ccs.calculateTemperature();
      if(ccs.readData()){
        Serial.print("eCO2: ");
        eCO2 = ccs.geteCO2();
        Serial.print(eCO2);
        Serial.print(" ppm, TVOC: ");      
        TVOC = ccs.getTVOC();
        Serial.print(TVOC);
        Serial.print(" ppb   Temp:");
        Serial.println(CCStemp);
      }
      else{
        Serial.println("ERROR!");
      }
    }

  // SHT30 lesen
    Temp = sht31.readTemperature();
    Hum = sht31.readHumidity();

    if (! isnan(Temp)) {  // check if 'is not a number'
      Serial.print("Temp *C = "); Serial.print(Temp); Serial.print("\t\t");
    } else { 
      Serial.println("Failed to read temperature");
    }
    
    if (! isnan(Hum)) {  // check if 'is not a number'
      Serial.print("Hum. % = "); Serial.println(Hum);
    } else { 
      Serial.println("Failed to read humidity");
    }

    // check WiFi
    if (WiFi.status() != WL_CONNECTED  ) {
      // try reconnect every 5 seconds
      if (now - lastReconnectAttempt > 5000) {
        lastReconnectAttempt = now;              // prevents mqtt reconnect running also
        // Attempt to reconnect
        Serial.printf("WiFi reconnect"); 
        reconnect_wifi();
      }
    }

  // check if MQTT broker is still connected
    if (!client.connected()) {
      // try reconnect every 5 seconds
      if (now - lastReconnectAttempt > 5000) {
        lastReconnectAttempt = now;
        // Attempt to reconnect
        Serial.printf("MQTT reconnect"); 
        reconnect_mqtt();
      }
    } else {
      // Client connected

      client.loop();

      // send data to MQTT broker
      if (now - Mqtt_lastScan > Mqtt_sendInterval) {
      Mqtt_lastScan = now;
      MQTTsend();
      } 
    }

  // update Webpage and wait 
  notifyClients(getOutputStates());
  delay(2000);
}