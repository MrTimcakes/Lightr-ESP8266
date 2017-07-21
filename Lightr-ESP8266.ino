#define DEBUG
//#define FORMAT

#ifdef DEBUG // Helper functions for debugging
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
//#include <Ticker.h>               // Non-Blocking timer
#include <ESP8266mDNS.h>          // OTA Discovery
#include <WiFiUdp.h>              // UDP for OTA
#include <ArduinoOTA.h>           // Update OTA
#include <InputDebounce.h>        // Easy Debouncing

char mqtt_server[32];  
char mqtt_port[6] = "1883";
char mqtt_username[32];
char mqtt_password[32];
char lightr_nickname[32]; // Default is set when WiFi initialized (Needs MAC Address)

WiFiClient espClient;
PubSubClient MQTTclient(espClient);
//Ticker MQTTreconnectTicker;

bool _saveConfigFlag = false;
long lastReconnectAttempt;

volatile bool _outputStatus = false; // Volatile becuase could be changed by Button Interrupt
#define _outputPin 12 // GPIO12 Relay (HIGH to turn on)
#define _outputLed 13 // Low to enable

#define BTN1 0
#define BTN2 14
#define BUTTON_DEBOUNCE_DELAY 20 // [ms]
#ifdef BTN1
  static InputDebounce BTN1Debounce;
#endif
#ifdef BTN2
  static InputDebounce BTN2Debounce;
#endif

void setup() {
  pinMode(_outputPin, OUTPUT);
  pinMode(_outputLed, OUTPUT);
  #ifdef DEBUG
    Serial.begin(115200);
  #endif

  startWiFi(); //Start WiFi + Load config, will not progress past until connected

  DEBUG_PRINTLN();
  DEBUG_PRINT("Nickname      : ");
  DEBUG_PRINTLN(lightr_nickname);
  DEBUG_PRINT("MQTT Server   : ");
  DEBUG_PRINTLN(mqtt_server);
  DEBUG_PRINT("MQTT Port     : ");
  DEBUG_PRINTLN(mqtt_port);
  DEBUG_PRINT("MQTT Username : ");
  DEBUG_PRINTLN(mqtt_username);
  DEBUG_PRINT("MQTT Password : ");
  DEBUG_PRINTLN(mqtt_password);
  DEBUG_PRINTLN();
  
  MQTTinit();
  OTAinit();
  buttonInit();

  setState(_outputStatus);
}

void loop() {
 
  MQTTtick();
  ArduinoOTA.handle();
  buttonTick();
  
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
    DEBUG_PRINTLN("MQTT BROKER VIA IP");
    DEBUG_PRINTLN(MQTT_SERVER_ADR);
  }else{
    MQTTclient.setServer(mqtt_server, atoi(mqtt_port)); // Not IP Address, assume domain#
    DEBUG_PRINT("MQTT BROKER VIA DOMAIN: ");
    DEBUG_PRINTLN(mqtt_server);
  }  
  
  MQTTclient.setCallback(mqttCallback);
  MQTTreconnect();
  //MQTTreconnectTicker.attach(5, MQTTconnect);
}

boolean MQTTreconnect() {
  if ( MQTTclient.connect(lightr_nickname, mqtt_username, mqtt_password) ) {
    DEBUG_PRINTLN("MQTT Connected");
    MQTTclient.subscribe("lights/all");
    MQTTclient.subscribe(String( "lights/" + WiFi.macAddress() ).c_str());
    MQTTclient.subscribe(String( "lights/" + WiFi.macAddress() + "/set" ).c_str());
    MQTTclient.subscribe(String( "lights/" + WiFi.macAddress() + "/nickname" ).c_str());
  }
  #ifdef DEBUG
    if(MQTTclient.state() != 0){
      DEBUG_PRINTLN( String("MQTT State: " + MQTTclient.state()).c_str() );
    }
  #endif
  return MQTTclient.connected();
}

void MQTTtick(){
  if (!MQTTclient.connected()) {
    long now = millis();
    if (now - lastReconnectAttempt > 5000) { // Has it been 5 seconds since the last reconnecet attempt?
      lastReconnectAttempt = now;
      // Attempt to reconnect
      if ( MQTTreconnect() ){
        lastReconnectAttempt = 0;
      }
    }
  }else {
    //MQTT is connected here
    MQTTclient.loop();
  }
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
    DEBUG_PRINTLN("Published to 'lights/all/status'");
    DEBUG_PRINTLN(data.c_str());
  }else
  
  if( strcmp(topic, String( "lights/" + WiFi.macAddress() ).c_str()) == 0 ){
    publishStatus( String( "lights/" + WiFi.macAddress() + "/status").c_str() );
  }else
  
  if( strcmp(topic, String( "lights/" + WiFi.macAddress() + "/set" ).c_str()) == 0 ){
    if((char)payload[0] == '1'){ setState(true); }else
    if((char)payload[0] == '0'){ setState(false); }else
    if((char)payload[0] == 't'){ setState(!_outputStatus); }
  }else
  
  if( strcmp(topic, String( "lights/" + WiFi.macAddress() + "/nickname" ).c_str()) == 0 ){
    if(length < 32){ // Nickname variable is only 32 long, new nick must be shorter
      memset(lightr_nickname, 0, sizeof(lightr_nickname)); // Clear original nickname (if new shorter woudln't overwrite)
      strncpy(lightr_nickname, (char*)payload, length); // Change the nickname variable to the new nickname

      saveConfig();
    }
  }

}

void publishStatus( const char* topic ){
  String data = JSONstatus();
  MQTTclient.publish(topic, data.c_str());
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

void setState(bool state){
  _outputStatus = state;
  digitalWrite(_outputPin, _outputStatus);
  digitalWrite(_outputLed, !_outputStatus);

  publishStatus("lights/all/status");
}

void OTAinit(){
  ArduinoOTA.setHostname(lightr_nickname);
  // ArduinoOTA.setPort(8266);
  // ArduinoOTA.setPassword((const char *)"123");
  
  ArduinoOTA.onStart([]() {
    DEBUG_PRINTLN("OTA Start");
  });
  ArduinoOTA.onEnd([]() {
    DEBUG_PRINTLN("\nOTA End");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    #ifdef DEBUG
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      Serial.println();
    #endif
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) DEBUG_PRINTLN("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) DEBUG_PRINTLN("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) DEBUG_PRINTLN("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) DEBUG_PRINTLN("Receive Failed");
    else if (error == OTA_END_ERROR) DEBUG_PRINTLN("End Failed");
  });
  ArduinoOTA.begin();
}

void buttonInit(){
  #ifdef BTN1
    BTN1Debounce.registerCallbacks(pressedCallback, releasedCallback, pressedDurationCallback);
    BTN1Debounce.setup(BTN1, BUTTON_DEBOUNCE_DELAY, InputDebounce::PIM_EXT_PULL_UP_RES                                                                                                                                                                           );
  #endif
  #ifdef BTN2
    BTN2Debounce.registerCallbacks(pressedCallback, releasedCallback, pressedDurationCallback);
    BTN2Debounce.setup(BTN2, BUTTON_DEBOUNCE_DELAY, InputDebounce::PIM_INT_PULL_UP_RES);
  #endif
}

void buttonTick(){
  unsigned long now = millis();
  #ifdef BTN1
    BTN1Debounce.process(now);
  #endif
  #ifdef BTN2
    BTN2Debounce.process(now);
  #endif
}

void pressedCallback()
{
  setState(!_outputStatus);
}

void releasedCallback()
{
  //
}

void pressedDurationCallback(unsigned long duration)
{
  //
}
