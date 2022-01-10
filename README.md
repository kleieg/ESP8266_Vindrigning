# ESP8266_Vindrigning
CO2, Temperatur und Luftfeuchte Messung mit ESP8266 im Vindrigning-Gehäuse


Für die WLAN SSID und Passwort sowie für die Adresse des MQTT Brokers wird der File WLAN_Credentials.h benötigt.
Oder das include muss entsprechend geändert werden.

Der File muss enthalten:

const char* ssid = "xxxxxx";

const char* password = "xxxxxxxxxxxx";

#define CREDENTIALS_MQTT_BROKER "xxxxxxx"

In platformio.ini ist der Include-Pfad anzupassen.

Optional können in diesem File auch die MQTT Credentials und ein Zugangsschutz für den integrierten Webserver eingetragen werden.

MQTT Credentials:

#define CREDENTIALS_MQTT_USER "xxxxxxxxx"
#define CREDENTIALS_MQTT_PASSWORD "xxxxxxxxx"

Zugangsschutz:

#define CREDENTIALS_WEB_USER "xxxxxx"
#define CREDENTIALS_WEB_PASSWORD "xxxxxx"