#include <Arduino.h>
#include "WiFi.h"
#include <HTTPClient.h>
#include <WebServer.h>
#include <PubSubClient.h>

#define WIFI_SSID "foobar"
#define WIFI_PASSWORD "foo-bar-baz"
#define WIFI_HOSTNAME "ESP32-smart-irrigation"
#define WEB_SERVER_PORT 80

#define SerialMon Serial // Set serial for debug console
// #define SerialAT Serial2 // Set serial for AT commands (to the GSM module)

#define VALVE_RELAY_PIN 18 // pin 18 for  in1, pin 19 for in2
#define PUSH_BUTTON_PIN 21
#define BUZZER_PIN 23 


unsigned long MAX_WATERING_TIME = 1 * 60 * 1000UL; // 1 minute
unsigned int WATERING_BEEP_INTERVAL = 2 * 1000; // 2 seconds
unsigned int WATERING_STATUS_UPDATE_INTERVAL = 10 * 1000; // 10 seconds

bool isWatering = false;
unsigned long startedWateringTime = 0;
unsigned long lastWateringBeepTime = 0;
unsigned long lastWateringStatusUpdateTime = 0;


String serverName = "http://192.168.100.2:8000/";
unsigned long wifiPreviousMillis = 0;
unsigned long mqttPreviousMillis = 0;
unsigned long wifiReconnectInterval = 30000; // 30 seconds
unsigned long mqttReconnectInterval = 30000; // 30 seconds


// MQTT Broker
const char *mqtt_broker = "46.101.221.32"; // digital ocean VPS server
const char *topicHardware = "topic-hardware";
const char *topicServer = "topic-server";
const char *mqtt_username = "james";
const char *mqtt_password = "Foo3bar6;;";
const int mqtt_port = 1883;


WebServer server(WEB_SERVER_PORT);
WiFiClient wifiClient;
PubSubClient pubSubClient(wifiClient);

// Functions prototypes
void turnWaterOn();
void turnWaterOff();

void processMqttMessage(String message) {
  unsigned long wateringTime = millis() - startedWateringTime;
  if(startedWateringTime == 0)  wateringTime = 0;

  unsigned long wateringTimeMinutes = wateringTime / 1000 / 60;
  unsigned long wateringTimeSeconds = (wateringTime / 1000) % 60;

  String replyMessage;

  if (message.startsWith("water on")) {
    turnWaterOn();
    replyMessage = "Water Turned ON ✅";

  } else if (message.startsWith("water off")) {
    turnWaterOff();
    replyMessage = "Water Turned OFF ❌";
    if(isWatering) replyMessage += "\nBeen running for: " + String(wateringTimeMinutes) + " minutes and " + String(wateringTimeSeconds) + " seconds";

  }else if (message.startsWith("status")) {
    String status = (digitalRead(VALVE_RELAY_PIN) == HIGH) ? "OFF ❌" : "ON ✅";

    replyMessage = "Water is currently turned " + status;
    if(isWatering) replyMessage += "\nBeen running for: " + String(wateringTimeMinutes) + " minutes and " + String(wateringTimeSeconds) + " seconds";

  }else if ( message.startsWith("debug")) {
    replyMessage = "debug info here";

  } else{
    replyMessage = "Unknown command '" + message + "'\nPlease try again!";
  }

  SerialMon.println(replyMessage);
  pubSubClient.publish(topicServer, replyMessage.c_str());

}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
    Serial.print("Message arrived in topic: ");
    Serial.println(topic);

    if(length > 32){
      Serial.println("**Long Message received: "+String(length)+" bytes**\n");
      return;
    }

    String message;
    for (int i = 0; i < length; i++) {
        message += (char) payload[i];
    }

    processMqttMessage(message);
}

void scanWiFi() {
  SerialMon.println("scan start");

  // WiFi.scanNetworks will return the number of networks found
  int n = WiFi.scanNetworks();
  SerialMon.println("scan done");
  if (n == 0) {
      SerialMon.println("no networks found");
  } else {
    SerialMon.print(n);
    SerialMon.println(" networks found");
    for (int i = 0; i < n; ++i) {
      // Print SSID and RSSI for each network found
      SerialMon.print(i + 1);
      SerialMon.print(": ");
      SerialMon.print(WiFi.SSID(i));
      SerialMon.print(" (");
      SerialMon.print(WiFi.RSSI(i));
      SerialMon.print(")");
      SerialMon.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN)?" ":"*");
      delay(10);
    }
  }
  SerialMon.println("");

}

void initWiFi() {
  // Set WiFi to station mode and disconnect from an AP if it was previously connected
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(WIFI_HOSTNAME);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  SerialMon.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    SerialMon.print('.');
    delay(1000);
  }
  SerialMon.println("\n" + String(WIFI_SSID) + " RRSI: " + String(WiFi.RSSI()));
  SerialMon.println("Hostname: " + String(WiFi.getHostname()));
  SerialMon.println("Connected IP: " + WiFi.localIP().toString());
}

void initMQTT() {
  //connecting to a mqtt broker
  pubSubClient.setServer(mqtt_broker, mqtt_port);
  pubSubClient.setCallback(mqttCallback);

  unsigned long startTime = millis();
  const unsigned long timeout = 60000; // 60 seconds timeout

  SerialMon.print("Connecting to MQTT broker..");
  while (!pubSubClient.connected()) {
    SerialMon.print(".");
    
    // Check if the timeout has been reached
    if (millis() - startTime >= timeout) {
      SerialMon.println("\nConnection attempt timed out...");
      return; // Exit the loop if the timeout is reached
    }

    // Attempt to connect to the MQTT broker
    if (pubSubClient.connect(WIFI_HOSTNAME, mqtt_username, mqtt_password)) {
      SerialMon.printf("\n%s connected to MQTT broker %s\n", WIFI_HOSTNAME, mqtt_broker);
    } else {
      SerialMon.print("\nFailed with state ");
      SerialMon.print(pubSubClient.state());
      delay(2000); // Wait before retrying
    }
  }

  // Publish and subscribe
  pubSubClient.publish(topicServer, "Smart Irrigation System powered on");
  pubSubClient.subscribe(topicHardware);
}

void checkWiFi() {
  if ((millis() - wifiPreviousMillis) > wifiReconnectInterval) {
    if (WiFi.status() != WL_CONNECTED) {
      SerialMon.print("Reconnecting to WiFi...");
      // initWiFi();
      WiFi.disconnect();
      WiFi.reconnect();

      delay(100);
      wifiPreviousMillis = millis();

      if (WiFi.status() == WL_CONNECTED) {
        SerialMon.println("Connected to WiFi");
      } else {
        SerialMon.println("Not connected to WiFi");
      }
    }
  }
}

void checkMQTT() {
  if ((millis() - mqttPreviousMillis) > mqttReconnectInterval) {
    if (!pubSubClient.connected()) {
      SerialMon.println("Reconnecting to MQTT...");
      initMQTT();

      delay(100);
      mqttPreviousMillis = millis();

      pubSubClient.loop();
    }
  }
}

void makeHttpGetRequest() {
  HTTPClient http;

  String serverPath = serverName + "foo";
  
  // Your Domain name with URL path or IP address with path
  http.begin(serverPath.c_str());

  SerialMon.println("Connecting to " + serverPath);
  
  // Send HTTP GET request
  int httpResponseCode = http.GET();
  
  if (httpResponseCode>0) {
    SerialMon.print("HTTP Response code: ");
    SerialMon.println(httpResponseCode);

    String payload = http.getString();
    Serial.println(payload);

    // int bufferSize = 2048;
    // unsigned long bytesReceived = 0;

    // char buffer[bufferSize];
    // WiFiClient * stream = http.getStreamPtr();
    // while(true) {
    //   int c = stream->readBytes(buffer, bufferSize);
    //   if (c) {
    //     bytesReceived += c;

    //   }
    // }
    // SerialMon.println("Bytes received: " + String(bytesReceived));
    // SerialMon.println("Closing connection");
  }
  else {
    SerialMon.print("Error code: ");
    SerialMon.println(httpResponseCode);
  }
  // Free resources
  http.end();
}


void waterOnBeep() {
  for (int x = 0; x < 3; x++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(40);
    digitalWrite(BUZZER_PIN, LOW);
    delay(20);
  }
}
void waterOffBeep() {
  for (int x = 0; x < 2; x++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    delay(20);
  }
}

void wateringBeep() {
  for (int x = 0; x < 1; x++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    delay(20);
  }
}

void turnWaterOn(){
  digitalWrite(VALVE_RELAY_PIN, LOW);
  waterOnBeep();

  isWatering = true;
  startedWateringTime = millis();
  lastWateringStatusUpdateTime = millis();
}

void turnWaterOff(){
  digitalWrite(VALVE_RELAY_PIN, HIGH);
  waterOffBeep();

  isWatering = false;
  startedWateringTime = 0;
}

void playWateringBeep() {
  if (millis() - lastWateringBeepTime >= WATERING_BEEP_INTERVAL) {
    lastWateringBeepTime = millis();
    wateringBeep();
  }
  
}


void checkWateringTimeout() {
  if(isWatering) playWateringBeep();
  // if (digitalRead(PUSH_BUTTON_PIN) == HIGH) {
  //   if(isWatering) turnWaterOff();
  //   else turnWaterOn();
    
  //   while(digitalRead(PUSH_BUTTON_PIN) == HIGH) {}
  // } 

  if(!isWatering) return;

  unsigned long wateringTime = millis() - startedWateringTime;
  if(startedWateringTime == 0)  wateringTime = 0;

  unsigned long wateringTimeMinutes = wateringTime / 1000 / 60;
  unsigned long wateringTimeSeconds = (wateringTime / 1000) % 60;


  if (millis() - startedWateringTime >= MAX_WATERING_TIME) {
    // automatically stop watering
    turnWaterOff();

    String replyMessage = "Water Automatically Turned OFF ❌\nWatered for: " + String(wateringTimeMinutes) + " minutes and " + String(wateringTimeSeconds) + " seconds";
    SerialMon.println(replyMessage);
    pubSubClient.publish(topicServer, replyMessage.c_str());
    return;
  }

  if (millis() - lastWateringStatusUpdateTime >= WATERING_STATUS_UPDATE_INTERVAL) {
    lastWateringStatusUpdateTime = millis();

    String replyMessage = "Water is currently turned " + String((digitalRead(VALVE_RELAY_PIN) == HIGH) ? "OFF ❌" : "ON ✅");
    replyMessage += "\nBeen running for: " + String(wateringTimeMinutes) + " minutes and " + String(wateringTimeSeconds) + " seconds";

    SerialMon.println(replyMessage);
    pubSubClient.publish(topicServer, replyMessage.c_str());
  }
}



void handleWaterOn() {
  turnWaterOn();
  server.send(200, "text/plain", "Water turned ON");
}

void handleWaterOff() {
  unsigned long wateringTime = millis() - startedWateringTime;
  if(startedWateringTime == 0)  wateringTime = 0;

  unsigned long wateringTimeMinutes = wateringTime / 1000 / 60;
  unsigned long wateringTimeSeconds = (wateringTime / 1000) % 60;

  SerialMon.println("Water Turned OFF by user \nWatered for: " + String(wateringTimeMinutes) + " minutes and " + String(wateringTimeSeconds) + " seconds");
  String message = "Water Turned OFF\nWatered for: " + String(wateringTimeMinutes) + " minutes and " + String(wateringTimeSeconds) + " seconds";
  
  turnWaterOff();

  server.send(200, "text/plain", message);
}

void handleStatus() {
  unsigned long wateringTime = millis() - startedWateringTime;
  if(startedWateringTime == 0)  wateringTime = 0;

  unsigned long wateringTimeMinutes = wateringTime / 1000 / 60;
  unsigned long wateringTimeSeconds = (wateringTime / 1000) % 60;

  String message = "Water is currently turned " + String(isWatering ? "ON" : "OFF");
  if(isWatering) message += "\nBeen watering for: " + String(wateringTimeMinutes) + " minutes and " + String(wateringTimeSeconds) + " seconds";
  SerialMon.println(message);
  server.send(200, "text/plain", message);
}

void handleDebug() {
  String debugInfo = "Debug info will go here";
  server.send(200, "text/plain", debugInfo);
}

void handleRoot() {
  String htmlPage = R"rawliteral(
    <!DOCTYPE html>
    <html lang="en">
    <head>
      <meta charset="UTF-8">
      <meta name="viewport" content="width=device-width, initial-scale=1.0">
      <title>Smart Home Irrigation</title>
      <style>
        body { font-family: Arial, sans-serif; text-align: center; }
        button { padding: 10px 20px; margin: 10px; }
        p { font-size: 18px; }
      </style>
    </head>
    <body>
      <h1>Smart Home Irrigation</h1>
      <button onclick="getStatus()">Refresh Status</button>
      <p id="status">Status: </p>
      <button onclick="controlWater('on')">Turn Water ON</button>
      <button onclick="controlWater('off')">Turn Water OFF</button>
      <p id="debug">Debug Info: </p>
      <button onclick="getDebug()">Get Debug Info</button>

      <script>
        function controlWater(action) {
          fetch('/water-' + action)
          .then(response => response.text())
          .then(data => {
            document.getElementById('status').innerText = "Status: " + data;
          });
        }

        function getStatus() {
          fetch('/status')
          .then(response => response.text())
          .then(data => {
            document.getElementById('status').innerText = "Status: " + data;
          });
        }

        function getDebug() {
          fetch('/debug')
          .then(response => response.text())
          .then(data => {
            document.getElementById('debug').innerText = "Debug Info: " + data;
          });
        }

        // Fetch initial status when the page loads
        window.onload = getStatus;
      </script>
    </body>
    </html>
  )rawliteral";

  server.send(200, "text/html", htmlPage);
}

void initServer() {
  // Set up routes
  server.on("/", handleRoot);
  server.on("/water-on", handleWaterOn);
  server.on("/water-off", handleWaterOff);
  server.on("/status", handleStatus);
  server.on("/debug", handleDebug);

  // Start server
  server.begin();
  Serial.println("Web Server started on http://" + String(WiFi.localIP().toString()) + ":" + String(WEB_SERVER_PORT));
}

void setup() {
  SerialMon.begin(115200);
  delay(10);

  pinMode(VALVE_RELAY_PIN ,OUTPUT); 
  pinMode(BUZZER_PIN, OUTPUT);
  // pinMode(PUSH_BUTTON_PIN, INPUT_PULLUP);

  digitalWrite(VALVE_RELAY_PIN, HIGH); // Normally ON Only For Chanies Relay Module 
  digitalWrite(BUZZER_PIN, LOW);

  // scanWiFi();
  initWiFi();
  initServer();
  initMQTT();

}

void loop() {
  checkWiFi();
  checkMQTT();
  checkWateringTimeout();

  // Check if data is available on the Serial Monitor side
  if (SerialMon.available()) {
    String line = SerialMon.readStringUntil('\n');

    SerialMon.println("Publishing: " + line);
    pubSubClient.publish(topicServer, line.c_str());

    // makeHttpGetRequest();
  }

  // Handle incoming client requests
  server.handleClient();
  pubSubClient.loop();
  delay(100);
}