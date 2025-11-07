#include <WiFi.h>
#include <PubSubClient.h>
#include "env.h"

// CẤU HÌNH WIFI
const char* ssid = WIFI_SSID;       
const char* password = WIFI_PASSWORD; 

// CẤU HÌNH MQTT
const char* mqtt_server = "192.168.137.1";    
const int mqtt_port = 1883;                   
const char* mqtt_topic = "iot/parking/slots"; 

// CẤU HÌNH SENSOR
const int sensor1Pin = 34;  // Cảm biến hồng ngoại slot 1
const int sensor2Pin = 25;  // Cảm biến hồng ngoại slot 2

// TRẠNG THÁI
bool lastState1 = false;  // Trạng thái trước đó của slot 1
bool lastState2 = false;  // Trạng thái trước đó của slot 2

// ID SLOT
const char* slot1ID = "A1";
const char* slot2ID = "A2";

// THỜI GIAN DEBOUNCE
unsigned long lastDebounceTime1 = 0;
unsigned long lastDebounceTime2 = 0;
const unsigned long debounceDelay = 500; // 500ms debounce

// MQTT CLIENT 
WiFiClient espClient;  // WiFi client thông thường 
PubSubClient mqttClient(espClient);

// RECONNECT INTERVAL
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectInterval = 5000; // Thử kết nối lại mỗi 5s

void setup() {
  Serial.begin(115200);
  Serial.println("NODE ESP32 - Parking Slot Sensor");

  // Cấu hình pin sensor
  pinMode(sensor1Pin, INPUT);
  pinMode(sensor2Pin, INPUT);
  
  Serial.println("Sensors initialized");

  // Kết nối WiFi
  connectWiFi();
  
  // Cấu hình MQTT 
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setBufferSize(512);  //buffer size
  mqttClient.setKeepAlive(60);  // Keep alive 60s
  mqttClient.setSocketTimeout(30);  // Socket timeout 30s
  
  Serial.println("MQTT configured (broker.emqx.io:1883)");
  
  // Kết nối MQTT 
  Serial.println("\nConnecting to MQTT broker...");
  while (!mqttClient.connected()) {
    if (reconnectMQTT()) {
      Serial.println("Initial MQTT connection successful!");
      break;
    }
    Serial.println("Retrying in 2 seconds...");
    delay(2000);
  }
  
  Serial.println("=================================\n");
  Serial.println("System ready! Monitoring sensors...\n");
}

void loop() {
  // Kiểm tra và duy trì kết nối
  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > reconnectInterval) {
      lastReconnectAttempt = now;
      if (reconnectMQTT()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    mqttClient.loop();
  }

  // Đọc trạng thái cảm biến
  checkSensor(sensor1Pin, slot1ID, lastState1, lastDebounceTime1);
  checkSensor(sensor2Pin, slot2ID, lastState2, lastDebounceTime2);

  delay(100); // Delay
}

void connectWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("MAC address: ");
    Serial.println(WiFi.macAddress());
  } else {
    Serial.println("\nWiFi connection failed!");
    Serial.println("Restarting in 5 seconds...");
    delay(5000);
    ESP.restart();
  }
}

bool reconnectMQTT() {
  Serial.print("Connecting to MQTT broker...");
  Serial.print("\nServer: ");
  Serial.println(mqtt_server);
  Serial.print("Port: ");
  Serial.println(mqtt_port);
  
  // Tạo client ID unique
  String clientId = "ESP32_Node_";
  clientId += String(random(0xffff), HEX);
  
  Serial.print("Client ID: ");
  Serial.println(clientId);
  Serial.print("Attempting connection...");
  
  bool connected = mqttClient.connect(clientId.c_str());
  
  if (connected) {
    Serial.println(" ✓ Connected!");
       
    // Gửi message online khi kết nối
    publishStatus("system", "online");
    
    return true;
  } else {
    Serial.print("Failed, rc=");
    Serial.print(mqttClient.state());

    return false;
  }
}

void checkSensor(int sensorPin, const char* slotID, bool &lastState, unsigned long &lastDebounceTime) {
  // Đọc trạng thái cảm biến
  // LOW = có vật thể, HIGH = không có vật thể
  bool currentState = (digitalRead(sensorPin) == LOW);
  
  // Kiểm tra nếu trạng thái thay đổi
  if (currentState != lastState) {
    unsigned long now = millis();
    
    // Debounce
    if (now - lastDebounceTime > debounceDelay) {
      lastDebounceTime = now;
      lastState = currentState;
      
      // Chỉ gửi nếu MQTT đã kết nối
      if (mqttClient.connected()) {
        // Gửi trạng thái mới lên MQTT
        publishSlotStatus(slotID, currentState);
      } else {
        Serial.print("[");
        Serial.print(slotID);
        Serial.println("] State changed but MQTT not connected, will retry...");
      }
    }
  }
}

void publishSlotStatus(const char* slotID, bool isOccupied) {
  // Double check MQTT connection
  if (!mqttClient.connected()) {
    Serial.println("MQTT disconnected in publishSlotStatus!");
    return;
  }

  // Tạo JSON message
  // Format: {"slot":"A1","occupied":true}
  char message[100];
  snprintf(message, sizeof(message), 
           "{\"slot\":\"%s\",\"occupied\":%s}", 
           slotID, 
           isOccupied ? "true" : "false");

  // Publish lên MQTT
  bool success = mqttClient.publish(mqtt_topic, message, false);

  // Log kết quả
  Serial.print("[");
  Serial.print(slotID);
  Serial.print("] ");
  Serial.print(isOccupied ? "OCCUPIED" : "FREE");
  Serial.print(" → ");
  
  if (success) {
    Serial.print("✓ Published to '");
    Serial.print(mqtt_topic);
    Serial.print("': ");
    Serial.println(message);
  } else {
    Serial.println("✗ Publish failed!");
    Serial.print("   MQTT State: ");
    Serial.println(mqttClient.state());
  }
}

void publishStatus(const char* status_type, const char* value) {
  char message[100];
  snprintf(message, sizeof(message), 
           "{\"type\":\"%s\",\"value\":\"%s\"}", 
           status_type, 
           value);
  
  mqttClient.publish(mqtt_topic, message);
  Serial.print("Status: ");
  Serial.println(message);
}
