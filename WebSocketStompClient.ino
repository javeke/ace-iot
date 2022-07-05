#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WebSocketsClient.h>
#include <StompClient.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "env.h";


// Variables

const char* ssid = SSID;
const char* password = PASSWORD;

const char* host = HOST;
const int port = PORT;

const char* stompUrl = STOMP_URL;

WebSocketsClient webSocket;
bool ledState = 0;
unsigned long lastInterval = 0;

Stomp::StompClient stompClient(webSocket, host, port, stompUrl, true);
String message = "";

AsyncWebServer httpServer(80);

bool shouldReset = 0;

// Functions

void handleConnect(Stomp::StompCommand cmd){
  stompClient.sendMessage("/ace/test", "Test string");
  stompClient.subscribe("/controlData/organizations/1/devices/2", Stomp::CLIENT, handleControlMessage);
  Serial.println("Connected to STOMP broker");
}

void handleError(const Stomp::StompCommand cmd){
  Serial.println("ERROR: "+ cmd.body);
}

void handleDisconnect(Stomp::StompCommand cmd){
  Serial.println("Disconnected");
}

void sendMessageAfterInterval(unsigned long timeout) {
  if(millis() > (timeout + lastInterval)){
    int paramValue = random(10, 30);
    message = "{\\\"data\\\": {\\\"paramName\\\": \\\"Temperature\\\",\\\"paramValue\\\": "+String(paramValue)+",\\\"createdAt\\\": "+String(lastInterval)+"},\\\"message\\\":\\\"New data from NodeMCU\\\"}";
    stompClient.sendMessage("/ace/data/organizations/1/devices/2", message);
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


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();
  
  Serial.println("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while(WiFi.status() != WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }

  Serial.println("Connected to WiFi");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  stompClient.onConnect(handleConnect);
  stompClient.onError(handleError);

  stompClient.begin();
  Serial.println("Initialize WS connection");

  pinMode(LED_BUILTIN, OUTPUT);

  Serial.println("Mounting SPIFFS");

  if(!SPIFFS.begin()){
    Serial.println("Failed to mount SPIFFS");
  }

  Serial.println("Mounted SPIFFS");

  Serial.println("Setting up web server");

  httpServer.on("/", HTTP_GET, handleHomeRoute);
  httpServer.serveStatic("/", SPIFFS, "/static/").setDefaultFile("index.html");

  httpServer.begin();
}

void loop() {
  // put your main code here, to run repeatedly:
  webSocket.loop();
  sendMessageAfterInterval(5000);
}
