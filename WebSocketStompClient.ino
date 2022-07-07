#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WebSocketsClient.h>
#include <StompClient.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "env.h";


// Variables

// WiFi name and password
const char* ssid = SSID;
const char* password = PASSWORD;

// Soft AP name and password
const char* softAPSSID = "Testing";
const char* softAPPassword = "Te$ter123";

const char* host = HOST;
const int port = PORT;

const char* stompUrl = STOMP_URL;

const char* organizationId = ORGANIZATION_ID;
const char* deviceId = DEVICE_ID;

WebSocketsClient webSocket;
bool ledState = 0;
unsigned long lastInterval = 0;

Stomp::StompClient stompClient(webSocket, host, port, stompUrl, true);
String message = "";

AsyncWebServer httpServer(80);

bool shouldReset = 0;

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
  Serial.println("Connecting to WiFi");

  Serial.printf("SSID - %s ::: Password - %s\n", wifiName, wifiPassword);

  WiFi.begin(wifiName, wifiPassword);

  if(WiFi.waitForConnectResult() != WL_CONNECTED) {
    return false;
  }

  Serial.println("Connected to WiFi");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  return true;
}


void handleConnect(Stomp::StompCommand cmd){
  stompClient.sendMessage("/ace/test", "Test string");
  String destination = "/controlData/organizations/"+ String(organizationId) + "/devices/" + String(deviceId);
  stompClient.subscribe((char*)destination.c_str(), Stomp::CLIENT, handleControlMessage);
  Serial.println("Connected to STOMP broker");
}

void handleError(const Stomp::StompCommand cmd){
  Serial.println("ERROR: "+ cmd.body);
}

void handleDisconnect(Stomp::StompCommand cmd){
  Serial.println("Disconnected");
}

void sendMessageAfterInterval(unsigned long timeout) {
  if(millis() > (timeout + lastInterval) && WiFi.isConnected()){
    int paramValue = random(10, 30);
    message = "{\\\"data\\\": {\\\"paramName\\\": \\\"Temperature\\\",\\\"paramValue\\\": "+String(paramValue)+",\\\"createdAt\\\": "+String(lastInterval)+"},\\\"message\\\":\\\"New data from NodeMCU\\\"}";
    String destination = "/ace/data/organizations/"+ String(organizationId) + "/devices/" + String(deviceId);
    stompClient.sendMessage(destination, message);
    lastInterval = millis();
  }
}

Stomp::Stomp_Ack_t handleControlMessage(Stomp::StompCommand cmd){
  Serial.println("Received control");
  ledState ^=1;
  Serial.print("Led state: ");
  Serial.println(ledState);
  digitalWrite(LED_BUILTIN, ledState);
  return Stomp::CONTINUE;
}

// HTTP Callbacks

void handleHomeRoute(AsyncWebServerRequest* request) {
  request->send(200, "text/plain", "Welcome to Ace");
}


void handleStateRoute(AsyncWebServerRequest* request){
  int params = request->params();

  if(params == 0) {
    request->redirect("/dashboard");
    return;
  }

  if(request->hasParam("v")){
    AsyncWebParameter* stateParam = request->getParam("v");
    if (stateParam->value().equals("0")){
      ledState = LOW;
      digitalWrite(LED_BUILTIN, ledState);
    }
    else if (stateParam->value().equals("1")){
      ledState = HIGH;
      digitalWrite(LED_BUILTIN, ledState);
    }
  }

  request->redirect("/dashboard");
  return;
}

void handleConnectRoute(AsyncWebServerRequest* request) {
  short params =  request->params();
  if(params == 0) {
    request->redirect("/index.html");
    return;
  }

  if(request->hasParam("ssid") && request->hasParam("password")) {
    String newSSID = request->getParam("ssid")->value();
    String newPassword =  request->getParam("password")->value();

    bool isConnected = connectToWifi(newSSID.c_str(), newPassword.c_str());

    String message = "";
    if(isConnected) {
      message = "Connected successfully to " + newSSID + "\n";
      IPAddress newIp = WiFi.localIP();
      message += "New IP Address: " + newIp.toString();
      request->send(200, "text/plain", message);
    }
    else {
      message = "Failed to connect to " + newSSID + "\n";
    }

    request->send(200, "text/plain", message);
  }
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();
  
  Serial.println("Setting up AP");
  WiFi.mode(WIFI_AP_STA);

  setUpSoftAP(softAPSSID, softAPPassword);
    
  bool isConnected = connectToWifi(ssid, password);

  if(isConnected) {
    stompClient.onConnect(handleConnect);
    stompClient.onError(handleError);

    stompClient.begin();
    Serial.println("Initialize WS connection");
  }
  else {
    Serial.printf("Failed to connect to %s\n", ssid);
  }

  pinMode(LED_BUILTIN, OUTPUT);

  Serial.println("Mounting SPIFFS");

  if(!SPIFFS.begin()){
    Serial.println("Failed to mount SPIFFS");
  }

  Serial.println("Mounted SPIFFS");

  Serial.println("Setting up web server");

  httpServer.serveStatic("/", SPIFFS, "/static/").setDefaultFile("index.html");
  httpServer.on("/home", HTTP_GET, handleHomeRoute);
  httpServer.on("/state", HTTP_GET, handleStateRoute);
  httpServer.on("/connect", HTTP_GET, handleConnectRoute);

  httpServer.begin();
}

void loop() {
  if(!WiFi.isConnected()){
    webSocket.loop();
    sendMessageAfterInterval(5000);
  }
}
