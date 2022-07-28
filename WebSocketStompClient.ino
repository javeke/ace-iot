#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266HTTPClient.h>
#include <WebSocketsClient.h>
#include <StompClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>

// For OLED Display
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// For MPU Sensor
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

#include <TinyGPSPlus.h>
#include <SoftwareSerial.h>

#include "env.h";

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # 
#define SCREEN_ADDRESS 0x3C


#define INITIAL_SCREEN_TOOLBAR "Disconnected Temp:30*C"
#define INITIAL_SCREEN_BODY "Loading..."

#define HEALTH_CHECK_PIN 14

#define PUSH_DATA_TIMEOUT 2000
#define MPU_TIMEOUT 100
#define GPS_TIMEOUT 750 

#define GPS_TX_PIN 13
#define GPS_RX_PIN 12

#define SERIAL_BAUD 115200
#define GPS_BAUD 9600

// Variables

// WiFi name and password
String ssid = WIFI_NAME;
String password = PASSWORD;

// Soft AP name and password
const char* softAPSSID = "Testing";
const char* softAPPassword = "Te$ter123";

String host = HOST;
int port = PORT;

String stompUrl = STOMP_URL;

int dataSubscription = 0;

String organizationId = ORGANIZATION_ID;
String deviceId = DEVICE_ID;
String deviceName = "";

WebSocketsClient webSocket;
bool ledState = HIGH;
unsigned long lastInterval = 0;


bool IS_SOCKJS = true;
Stomp::StompClient* stompClient = new Stomp::StompClient(webSocket, host.c_str(), port, stompUrl.c_str(), IS_SOCKJS);
String message = "";

ESP8266WebServer httpServer(80);

bool shouldReset = 0;

String fileMetadata = "";
StaticJsonDocument<512> secretJsonData;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
String screenToolbar = INITIAL_SCREEN_TOOLBAR;
String screenBody = INITIAL_SCREEN_BODY;

Adafruit_MPU6050 mpu;
sensors_event_t accel, gyro, temp;
float temperature = 0.0;
int mpuInterval = 0; 
int gpsInterval = 0;
float gpsLat = 0.0;
float gpsLng = 0.0;

TinyGPSPlus gps;
SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);


// Functions

bool setUpSoftAP(const char* softAPName, const char* softAPPasscode){
  bool isReady = WiFi.softAP(softAPName, softAPPasscode);
  if(isReady) {
    Serial.println("Soft AP configured");
    IPAddress softAPIP = WiFi.softAPIP();
    Serial.print("Soft AP Ip Address: ");
    Serial.println(softAPIP);
    return true;
  }
  else {
    Serial.println("Failed to set up soft AP.");
    return false;
  }
}

bool connectToWifi(const char* wifiName, const char* wifiPassword){
  Serial.println("Scanning WiFi");

  WiFi.disconnect();
  delay(500);
  
  bool isFound = false;

  int networks = WiFi.scanNetworks(false, true);
  Serial.print("Found: ");
  Serial.println(networks);

  for(int index = 0; index< networks; index++) {
    String name = WiFi.SSID(index);
    Serial.print(index +1);
    Serial.print(": ");
    Serial.println(name);
    if(name.equals(wifiName)) {
      Serial.printf("Found %s\n", wifiName);
      isFound = true;
      break;
    }
  }

  if(!isFound) {
    Serial.printf("Cannot find %s\n", wifiName);
    return false;
  }

  Serial.println("Connecting to WiFi");
  WiFi.begin(wifiName, wifiPassword);
  
  if (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    return false;
  }

  Serial.println("Connected to WiFi");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

void getDeviceDataFromServer(){
  WiFiClient wifiClient;
  HTTPClient httpClient;
  Serial.println("Fetching device data from server");
  if(httpClient.begin(wifiClient, "http://"+host+":"+String(port)+"/api/organizations/"+organizationId+"/devices/"+deviceId+"/info")){
    int responseCode = httpClient.GET();

    if(responseCode < 0){
      Serial.println("Response Code: " + responseCode);
      Serial.println("Failed to get device data from server");
    }
    else {
      if(responseCode == HTTP_CODE_OK){
        String responseBody = httpClient.getString();
        StaticJsonDocument<1024> jsonBody;
        deserializeJson(jsonBody, responseBody);
        deviceName = String((const char*)jsonBody["name"]);
        ledState = !((bool)jsonBody["enabled"]);
        digitalWrite(BUILTIN_LED, ledState);
      }
    }
  }
}

void loadConfigDataFromFS(){
  if(SPIFFS.exists("/metadata.txt")){
    File metadata = SPIFFS.open("/metadata.txt", "r");
    if(!metadata){
      Serial.println("No metadata available");
    }
    else {
      if(metadata.available()){
        fileMetadata = metadata.readString();

        if((fileMetadata.length() > 0) && (fileMetadata.indexOf(",") != -1)){
          fileMetadata.trim();

          organizationId = fileMetadata.substring(0,1);

          deviceId = fileMetadata.substring(2);

          Serial.printf("Metadata found. OrganizationId - %s\tDeviceId - %s\n", organizationId.c_str(), deviceId.c_str());
        }
      }
      metadata.close();
    }
  }

  if(SPIFFS.exists("/secrets.json")){
    File secretsFile = SPIFFS.open("/secrets.json", "r");

    if(!secretsFile) { 
      Serial.println("No secrets file found");
    }
    else {
      Serial.println("Secrets file found");
      deserializeJson(secretJsonData, secretsFile);

      ssid = String((const char*)(secretJsonData["ssid"]));
      password = String((const char*)secretJsonData["password"]);
      host = String((const char*)secretJsonData["serverHost"]);
      port = (int)secretJsonData["serverPort"];

      delete stompClient;
      stompClient = new Stomp::StompClient(webSocket, host.c_str(), port, stompUrl.c_str(), IS_SOCKJS);
      secretsFile.close();
    }
  }
}

void displayOnOLED(int delayTime, String toolbar = INITIAL_SCREEN_TOOLBAR, String message = INITIAL_SCREEN_BODY){
  display.clearDisplay();
  display.setCursor(0,0);
  display.println(toolbar);
  display.setCursor(0,12);
  display.println(message);
  display.display();
  delay(delayTime);
}

void readMPU(){
  if(millis() > (MPU_TIMEOUT + mpuInterval)){
    mpu.getEvent(&accel, &gyro, &temp);
    temperature = temp.temperature;
    mpuInterval = millis();
  }
}

void readGPS(){
  if(millis() > (gpsInterval + GPS_TIMEOUT)){
    while (gpsSerial.available() > 0)
    {
      if(gps.encode(gpsSerial.read())){
        if(gps.location.isValid()){
          gpsLat = gps.location.lat();
          gpsLng = gps.location.lng();
        }
        else {
          Serial.println("Invalid GPS Data");
        }
      }
    }
    gpsInterval = millis(); 
  }  
}

void sendMessageAfterInterval(unsigned long timeout) {
  if(millis() > (timeout + lastInterval) && WiFi.isConnected()){
    message = "{\\\"data\\\": {\\\"paramName\\\": \\\"Temperature\\\",\\\"paramValue\\\": "+String((int)temperature)+",\\\"createdAt\\\": "+String(lastInterval)+"},\\\"message\\\":\\\"New data from NodeMCU\\\"}";
    String destination = "/ace/data/organizations/"+ organizationId + "/devices/" + deviceId;
    stompClient->sendMessage(destination, message);
    lastInterval = millis();
  }
}

// Stomp Handlers

Stomp::Stomp_Ack_t handleControlMessage(Stomp::StompCommand cmd){
  Serial.println("Received control");
  ledState ^=1;
  Serial.print("Led state: ");
  Serial.println(ledState);
  digitalWrite(LED_BUILTIN, ledState);
  screenBody = ledState ? "Turning off LED" : "Turning on LED";
  displayOnOLED(100, screenToolbar, screenBody);
  return Stomp::CONTINUE;
}

void handleConnect(Stomp::StompCommand cmd){
  stompClient->sendMessage("/ace/test", "Test string");
  String destination = "/controlData/organizations/"+ organizationId + "/devices/" + deviceId;
  dataSubscription = stompClient->subscribe((char*)destination.c_str(), Stomp::CLIENT, handleControlMessage);
  displayOnOLED(1000, screenToolbar, F("Connected to STOMP broker"));
}

void handleDisconnect(Stomp::StompCommand cmd){
  Serial.println("Disconnected");
}


void handleError(const Stomp::StompCommand cmd){
  Serial.println("ERROR: "+ cmd.body);
}

void setUpStompHandlers(){
  stompClient->onConnect(handleConnect);
  stompClient->onError(handleError);
  stompClient->onDisconnect(handleDisconnect);
}




// HTTP Callbacks

void handleHomeRoute() {
  String message = "Welcome to Ace for device "+deviceId;
  httpServer.send(200, "text/plain", message);
}


void handleStateRoute(){
  int params = httpServer.args();

  if(params == 0) {
    httpServer.sendHeader("Location", "/dashboard.html", true);
    httpServer.send(302, "text/plain", "");
    return;
  }

  if(httpServer.hasArg("v")){
    
    for(int i = 0; i< httpServer.args(); i++){
      if(httpServer.argName(i).equals("v")){
        String stateParamValue = httpServer.arg(i);
        if (stateParamValue.equals("0")){
          ledState = LOW;
          digitalWrite(LED_BUILTIN, ledState);
        }
        else if (stateParamValue.equals("1")){
          ledState = HIGH;
          digitalWrite(LED_BUILTIN, ledState);
        }
      }
    }
  }

  httpServer.sendHeader("Location", "/dashboard.html", true);
  httpServer.send(302, "text/plain", "");
  return;
}

void handleConnectRoute() {
  short params =  httpServer.args();
  if(params == 0) {
    httpServer.sendHeader("Location", "/index.html", true);
    httpServer.send(302, "text/plain", "");
    return;
  }

  if(httpServer.hasArg("ssid") && httpServer.hasArg("password")) {
    String newSSID = "";
    String newPassword =  "";
    
    int found = 0;

    for(int i = 0; i< httpServer.args(); i++){
      if(httpServer.argName(i).equals("ssid")) {
        newSSID = httpServer.arg(i);
        found++;
      }

      if(httpServer.argName(i).equals("password")) {
        newPassword = httpServer.arg(i);
        found++;
      }
    }
    
    if(found != 2) {
      httpServer.send(400, "text/plain", "Bad Request");
      return;
    }

    bool isConnected = connectToWifi(newSSID.c_str(), newPassword.c_str());

    String message = "";
    if(isConnected) {
      message = "Connected successfully to " + newSSID + "\n";
      IPAddress newIp = WiFi.localIP();
      message += "New IP Address: " + newIp.toString();
    }
    else {
      message = "Failed to connect to " + newSSID + "\n";
    }

    httpServer.send(200, "text/plain", message);
    return;
  }
  httpServer.send(400, "text/plain", "Bad Request");
  return;
}

void handleConfigureRoute(){
  int args = httpServer.args();

  String message = "No configuration changes";

  if(args == 0){
    httpServer.send(200, "text/plain", message);
    return;
  }

  message = "";

  if(httpServer.hasArg("deviceId")){
    for(int i=0; i<args; i++){
      if(httpServer.argName(i).equals("deviceId")){
        deviceId = httpServer.arg(i);
        String deviceMessage = "Updated device Id to" + deviceId + "\n";

        File metadata = SPIFFS.open("/metadata.txt", "w+");

        String newData = organizationId + "," + deviceId;

        int written = metadata.print(newData);

        if(written == newData.length()){

          String destination = "/controlData/organizations/"+ organizationId + "/devices/" + deviceId;
          stompClient->unsubscribe(dataSubscription);
          dataSubscription = stompClient->subscribe((char*)destination.c_str(), Stomp::CLIENT, handleControlMessage);

          Serial.println("Updated the metadata file");
          deviceMessage += "Metadata file found and updated\n";
        }
        else {
          Serial.println("Failed to update the metadata file");
          deviceMessage += "Metadata file update failed\n";
        }

        message += deviceMessage;
        break;
      }
    }
  }

  if(httpServer.hasArg("organizationId")){
    for(int i=0; i<args; i++){
      if(httpServer.argName(i).equals("organizationId")){
        organizationId = httpServer.arg(i);
        String orgMessage = "Updated organization Id to" + organizationId + "\n";

        File metadata = SPIFFS.open("/metadata.txt", "w+");

        String newData = organizationId + "," + deviceId;

        int written = metadata.print(newData);

        if(written == newData.length()){

          String destination = "/controlData/organizations/"+ organizationId + "/devices/" + deviceId;
          stompClient->unsubscribe(dataSubscription);
          dataSubscription = stompClient->subscribe((char*)destination.c_str(), Stomp::CLIENT, handleControlMessage);
          
          Serial.println("Updated the metadata file");
          orgMessage += "Metadata file found and updated\n";
        }
        else {
          Serial.println("Failed to update the metadata file");
          orgMessage += "Metadata file update failed\n";
        }
        message += orgMessage;
        break;
      }
    }
  }

  if(httpServer.hasArg("server")){
    for(int i=0; i<args; i++){
      if(httpServer.argName(i).equals("server")){
        host = httpServer.arg(i);
        String hostMessage = "Updated host to" + host + "\n";

        File secretsFile = SPIFFS.open("/secrets.json", "r+");

        deserializeJson(secretJsonData, secretsFile);
        secretJsonData["serverHost"] = host.c_str();
        port = (int)secretJsonData["serverPort"];
        serializeJson(secretJsonData, secretsFile);
        secretsFile.close();

        secretsFile = SPIFFS.open("/secrets.json", "w+");
        serializeJson(secretJsonData, secretsFile);
        secretsFile.close();

        delete stompClient;
        stompClient = new Stomp::StompClient(webSocket, host.c_str(), port, stompUrl.c_str(), IS_SOCKJS);

        stompClient->onConnect(handleConnect);
        stompClient->onError(handleError);
      
        stompClient->begin();
        Serial.println("Reinitialize WS connection");

        Serial.println("Updated the host in the secrets file");
        hostMessage += "Secrets file found and updated with host\n";
        message += hostMessage;
        break;
      }
    }
  }

  if(httpServer.hasArg("port")){
    for(int i=0; i<args; i++){
      if(httpServer.argName(i).equals("port")){
        String portArg = httpServer.arg(i);
        String portMessage = "Updated port to" + portArg + "\n";

        File secretsFile = SPIFFS.open("/secrets.json", "r+");

        deserializeJson(secretJsonData, secretsFile);

        port = atoi(portArg.c_str());

        secretJsonData["serverPort"] = port;
        host = String((const char*)secretJsonData["serverHost"]);
        
        serializeJson(secretJsonData, secretsFile);
        secretsFile.close();

        secretsFile = SPIFFS.open("/secrets.json", "w+");
        serializeJson(secretJsonData, secretsFile);
        secretsFile.close();

        delete stompClient;
        stompClient = new Stomp::StompClient(webSocket, host.c_str(), port, stompUrl.c_str(), IS_SOCKJS);

        stompClient->onConnect(handleConnect);
        stompClient->onError(handleError);
      
        stompClient->begin();
        Serial.println("Reinitialize WS connection");

        Serial.println("Updated the port in the secrets file");
        portMessage += "Secrets file found and updated with port\n";
        message += portMessage;
        break;
      }
    }
  }

  if(message.length()==0){
    message = "No configuration changes";
  }
  else {
    getDeviceDataFromServer();
  }
  httpServer.send(200, "text/plain", message);
}

void setUpServerRoutes(){
  Serial.println("Setting up web server");

  httpServer.on("/home", handleHomeRoute);
  httpServer.on("/state", handleStateRoute);
  httpServer.on("/connect", handleConnectRoute);
  httpServer.on("/configure", handleConfigureRoute);
  httpServer.serveStatic("/", SPIFFS, "/", "no-cache");
}


void setup() {
  // put your setup code here, to run once:
  Serial.begin(SERIAL_BAUD);
  Serial.println();

  gpsSerial.begin(GPS_BAUD);

  pinMode(HEALTH_CHECK_PIN, OUTPUT);
  digitalWrite(HEALTH_CHECK_PIN, HIGH);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, ledState);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    while(1); // Don't proceed, loop forever
  }

  display.clearDisplay();
  display.setTextSize(2);             // Normal 1:1 pixel scale
  display.setTextColor(SSD1306_WHITE);   

  display.println("ACE Device");
  display.display();
  delay(2000);  
  
  display.setTextSize(1);
  displayOnOLED(1000, screenToolbar, F("Mounting SPIFFS"));

  if(!SPIFFS.begin()){
    displayOnOLED(1000, screenToolbar, F("Failed to mount SPIFFS"));
  }
  else {
    displayOnOLED(1000, screenToolbar, F("Mounted SPIFFS"));
    loadConfigDataFromFS();
  }
  
  displayOnOLED(1000, screenToolbar, F("Setting up AP"));
  WiFi.mode(WIFI_AP_STA);

  setUpSoftAP(softAPSSID, softAPPassword);
    
  bool isConnected = connectToWifi(ssid.c_str(), password.c_str());

  if(!isConnected) {
    Serial.printf("Failed to connect to %s\n", ssid.c_str());
  }
  else {
    getDeviceDataFromServer();
  }

  setUpStompHandlers();

  stompClient->begin();
  displayOnOLED(1000, screenToolbar, F("Initialize WS connection"));

  setUpServerRoutes();

  if(MDNS.begin(softAPSSID)){
    displayOnOLED(1000, screenToolbar, F("MDNS responder started"));
  }

  if(!mpu.begin()){
    displayOnOLED(1000, screenToolbar, F("MPU init failed. Please reset"));
    while(1);
  }
  readMPU();
  displayOnOLED(1000, screenToolbar, F("MPU started"));

  httpServer.begin();

  // Turning off this LED means the device started up okay 
  digitalWrite(HEALTH_CHECK_PIN, LOW);
}

void loop() {
  httpServer.handleClient();
  MDNS.update();
  readMPU();
  readGPS();
  if(WiFi.isConnected()){
//    webSocket.loop();
//    sendMessageAfterInterval(PUSH_DATA_TIMEOUT);
    screenToolbar = deviceName;
  }
  else {
    screenToolbar = "Disconnected";
  }
  screenToolbar += "  ";
  screenToolbar += "T:"+String(temperature, 1)+"*C";

  // Update OLED display

  screenBody = "Lat:"+String(gpsLat, 2)+" Lng:"+String(gpsLng, 2);
  displayOnOLED(1, screenToolbar, screenBody);
}
