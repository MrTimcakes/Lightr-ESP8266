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

bool _saveConfigFlag = false;

bool _outputStatus = false;
#define _outputPin LED_BUILTIN

void setup() {
  pinMode(_outputPin, OUTPUT);
  #ifdef DEBUG
    Serial.begin(115200);
  #endif

  startWiFi(); //Start WiFi + Load config, will not progress past until connected
  
  DEBUG_PRINTLN();
  DEBUG_PRINTLN(lightr_nickname);
  DEBUG_PRINTLN(mqtt_server);
  DEBUG_PRINTLN(mqtt_port);
  DEBUG_PRINTLN(mqtt_username);
  DEBUG_PRINTLN(mqtt_password);
  DEBUG_PRINTLN();
  
  MQTTinit();
}

void loop() {

  if(MQTTclient.connected()){
    MQTTclient.loop();
  }
  
}

void startWiFi(){
  strcpy(lightr_nickname, String( "Lightr " + WiFi.macAddress() ).c_str()); // Hack to get nickname to default to "Lightr xx:xx:xx:xx:xx:xx"
  
  #ifdef FORMAT
    DEBUG_PRINTLN("FORMAT START");
    WiFiManager wifiMgr;
    SPIFFS.format();
    wifiMgr.resetSettings();
    DEBUG_PRINTLN("FORMAT COMPLETE");
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
  
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setAPStaticIPConfig(IPAddress(192,168,0,1), IPAddress(192,168,0,1), IPAddress(255,255,255,0));
  
  
  wifiManager.autoConnect(lightr_nickname);
  DEBUG_PRINTLN();
  DEBUG_PRINTLN("Successfully Connected!");
  
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_username, custom_mqtt_username.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());
  strcpy(lightr_nickname, custom_nickname.getValue());
  if(_saveConfigFlag){saveConfig();}
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

void saveConfigCallback(){
  _saveConfigFlag = true;
}

void saveConfig(){
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

void MQTTinit(){
  IPAddress MQTT_SERVER_ADR;
  if (MQTT_SERVER_ADR.fromString(mqtt_server)) {
    MQTTclient.setServer(MQTT_SERVER_ADR, atoi(mqtt_port)); // Converted to IP, Connect via IP
    DEBUG_PRINTLN("SERVER VIA IP");
  }else{
    MQTTclient.setServer(mqtt_server, atoi(mqtt_port)); // Not IP Address, assume domain#
    DEBUG_PRINT("SERVER VIA DOMAIN: ");
    DEBUG_PRINTLN(mqtt_server);
  }  
  
  MQTTclient.setCallback(mqttCallback);
  MQTTconnect();
  MQTTreconnectTicker.attach(5, MQTTconnect);
}

void MQTTconnect(){
  if(!MQTTclient.connected()){
    if(MQTTclient.connect(lightr_nickname, mqtt_username, mqtt_password)){

      DEBUG_PRINTLN("MQTT Connected");

      MQTTclient.subscribe("lights/all");                                                   // Respond to status of all
      MQTTclient.subscribe(String( "lights/" + WiFi.macAddress() ).c_str());                // Respond W/ individual status
      MQTTclient.subscribe(String( "lights/" + WiFi.macAddress() + "/set" ).c_str());       // Set to
      MQTTclient.subscribe(String( "lights/" + WiFi.macAddress() + "/nickname" ).c_str());  // Change Nickname and save config
    }
  }
  #ifdef DEBUG
    if(MQTTclient.state() != 0){
      DEBUG_PRINTLN( String("MQTT State: " + MQTTclient.state()).c_str());
    }
  #endif

}

void mqttCallback(char* topic, byte* payload, unsigned int length){

  #ifdef DEBUG
    DEBUG_PRINTLN();
    DEBUG_PRINTLN();
    DEBUG_PRINT("Topic:  ");
    DEBUG_PRINTLN(topic);
    DEBUG_PRINT("Length: ");
    DEBUG_PRINTLN(length);
    DEBUG_PRINTLN("Message: ");
    for (int i=0; i < length; i++){
      DEBUG_PRINT( (char)payload[i] );
    } 
    //DEBUG_PRINTLN( (char*)payload );
    DEBUG_PRINTLN();
    DEBUG_PRINTLN();
  #endif
  
  if( strcmp(topic, "lights/all") == 0 ){
    String data = JSONstatus();
    MQTTclient.publish("lights/all/status", data.c_str());
    DEBUG_PRINTLN("Pub'd to lights/all/status");
    DEBUG_PRINTLN(data.c_str());
  }else
  
  if( strcmp(topic, String( "lights/" + WiFi.macAddress() ).c_str()) == 0 ){
    String data = JSONstatus();
    MQTTclient.publish(String( "lights/" + WiFi.macAddress() + "/status").c_str(), data.c_str());
  }else
  
  if( strcmp(topic, String( "lights/" + WiFi.macAddress() + "/set" ).c_str()) == 0 ){
    if((char)payload[0] == '1') {
      _outputStatus = true;
      digitalWrite(_outputPin, _outputStatus);
      String data = JSONstatus();
      MQTTclient.publish("lights/all/status", data.c_str());
    }else
    if((char)payload[0] == '0') {
      _outputStatus = false;
      digitalWrite(_outputPin, _outputStatus);
      String data = JSONstatus();
      MQTTclient.publish("lights/all/status", data.c_str());
    }else
    if((char)payload[0] == 't') {
      _outputStatus = !_outputStatus;
      digitalWrite(_outputPin, _outputStatus);
      String data = JSONstatus();
      MQTTclient.publish("lights/all/status", data.c_str());
    }
  }else
  
  if( strcmp(topic, String( "lights/" + WiFi.macAddress() + "/nickname" ).c_str()) == 0 ){
    if(length < 32){ // Nickname variable is only 32 long, new nick must be shorter
      memset(lightr_nickname, 0, sizeof(lightr_nickname)); // Clear original nickname (if new shorter woudln't overwrite)
      strncpy(lightr_nickname, (char*)payload, length); // Change the nickname variable to the new nickname

      saveConfig();
    }
  }

}

String JSONstatus(){
  DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["MAC"] = WiFi.macAddress();
    json["nickname"] = lightr_nickname;
    json["status"] = _outputStatus;

    String data;
    json.printTo(data);
    return data;
}

