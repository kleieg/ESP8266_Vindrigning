# ESP8266_Vindrigning
CO2, Temperatur und Luftfeuchte Messung mit ESP8266 im Vindrigning-Gehäuse

für die WLAN SSID und Passwort wird der File WLAN_Credentials.h benötigt.
Oder das include muss entsprechend geändert werden.

Der File muss enthalten:
const char* ssid = "xxxxxx";
const char* password = "xxxxxxxxxxxx";

In platformio.ini ist der Include-Pfad anzupassen.
