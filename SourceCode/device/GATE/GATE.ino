#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>  
#include <Adafruit_NeoPixel.h> 
#include "env.h"

// IR Sensors
#define IR_SENSOR_IN   8    // Cảm biến vào 
#define IR_SENSOR_OUT  9     // Cảm biến ra 

// Servo 
#define SERVO_PIN      4     

// WS2812 RGB LED 
#define WS2812_PIN     10     // WS2812 RGB LED 
#define WS2812_COUNT   1     // Số lượng LED 

// MQTT Topics
#define TOPIC_TRIGGER_CAM_IN   "iot/parking/trigger/in"    // Gửi trigger chụp cổng vào
#define TOPIC_TRIGGER_CAM_OUT  "iot/parking/trigger/out"   // Gửi trigger chụp cổng ra
#define TOPIC_GATE_CONTROL     "iot/parking/gate/control"  // Nhận lệnh mở cổng
#define TOPIC_GATE_STATUS      "iot/parking/gate/status"   // Gửi trạng thái cổng

// Servo Config
#define SERVO_CLOSED_ANGLE  0     // Góc đóng cổng
#define SERVO_OPEN_ANGLE    90    // Góc mở cổng
#define GATE_OPEN_DURATION  5000  // Thời gian giữ cổng mở 

// Objects
WiFiClient espClient;
PubSubClient mqtt(espClient);
Servo gateServo;
Adafruit_NeoPixel rgb_led(WS2812_COUNT, WS2812_PIN, NEO_GRB + NEO_KHZ800);

// State Variables
bool gateOpen = false;
unsigned long gateOpenTime = 0;
bool waitingForOCR = false;
unsigned long ocrWaitStartTime = 0;
const unsigned long OCR_TIMEOUT = 10000;  // 10 giây timeout cho OCR
String currentDirection = "";  // "in" hoặc "out"

// Manual Gate Queue
volatile bool manualGateRequested = false;
SemaphoreHandle_t gateMutex;

// Debounce
unsigned long lastIRInTime = 0;
unsigned long lastIROutTime = 0;
const unsigned long debounceDelay = 1000;  // 1 giây

// RGB LED Functions
void setRGB(uint8_t r, uint8_t g, uint8_t b) {
  rgb_led.setPixelColor(0, rgb_led.Color(r, g, b));
  rgb_led.show();
}

void setRGB_Off() {
  setRGB(0, 0, 0);
}

void setRGB_Red() {
  setRGB(255, 0, 0);  // Đỏ - Booting/Error
}

void setRGB_Green() {
  setRGB(0, 255, 0);  // Xanh lá - Ready
}

void setRGB_Blue() {
  setRGB(0, 0, 255);  // Xanh dương - Processing
}

void setRGB_Yellow() {
  setRGB(255, 255, 0);  // Vàng - Waiting OCR
}

void setRGB_Purple() {
  setRGB(255, 0, 255);  // Tím - Manual Override
}

void setRGB_Blink(uint8_t r, uint8_t g, uint8_t b, int times = 3) {
  for (int i = 0; i < times; i++) {
    setRGB(r, g, b);
    delay(100);
    setRGB_Off();
    delay(100);
  }
}

// Fail-safe Gate Handler
void manualGateTask(void* parameter) {
  for (;;) {
    // Chờ lệnh manual 
    if (manualGateRequested) {
      // Lock mutex
      if (xSemaphoreTake(gateMutex, portMAX_DELAY)) {
        Serial.println("\n[TASK] Processing manual gate request");
        
        manualGateRequested = false;  // Reset flag
        
        // Tím = Manual Override
        setRGB_Purple();
        
        // Reset waiting state nếu đang chờ OCR
        if (waitingForOCR) {
          Serial.println("[SYSTEM] Cancelling pending OCR operation");
          waitingForOCR = false;
        }
        
        // Mở cổng ngay lập tức
        openGate();
        
        // Gửi log về server
        String message = "{\"source\":\"manual\",\"direction\":\"manual\",\"override\":true}";
        if (mqtt.connected()) {
          mqtt.publish(TOPIC_GATE_STATUS, message.c_str());
          Serial.println("[MQTT] Manual override logged to server");
        }
        
        // Release mutex
        xSemaphoreGive(gateMutex);
      }
    }
    
    // Small delay để không hog CPU
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// Setup
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=================================");
  Serial.println("IoT Parking Gate Controller");
  Serial.println("=================================");
  
  // Khởi tạo WS2812 RGB LED
  rgb_led.begin();
  rgb_led.setBrightness(50);  // 50/255 = 20% brightness 
  setRGB_Red();  // Đỏ khi khởi động
  Serial.println("[LED] SUCCESS WS2812 RGB LED initialized");
  
  // Cấu hình pins
  pinMode(IR_SENSOR_IN, INPUT);
  pinMode(IR_SENSOR_OUT, INPUT);
  
  // Khởi tạo Servo
  gateServo.attach(SERVO_PIN);
  closeGate();
  Serial.println("[SERVO] SUCCESS Servo initialized - Gate CLOSED");
  
  // Tạo mutex
  gateMutex = xSemaphoreCreateMutex();
  if (gateMutex == NULL) {
    Serial.println("[SYSTEM] ERROR Failed to create mutex!");
  } else {
    Serial.println("[SYSTEM] SUCCESS Mutex created");
  }
  
  // Kết nối WiFi
  connectWiFi();
  
  // Cấu hình MQTT
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  
  // Kết nối MQTT
  connectMQTT();
  
  // Tạo FreeRTOS Task cho manual gate handler
  xTaskCreate(
    manualGateTask,      // Function
    "ManualGateTask",    // Name
    4096,                // Stack size (bytes)
    NULL,                // Parameter
    1,                   // Priority 
    NULL                 // Task handle
  );
  Serial.println("[SYSTEM] SUCCESS Manual gate task created");
  
  // Xanh lá = System Ready
  setRGB_Blink(0, 255, 0, 5);  // Blink 5 lần
  setRGB_Green();  // Sáng xanh lá cố định
  
  Serial.println("=================================");
  Serial.println("System Ready");
  Serial.println("=================================\n");
}

void loop() {
  // Kiểm tra kết nối WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[WIFI] ERROR WiFi disconnected! Reconnecting");
    setRGB_Red();  // Đỏ khi mất WiFi
    connectWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      setRGB_Green();  // Xanh lá khi kết nối lại thành công
    }
  }
  
  // Duy trì kết nối MQTT
  if (!mqtt.connected()) {
    connectMQTT();
  }
  mqtt.loop();
  
  // Kiểm tra cảm biến IR vào khi không đang chờ OCR
  if (digitalRead(IR_SENSOR_IN) == LOW && !waitingForOCR) {
    unsigned long currentTime = millis();
    if (currentTime - lastIRInTime > debounceDelay) {
      lastIRInTime = currentTime;
      handleVehicleDetected("in");
    }
  }
  
  // Kiểm tra cảm biến IR ra khi không đang chờ OCR
  if (digitalRead(IR_SENSOR_OUT) == LOW && !waitingForOCR) {
    unsigned long currentTime = millis();
    if (currentTime - lastIROutTime > debounceDelay) {
      lastIROutTime = currentTime;
      handleVehicleDetected("out");
    }
  }
  
  // Kiểm tra timeout OCR 
  if (waitingForOCR && (millis() - ocrWaitStartTime > OCR_TIMEOUT)) {
    Serial.println("\n[SYSTEM] OCR TIMEOUT, Return to ready state");
    waitingForOCR = false;
    
    // Reset RGB LED về trạng thái sẵn sàng
    setRGB_Blink(255, 0, 0, 3);  // Red blink = Timeout
    setRGB_Green();
    
    publishGateStatus("timeout");
  }
  
  // Tự động đóng cổng sau thời gian
  if (gateOpen && (millis() - gateOpenTime > GATE_OPEN_DURATION)) {
    closeGate();
  }
  
  delay(10);
}

// WiFi Functions
void connectWiFi() {
  Serial.print("[WIFI] Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  Serial.print("Password: ");
  Serial.println(WIFI_PASSWORD);
  
  WiFi.disconnect(true);
  delay(1000);
  
  // Set WiFi mode
  WiFi.mode(WIFI_STA);
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 60) {  // Tăng lên 60 (30 giây)
    delay(500);
    Serial.print(".");
    attempts++;
    
    // Debug status
    if (attempts % 10 == 0) {
      Serial.print("\n[WiFi Status: ");
      Serial.print(WiFi.status());
      Serial.print("] ");
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] SUCCESS WiFi Connected");
    Serial.print("[WIFI] IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("[WIFI] Signal Strength (RSSI): ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("\n[WIFI] ERROR WiFi Connection Failed");
    Serial.print("[WIFI] Final Status Code: ");
    Serial.println(WiFi.status());
  }
}

// MQTT Functions
void connectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("[MQTT] Connecting to MQTT");
    
    String mac = WiFi.macAddress();
    mac.replace(":", ""); 

    String clientId = "ESP32_GATE_" + mac;
    
    if (mqtt.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD)) {
      Serial.println("[MQTT] SUCCESS Connected");
      
      // Subscribe vào topic điều khiển cổng
      mqtt.subscribe(TOPIC_GATE_CONTROL);
      Serial.print("[MQTT] SUCCESS Subscribed to: ");
      Serial.println(TOPIC_GATE_CONTROL);
      
      publishGateStatus("ready");
      
    } else {
      Serial.print("[MQTT] ERROR Failed, rc=");
      Serial.print(mqtt.state());
      Serial.println("[MQTT] Retry in 5s");
      delay(5000);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print("[MQTT] Topic: ");
  Serial.println(topic);
  Serial.print("[MQTT] Message: ");
  Serial.println(message);
  
  // Xử lý lệnh điều khiển cổng
  if (String(topic) == TOPIC_GATE_CONTROL) {
    handleGateCommand(message);
  }
}

// Gate Control Functions
void handleVehicleDetected(String direction) {
  Serial.println("\n>>> VEHICLE DETECTED <<<");
  Serial.print("Direction: ");
  Serial.println(direction == "in" ? "ENTRANCE" : "EXIT");
  
  // RGB LED: Vàng = Waiting OCR
  setRGB_Yellow();
  
  // Lưu hướng hiện tại
  currentDirection = direction;
  waitingForOCR = true;
  ocrWaitStartTime = millis();  // Bắt đầu đếm timeout
  
  // Gửi trigger chụp ảnh đến CAM
  String topic = (direction == "in") ? TOPIC_TRIGGER_CAM_IN : TOPIC_TRIGGER_CAM_OUT;
  String message = "{\"trigger\":true,\"direction\":\"" + direction + "\"}";
  
  if (mqtt.publish(topic.c_str(), message.c_str())) {
    Serial.println("[MQTT] Trigger sent to CAM");
    Serial.print("  Topic: ");
    Serial.println(topic);
    Serial.println("[SYSTEM] Waiting for OCR result");
    
    // Gửi trạng thái
    publishGateStatus("waiting_ocr");
  } else {
    Serial.println("[MQTT] ERROR Failed to send trigger");
    waitingForOCR = false;
    setRGB_Green(); 
  }
}

void handleGateCommand(String message) {
  Serial.println("\n>>> GATE COMMAND RECEIVED <<<");
  Serial.println(message);
  Serial.print("  waitingForOCR state: ");
  Serial.println(waitingForOCR ? "TRUE" : "FALSE");
  
  // Kiểm tra lệnh manual từ MONITOR
  if (message.indexOf("\"manual\"") > 0 && message.indexOf("\"source\":\"manual\"") > 0) {
    Serial.println("[MQTT] Manual override command received");
    manualGateRequested = true;  // Set flag cho task xử lý
    return;
  }
  
  if (message.indexOf("\"open\"") > 0) {
    Serial.println("[SYSTEM] Opening gate");

    openGate();
    waitingForOCR = false;
    
  } else if (message.indexOf("\"reject\"") > 0) {
    Serial.println("[SYSTEM] OCR Failed");
    waitingForOCR = false;
    
    setRGB_Blink(255, 0, 0, 3);  // Red blink = Rejected
    setRGB_Green();
  
    publishGateStatus("rejected");
  } else {
    Serial.println("[SYSTEM] ERROR  Unknown command format");
    Serial.println("Received: " + message);
  }
}

void handleManualOpen() {
  // Chỉ set flag, task sẽ xử lý
  manualGateRequested = true;
  Serial.println("[SYSTEM] Manual gate request queued");
}

void openGate() {
  if (!gateOpen) {
    Serial.println("\n>>> OPENING GATE <<<");
    
    gateServo.write(SERVO_OPEN_ANGLE);
    gateOpen = true;
    gateOpenTime = millis();
    
    // Xanh dương = Gate Open
    setRGB_Blue();
    
    Serial.println("[GATE] SUCCESS Gate opened");
    Serial.print(GATE_OPEN_DURATION / 1000);
 
    publishGateStatus("open");
  } else {
    Serial.println("[GAT] already open, resetting timer");
    gateOpenTime = millis();  // Reset timer
  }
}

void closeGate() {
  if (gateOpen) {
    Serial.println("\n>>> CLOSING GATE <<<");
    
    gateServo.write(SERVO_CLOSED_ANGLE);
    gateOpen = false;
    
    // Xanh lá = Ready
    setRGB_Green();
    
    Serial.println("[GATE] SUCCESS Gate closed"); 
    publishGateStatus("closed");
  }
}

void publishGateStatus(String status) {
  String message = "{\"status\":\"" + status + "\",\"direction\":\"" + currentDirection + "\"}";
  mqtt.publish(TOPIC_GATE_STATUS, message.c_str());
  
  Serial.print("[STATUS] ");
  Serial.println(status);
}

// Helper Functions
void blinkLED(int pin, int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(100);
    digitalWrite(pin, LOW);
    delay(100);
  }
}
