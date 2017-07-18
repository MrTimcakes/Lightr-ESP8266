#define DEBUG
//#define FORMAT

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
#include <WiFiManager.h>          // WiFi Manager https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>          // JSON Libary https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h>         // MQTT Libary https://github.com/knolleary/pubsubclient
#include <Ticker.h>               // Non-Blocking timer

char mqtt_server[32];
char mqtt_port[6] = "1883";
char mqtt_username[32];
char mqtt_password[32];
char lightr_nickname[32];

WiFiClient espClient;
PubSubClient MQTTclient(espClient);

Ticker MQTTreconnectTicker;

void setup() {
  pinMode(BUILTIN_LED, OUTPUT);
  #ifdef DEBUG
    Serial.begin(115200);
  #endif
  
  startWiFi(); //Start WiFi + Load config, will not progress past until connected

  DEBUG_PRINTLN(lightr_nickname);
  DEBUG_PRINTLN(mqtt_server);
  DEBUG_PRINTLN(mqtt_port);
  DEBUG_PRINTLN(mqtt_username);
  DEBUG_PRINTLN(mqtt_password);
}

void loop() {
}

void startWiFi(){
  strcpy(lightr_nickname, String( "Lightr " + WiFi.macAddress() ).c_str()); // Hack to get nickname to default to "Lightr xx:xx:xx:xx:xx:xx"
  
  #ifdef FORMAT
    WiFiManager wifiMgr;
    SPIFFS.format();
    wifiMgr.resetSettings();
  #endif
  
  loadConfig(); 
  
  WiFiManager wifiManager;
  
  WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", mqtt_server, 32);
  WiFiManagerParameter custom_mqtt_port("port", "MQTT Port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_username("username", "MQTT Username", mqtt_username, 32);
  WiFiManagerParameter custom_mqtt_password("password", "MQTT Password", mqtt_password, 32);
  WiFiManagerParameter custom_nickname("nickname", "Light Nickname", lightr_nickname, 32); 
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_username);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.addParameter(&custom_nickname);
  
  wifiManager.setSaveConfigCallback(saveConfig);
  wifiManager.setAPStaticIPConfig(IPAddress(192,168,0,1), IPAddress(192,168,0,1), IPAddress(255,255,255,0));
  
  
  wifiManager.autoConnect(lightr_nickname);
  DEBUG_PRINTLN("Successfully Connected!");
  
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_username, custom_mqtt_username.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());
  strcpy(lightr_nickname, custom_nickname.getValue());
}

void loadConfig(){
  DEBUG_PRINTLN("mounting FS...");
  if (SPIFFS.begin()) {
    DEBUG_PRINTLN("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      DEBUG_PRINTLN("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        DEBUG_PRINTLN("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        //json.printTo(Serial);
        if (json.success()) {
          DEBUG_PRINTLN("\nparsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_username, json["mqtt_username"]);
          strcpy(mqtt_password, json["mqtt_password"]);
          strcpy(lightr_nickname, json["nickname"]);

        } else {
          DEBUG_PRINTLN("failed to load json config");
        }
      }
    }
  } else {
    DEBUG_PRINTLN("failed to mount FS");
  }
}

void saveConfig(){
  DEBUG_PRINTLN("saving config");
  
  DynamicJsonBuffer jsonBuffer;
  JsonObject& json = jsonBuffer.createObject();
  
  json["mqtt_server"] = mqtt_server;
  json["mqtt_port"] = mqtt_port;
  json["mqtt_username"] = mqtt_username;
  json["mqtt_password"] = mqtt_password;
  json["nickname"] = lightr_nickname;

  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    DEBUG_PRINTLN("failed to open config file for writing");
  }
  #ifdef DEBUG
    json.printTo(Serial);
  #endif
  json.printTo(configFile);
  configFile.close();
}
