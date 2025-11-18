/*
 * IoT Parking Gate Controller
 * 
 * Hardware: ESP32-C3 Zero (GPIO 0-21 only)
 * 
 * Chức năng:
 * 1. Phát hiện xe vào/ra bằng 2 cảm biến IR
 * 2. Gửi tín hiệu trigger chụp ảnh đến ESP32-CAM qua MQTT
 * 3. Nhận lệnh mở cổng từ server (sau khi OCR xác nhận biển số)
 * 4. OCR Timeout: 10 giây - tự động reset về trạng thái sẵn sàng
 * 5. Tự động đóng cổng sau khi xe đi qua
 * 
 * Flow:
 * 1. IR detect → Trigger CAM → waitingForOCR = true (block IR sensors)
 * 2. Server OCR → MQTT command → Open/Reject gate
 * 3. Timeout 10s → Reset về ready state
 * 
 * Pinout ESP32-C3 Zero:
 * - GPIO 10: IR Sensor IN (entrance)
 * - GPIO 9:  IR Sensor OUT (exit)
 * - GPIO 4:  Servo Motor (PWM)
 * - GPIO 6:  LED Green (status ready)
 * - GPIO 7:  LED Red (processing)
 * 
 * Available GPIOs: 0-21 (avoid 18,19 for USB)
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>  // Cần cài ESP32Servo library từ Library Manager
#include <Adafruit_NeoPixel.h>  // Cần cài Adafruit NeoPixel library
#include "env.h"

// ==================== PIN Configuration (ESP32-C3 Zero) ====================
// Note: ESP32-C3 chỉ có GPIO 0-21
// IR Sensors
#define IR_SENSOR_IN   8    // Cảm biến vào (entrance)
#define IR_SENSOR_OUT  9     // Cảm biến ra (exit)

// Servo Motor
#define SERVO_PIN      4     // Servo điều khiển cổng

// Status LEDs (thay đổi cho ESP32-C3)
#define LED_GREEN      6     // LED xanh - Sẵn sàng (GPIO 6)
#define LED_RED        7     // LED đỏ - Đang xử lý (GPIO 7)

// WS2812 RGB LED (Built-in on ESP32-C3 Zero board)
#define WS2812_PIN     10     // WS2812 RGB LED (thường GPIO 8 hoặc 2 trên C3 Zero)
#define WS2812_COUNT   1     // Số lượng LED (board có 1 led built-in)

// ==================== MQTT Topics ====================
#define TOPIC_TRIGGER_CAM_IN   "iot/parking/trigger/in"    // Gửi trigger chụp cổng vào
#define TOPIC_TRIGGER_CAM_OUT  "iot/parking/trigger/out"   // Gửi trigger chụp cổng ra
#define TOPIC_GATE_CONTROL     "iot/parking/gate/control"  // Nhận lệnh mở cổng
#define TOPIC_GATE_STATUS      "iot/parking/gate/status"   // Gửi trạng thái cổng

// MQTT Server Configuration
// 
// Khuyến nghị: Dùng local Mosquitto broker (cùng mạng với GATE)
// - Server Python đã chạy Mosquitto tại localhost:1883
// - GATE cần kết nối tới IP của máy chạy server
// 
// Cách tìm IP máy chạy server:
// - Windows: ipconfig | findstr IPv4
// - Linux/Mac: ifconfig | grep inet
//
#define MQTT_SERVER "192.168.137.1"  // ⚠️ THAY ĐỔI thành IP máy chạy server
#define MQTT_PORT 1883

// Note: HiveMQ Cloud (từ env.h) cần TLS/SSL, phức tạp hơn
// Nếu muốn dùng cloud: cần thêm WiFiClientSecure + certificates

// ==================== Servo Settings ====================
#define SERVO_CLOSED_ANGLE  0     // Góc đóng cổng
#define SERVO_OPEN_ANGLE    90    // Góc mở cổng
#define GATE_OPEN_DURATION  5000  // Thời gian giữ cổng mở (ms)

// ==================== Objects ====================
WiFiClient espClient;
PubSubClient mqtt(espClient);
Servo gateServo;
Adafruit_NeoPixel rgb_led(WS2812_COUNT, WS2812_PIN, NEO_GRB + NEO_KHZ800);

// ==================== State Variables ====================
bool gateOpen = false;
unsigned long gateOpenTime = 0;
bool waitingForOCR = false;
unsigned long ocrWaitStartTime = 0;
const unsigned long OCR_TIMEOUT = 10000;  // 10 giây timeout cho OCR
String currentDirection = "";  // "in" hoặc "out"

// Debounce
unsigned long lastIRInTime = 0;
unsigned long lastIROutTime = 0;
const unsigned long debounceDelay = 1000;  // 1 giây

// ==================== RGB LED Functions ====================
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

// ==================== Setup ====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=================================");
  Serial.println("IoT Parking Gate Controller");
  Serial.println("=================================");
  
  // Khởi tạo WS2812 RGB LED
  rgb_led.begin();
  rgb_led.setBrightness(50);  // 50/255 = 20% brightness (không quá chói)
  setRGB_Red();  // Đỏ khi khởi động
  Serial.println("✓ WS2812 RGB LED initialized");
  
  // Cấu hình pins
  pinMode(IR_SENSOR_IN, INPUT);
  pinMode(IR_SENSOR_OUT, INPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  
  // Khởi tạo LEDs - đỏ khi khởi động
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, LOW);
  
  // Khởi tạo Servo
  gateServo.attach(SERVO_PIN);
  closeGate();
  Serial.println("✓ Servo initialized - Gate CLOSED");
  
  // Kết nối WiFi
  connectWiFi();
  
  // Cấu hình MQTT
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  
  // Kết nối MQTT
  connectMQTT();
  
  // Sẵn sàng - LED xanh + RGB LED xanh
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, HIGH);
  
  // WS2812 RGB LED: Xanh lá = System Ready
  setRGB_Blink(0, 255, 0, 5);  // Blink 5 lần
  setRGB_Green();  // Sáng xanh lá cố định
  
  Serial.println("=================================");
  Serial.println("System Ready!");
  Serial.println("RGB LED: GREEN (System Ready)");
  Serial.println("=================================\n");
}

// ==================== Main Loop ====================
void loop() {
  // Duy trì kết nối MQTT
  if (!mqtt.connected()) {
    connectMQTT();
  }
  mqtt.loop();
  
  // Kiểm tra cảm biến IR vào (entrance) - CHỈ khi KHÔNG đang chờ OCR
  if (digitalRead(IR_SENSOR_IN) == LOW && !waitingForOCR) {
    unsigned long currentTime = millis();
    if (currentTime - lastIRInTime > debounceDelay) {
      lastIRInTime = currentTime;
      handleVehicleDetected("in");
    }
  }
  
  // Kiểm tra cảm biến IR ra (exit) - CHỈ khi KHÔNG đang chờ OCR
  if (digitalRead(IR_SENSOR_OUT) == LOW && !waitingForOCR) {
    unsigned long currentTime = millis();
    if (currentTime - lastIROutTime > debounceDelay) {
      lastIROutTime = currentTime;
      handleVehicleDetected("out");
    }
  }
  
  // Kiểm tra timeout OCR (10 giây)
  if (waitingForOCR && (millis() - ocrWaitStartTime > OCR_TIMEOUT)) {
    Serial.println("\n⚠️  OCR TIMEOUT (10s) - Returning to ready state");
    waitingForOCR = false;
    
    // Reset LED về trạng thái sẵn sàng
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_GREEN, HIGH);
    setRGB_Blink(255, 0, 0, 3);  // Red blink = Timeout
    setRGB_Green();
    
    Serial.println("  RGB LED: GREEN (Ready)");
    publishGateStatus("timeout");
  }
  
  // Tự động đóng cổng sau thời gian
  if (gateOpen && (millis() - gateOpenTime > GATE_OPEN_DURATION)) {
    closeGate();
  }
  
  // Small delay để FreeRTOS scheduler hoạt động tốt hơn
  delay(10);
}

// ==================== WiFi Functions ====================
void connectWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  Serial.print("Password: ");
  Serial.println(WIFI_PASSWORD);
  
  // Disconnect trước khi reconnect (quan trọng cho ESP32-C3)
  WiFi.disconnect(true);
  delay(1000);
  
  // Set WiFi mode
  WiFi.mode(WIFI_STA);
  
  // Bắt đầu kết nối
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
    Serial.println("\n✓ WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal Strength (RSSI): ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("\n✗ WiFi Connection Failed!");
    Serial.print("Final Status Code: ");
    Serial.println(WiFi.status());
    Serial.println("Possible reasons:");
    Serial.println("  - Wrong SSID/Password");
    Serial.println("  - Router using 5GHz only (ESP32-C3 needs 2.4GHz)");
    Serial.println("  - Signal too weak");
    Serial.println("  - MAC filtering on router");
  }
}

// ==================== MQTT Functions ====================
void connectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Connecting to MQTT...");
    
    String clientId = "ESP32_GATE_" + String(random(0xffff), HEX);
    
    if (mqtt.connect(clientId.c_str())) {
      Serial.println(" ✓ Connected!");
      
      // Subscribe vào topic điều khiển cổng
      mqtt.subscribe(TOPIC_GATE_CONTROL);
      Serial.print("✓ Subscribed to: ");
      Serial.println(TOPIC_GATE_CONTROL);
      
      // Gửi trạng thái ban đầu
      publishGateStatus("ready");
      
    } else {
      Serial.print(" ✗ Failed, rc=");
      Serial.print(mqtt.state());
      Serial.println(" Retrying in 5s...");
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

// ==================== Gate Control Functions ====================
void handleVehicleDetected(String direction) {
  Serial.println("\n>>> VEHICLE DETECTED <<<");
  Serial.print("Direction: ");
  Serial.println(direction == "in" ? "ENTRANCE" : "EXIT");
  
  // Bật LED đỏ - đang xử lý
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_RED, HIGH);
  
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
    Serial.println("✓ Trigger sent to CAM");
    Serial.print("  Topic: ");
    Serial.println(topic);
    Serial.println("  Waiting for OCR result...");
    Serial.println("  RGB LED: YELLOW (Waiting OCR)");
    
    // Gửi trạng thái
    publishGateStatus("waiting_ocr");
  } else {
    Serial.println("✗ Failed to send trigger");
    waitingForOCR = false;
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_GREEN, HIGH);
    setRGB_Green();  // Về xanh nếu fail
  }
}

void handleGateCommand(String message) {
  Serial.println("\n>>> GATE COMMAND RECEIVED <<<");
  Serial.println(message);
  Serial.print("  waitingForOCR state: ");
  Serial.println(waitingForOCR ? "TRUE" : "FALSE");
  
  // Parse JSON message - hỗ trợ cả có và không có space
  // Expected format: {"action":"open",...} or {"action": "open",...}
  
  if (message.indexOf("\"open\"") > 0) {
    // Lấy biển số từ message (optional)
    int plateStart = message.indexOf("\"plate\"");
    String plate = "";
    if (plateStart > 0) {
      plateStart = message.indexOf("\"", plateStart + 7) + 1;
      int plateEnd = message.indexOf("\"", plateStart);
      if (plateEnd > plateStart) {
        plate = message.substring(plateStart, plateEnd);
      }
    }
    
    Serial.println("✓ OCR Verified - Opening gate...");
    if (plate.length() > 0) {
      Serial.print("  License Plate: ");
      Serial.println(plate);
    }
    
    openGate();
    waitingForOCR = false;
    Serial.println("  waitingForOCR reset to FALSE");
    
  } else if (message.indexOf("\"reject\"") > 0) {
    Serial.println("✗ OCR Failed - Gate remains closed");
    waitingForOCR = false;
    Serial.println("  waitingForOCR reset to FALSE");
    
    // Bật LED xanh lại
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_GREEN, HIGH);
    
    // RGB LED: Đỏ blink 3 lần → Xanh
    setRGB_Blink(255, 0, 0, 3);  // Red blink = Rejected
    setRGB_Green();
    
    Serial.println("  RGB LED: GREEN (Back to Ready)");
    
    publishGateStatus("rejected");
  } else {
    Serial.println("⚠️  Unknown command format!");
    Serial.println("  Expected: {\"action\":\"open\",...} or {\"action\":\"reject\",...}");
    Serial.println("  Received: " + message);
  }
}

void handleManualOpen() {
  Serial.println("\n╔════════════════════════════════════╗");
  Serial.println("║  MANUAL OVERRIDE - FAIL-SAFE MODE  ║");
  Serial.println("╚════════════════════════════════════╝");
  Serial.println("Button pressed - Opening gate immediately");
  Serial.println("This action BYPASSES all OCR checks");
  
  // RGB LED: Tím = Manual Override
  setRGB_Purple();
  Serial.println("  RGB LED: PURPLE (Manual Override)");
  
  // Reset waiting state (nếu đang chờ OCR)
  if (waitingForOCR) {
    Serial.println("⚠️  Cancelling pending OCR operation");
    waitingForOCR = false;
  }
  
  // Mở cổng ngay lập tức
  openGate();
  
  // Gửi log về server
  String message = "{\"source\":\"manual\",\"direction\":\"manual\",\"override\":true}";
  if (mqtt.connected()) {
    mqtt.publish(TOPIC_GATE_STATUS, message.c_str());
    Serial.println("✓ Manual override logged to server");
  }
}

void openGate() {
  if (!gateOpen) {
    Serial.println("\n=== OPENING GATE ===");
    
    gateServo.write(SERVO_OPEN_ANGLE);
    gateOpen = true;
    gateOpenTime = millis();
    
    // LED xanh nhấp nháy - cổng đang mở
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_GREEN, HIGH);
      delay(100);
      digitalWrite(LED_GREEN, LOW);
      delay(100);
    }
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_RED, LOW);
    
    // RGB LED: Xanh dương = Gate Open
    setRGB_Blue();
    
    Serial.println("✓ Gate OPENED");
    Serial.print("  Auto-close in: ");
    Serial.print(GATE_OPEN_DURATION / 1000);
    Serial.println(" seconds");
    Serial.println("  RGB LED: BLUE (Gate Open)");
    
    publishGateStatus("open");
  } else {
    Serial.println("Gate already open - resetting timer");
    gateOpenTime = millis();  // Reset timer
  }
}

void closeGate() {
  if (gateOpen) {
    Serial.println("\n=== CLOSING GATE ===");
    
    gateServo.write(SERVO_CLOSED_ANGLE);
    gateOpen = false;
    
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_GREEN, HIGH);
    
    // RGB LED: Xanh lá = Ready
    setRGB_Green();
    
    Serial.println("✓ Gate CLOSED");
    Serial.println("  RGB LED: GREEN (Ready)");
    
    publishGateStatus("closed");
  }
}

void publishGateStatus(String status) {
  String message = "{\"status\":\"" + status + "\",\"direction\":\"" + currentDirection + "\"}";
  mqtt.publish(TOPIC_GATE_STATUS, message.c_str());
  
  Serial.print("[STATUS] ");
  Serial.println(status);
}

// ==================== Helper Functions ====================
void blinkLED(int pin, int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(100);
    digitalWrite(pin, LOW);
    delay(100);
  }
}
