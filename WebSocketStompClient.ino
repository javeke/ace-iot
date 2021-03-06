#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266HTTPClient.h>
#include <WebSocketsClient.h>
#include <StompClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ArduinoJson.h>

#include "env.h";

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

  Serial.printf("SSID - %s ::: Password - %s\n", wifiName, wifiPassword);

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

void sendMessageAfterInterval(unsigned long timeout) {
  if(millis() > (timeout + lastInterval) && WiFi.isConnected()){
    int paramValue = random(10, 30);
    message = "{\\\"data\\\": {\\\"paramName\\\": \\\"Temperature\\\",\\\"paramValue\\\": "+String(paramValue)+",\\\"createdAt\\\": "+String(lastInterval)+"},\\\"message\\\":\\\"New data from NodeMCU\\\"}";
    String destination = "/ace/data/organizations/"+ organizationId + "/devices/" + deviceId;
    stompClient->sendMessage(destination, message);
    lastInterval = millis();
  }
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
        ledState = (bool)jsonBody["enabled"];
        digitalWrite(BUILTIN_LED, !ledState);
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

// Stomp Handlers

Stomp::Stomp_Ack_t handleControlMessage(Stomp::StompCommand cmd){
  Serial.println("Received control");
  ledState ^=1;
  Serial.print("Led state: ");
  Serial.println(ledState);
  digitalWrite(LED_BUILTIN, ledState);
  return Stomp::CONTINUE;
}

void handleConnect(Stomp::StompCommand cmd){
  stompClient->sendMessage("/ace/test", "Test string");
  String destination = "/controlData/organizations/"+ organizationId + "/devices/" + deviceId;
  dataSubscription = stompClient->subscribe((char*)destination.c_str(), Stomp::CLIENT, handleControlMessage);
  Serial.println("Connected to STOMP broker");
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
  Serial.begin(115200);
  Serial.println();

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, ledState);

  Serial.println("Mounting SPIFFS");
  delay(100);
  if(!SPIFFS.begin()){
    Serial.println("Failed to mount SPIFFS");
  }
  else {
    delay(200);
    Serial.println("Mounted SPIFFS");
    loadConfigDataFromFS();
  }
  
  Serial.println("Setting up AP");
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
  Serial.println("Initialize WS connection");

  setUpServerRoutes();

  if(MDNS.begin(softAPSSID)){
    Serial.println("MDNS responder started");
  }

  httpServer.begin();
}

void loop() {
  httpServer.handleClient();
  MDNS.update();
  if(WiFi.isConnected()){
    webSocket.loop();
    sendMessageAfterInterval(5000);
  }
}