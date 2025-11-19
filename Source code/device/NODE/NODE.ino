#include <WiFi.h>
#include <PubSubClient.h>
#include "env.h"

// Wifi
const char* ssid = WIFI_SSID;       
const char* password = WIFI_PASSWORD; 

// MQTT Topic                 
const char* mqtt_topic = "iot/parking/slots"; 

// Sensor
const int sensor1Pin = 34;  // Cảm biến hồng ngoại slot 1
const int sensor2Pin = 25;  // Cảm biến hồng ngoại slot 2

// State
bool lastState1 = false;  // Trạng thái trước đó của slot 1
bool lastState2 = false;  // Trạng thái trước đó của slot 2

// ID Slot
const char* slot1ID = "A1";
const char* slot2ID = "A2";

// Debounce
unsigned long lastDebounceTime1 = 0;
unsigned long lastDebounceTime2 = 0;
const unsigned long debounceDelay = 500; // 500ms 

// MQTT Client 
WiFiClient espClient;
PubSubClient mqtt(espClient);

// Reconnect Interval
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectInterval = 5000;

void setup() {
  Serial.begin(115200);
  Serial.println("\n=================================");
  Serial.println("PARKING SLOT SENSOR");
  Serial.println("=================================");

  pinMode(sensor1Pin, INPUT);
  pinMode(sensor2Pin, INPUT);
  
  Serial.println("[SENSOR] SUCCESS Sensors initialized");

  // Kết nối WiFi
  connectWiFi();
  
  // Cấu hình MQTT 
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setBufferSize(512);  
  mqtt.setKeepAlive(60);  
  mqtt.setSocketTimeout(30); 
  
  Serial.println("[MQTT] MQTT configured");
  
  // Kết nối MQTT 
  Serial.println("[MQTT] Connecting to MQTT broker");
  while (!mqtt.connected()) {
    if (reconnectMQTT()) {
      Serial.println("[MQTT] SUCCESS connection successful!");
      break;
    }
    Serial.println("[MQTT] Retrying in 2 seconds");
    delay(2000);
  }
  
  Serial.println("=================================");
  Serial.println("System ready");
  Serial.println("=================================");
}

void loop() {
  // Kiểm tra và duy trì kết nối
  if (!mqtt.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > reconnectInterval) {
      lastReconnectAttempt = now;
      if (reconnectMQTT()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    mqtt.loop();
  }

  // Đọc trạng thái cảm biến
  checkSensor(sensor1Pin, slot1ID, lastState1, lastDebounceTime1);
  checkSensor(sensor2Pin, slot2ID, lastState2, lastDebounceTime2);

  delay(100); // Delay
}

void connectWiFi() {
  Serial.print("[WIFI] Connecting to WiFi: ");
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
    Serial.println("\n[WIFI] SUCCESS WiFi connected");
    Serial.print("[WIFI] IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WIFI] ERROR WiFi connection failed");
    Serial.println("[WIFI] Restarting in 5 seconds");
    delay(5000);
    ESP.restart();
  }
}

bool reconnectMQTT() {
  Serial.print("[MQTT] Connecting to MQTT broker");

  // Tạo client ID unique
  String clientId = "ESP32_Node_";
  clientId += String(random(0xffff), HEX);
  
  bool connected = mqtt.connect(clientId.c_str());
  
  if (connected) {
    Serial.println("[MQTT] SUCCESS MQTT broker connected");
       
    // Gửi message online khi kết nối
    publishStatus("system", "online");
    
    return true;
  } else {
    Serial.print("[MQTT] ERROR Failed, rc=");
    Serial.print(mqtt.state());

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
      if (mqtt.connected()) {
        // Gửi trạng thái mới lên MQTT
        publishSlotStatus(slotID, currentState);
      } else {
        Serial.print("[");
        Serial.print(slotID);
        Serial.println("] State changed but MQTT not connected");
      }
    }
  }
}

void publishSlotStatus(const char* slotID, bool isOccupied) {
  // Double check MQTT 
  if (!mqtt.connected()) {
    Serial.println("[MQTT] MQTT disconnected");
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
  bool success = mqtt.publish(mqtt_topic, message, false);

  // Log kết quả
  Serial.print("[SLOT] ");
  Serial.print("[");
  Serial.print(slotID);
  Serial.print("] ");
  Serial.print(isOccupied ? "OCCUPIED" : "FREE");
  Serial.print(" → ");
  
  if (success) {
    Serial.print("[MQTT] Published to '");
    Serial.print(mqtt_topic);
    Serial.print("': ");
    Serial.println(message);
  } else {
    Serial.println("[MQTT] Publish failed");
    Serial.print("[MQTT] State: ");
    Serial.println(mqtt.state());
  }
}

void publishStatus(const char* status_type, const char* value) {
  char message[100];
  snprintf(message, sizeof(message), 
           "{\"type\":\"%s\",\"value\":\"%s\"}", 
           status_type, 
           value);
  
  mqtt.publish(mqtt_topic, message);
  Serial.print("Status: ");
  Serial.println(message);
}
