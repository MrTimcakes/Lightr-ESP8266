#define DEBUG
#define FORMAT

#ifdef DEBUG
 #define DEBUG_PRINTLN(x) Serial.println(x)
 #define DEBUG_PRINT(x) Serial.print(x)
#else
 #define DEBUG_PRINTLN(x)  
 #define DEBUG_PRINT(x)  
#endif

#include <FS.h>                   // Filesystem
#include <ESP8266WiFi.h>          // ESP8266 Core WiFi Library
#include <DNSServer.h>            // Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     // Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <ArduinoJson.h>          // JSON Libary https://github.com/bblanchon/ArduinoJson

void setup() {
  Serial.begin(115200);
  Serial.println();
  WiFiManager wifiManager;

  #ifdef FORMAT
    SPIFFS.format();
    wifiManager.resetSettings();
    Serial.println("FORMAT COMPLETE");
  #endif
  
  wifiManager.autoConnect( String( "Lightr " + WiFi.macAddress() ).c_str() );
  
  DEBUG_PRINTLN("Test");
  DEBUG_PRINTLN("Lightr " + WiFi.macAddress());
  //DEBUG_PRINTLN(wifiManager.ConfigPortalSSID());

  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  DEBUG_PRINTLN("Test");
  Serial.println("Test2");
  // put your main code here, to run repeatedly:
  digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  delay(500);
}
