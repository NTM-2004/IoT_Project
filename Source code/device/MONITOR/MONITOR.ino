#include <TFT_eSPI.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "env.h"

// CẤU HÌNH WIFI
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// CẤU HÌNH MQTT 
const char* mqtt_server = "192.168.137.1";   
const int mqtt_port = 1883;                   
const char* mqtt_topic = "iot/parking/slots"; 

// TFT DISPLAY
TFT_eSPI tft = TFT_eSPI();

// MQTT CLIENT
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// PARKING SLOTS
#define SLOT_COUNT 8
#define SLOT_WIDTH 75
#define SLOT_HEIGHT 45
#define PADDING 8
#define START_Y 40  

struct Slot {
  int x, y;
  String id;
  bool isOccupied;
  unsigned long lastUpdate;
};

Slot slots[SLOT_COUNT];

// COLORS
#define COLOR_BG       0x0000      // Black
#define COLOR_HEADER   0x001F      // Blue
#define COLOR_EMPTY    0x07E0      // Green
#define COLOR_OCCUPIED 0xF800      // Red
#define COLOR_TEXT     0xFFFF      // White
#define COLOR_BORDER   0x7BEF      // Light Gray

// STATUS
bool wifiConnected = false;
bool mqttConnected = false;
unsigned long lastReconnectAttempt = 0;
const unsigned long reconnectInterval = 5000;

void setup() {
  Serial.begin(115200);
  Serial.println("\n=================================");
  Serial.println("MONITOR - LilyGO T-Display S3");
  Serial.println("=================================");

  // Khởi tạo màn hình
  tft.init();
  tft.setRotation(1);  // Landscape mode (320x170)
  tft.fillScreen(COLOR_BG);
  
  // Hiển thị splash screen
  drawHeader("Parking Monitor", "Initializing...");
  
  // Khởi tạo slots
  initSlots();
  
  // Kết nối WiFi
  connectWiFi();
  
  // Cấu hình MQTT
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);
  
  // Kết nối MQTT
  connectMQTT();
  
  // Vẽ giao diện
  drawUI();
  
  Serial.println("System ready!");
}

void loop() {
  // Duy trì kết nối MQTT
  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > reconnectInterval) {
      lastReconnectAttempt = now;
      if (connectMQTT()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    mqttClient.loop();
  }
  
  // Kiểm tra WiFi
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    connectWiFi();
  }
}

void initSlots() {
  // Hàng 1: A1-A4
  for (int i = 0; i < 4; i++) {
    slots[i].x = PADDING + i * (SLOT_WIDTH + PADDING);
    slots[i].y = START_Y;
    slots[i].id = "A" + String(i + 1);
    slots[i].isOccupied = false;
    slots[i].lastUpdate = 0;
  }
  
  // Hàng 2: B1-B4
  for (int i = 4; i < 8; i++) {
    slots[i].x = PADDING + (i - 4) * (SLOT_WIDTH + PADDING);
    slots[i].y = START_Y + SLOT_HEIGHT + PADDING;
    slots[i].id = "B" + String(i - 3);
    slots[i].isOccupied = false;
    slots[i].lastUpdate = 0;
  }
  
  Serial.println("Slots initialized");
}

void connectWiFi() {
  Serial.print("Connecting to WiFi");
  drawHeader("Parking Monitor", "Connecting WiFi...");
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\n WiFi connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    wifiConnected = false;
    Serial.println("\n WiFi connection failed!");
  }
}

bool connectMQTT() {
  Serial.print("Connecting to MQTT broker...");
  drawHeader("Parking Monitor", "Connecting MQTT...");
  
  String clientId = "ESP32_Monitor_";
  clientId += String(random(0xffff), HEX);
  
  if (mqttClient.connect(clientId.c_str())) {
    mqttConnected = true;
    Serial.println(" Connected!");
    
    // Subscribe to topic
    if (mqttClient.subscribe(mqtt_topic, 1)) {
      Serial.print(" Subscribed to: ");
      Serial.println(mqtt_topic);
    } else {
      Serial.println(" Subscribe failed!");
    }
    
    drawHeader("Parking Monitor", "Connected");
    return true;
  } else {
    mqttConnected = false;
    Serial.print("  Failed, rc=");
    Serial.println(mqttClient.state());
    drawHeader("Parking Monitor", "MQTT Error");
    return false;
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Parse JSON message
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  
  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    return;
  }
  
  // Lấy thông tin slot
  const char* slotId = doc["slot"];
  bool occupied = doc["occupied"];
  
  if (slotId == nullptr) {
    Serial.println("Invalid message: missing 'slot' field");
    return;
  }
  
  Serial.print(" MQTT: ");
  Serial.print(slotId);
  Serial.print(" -> ");
  Serial.println(occupied ? "OCCUPIED" : "EMPTY");
  
  // Cập nhật slot
  updateSlot(String(slotId), occupied);
}

void updateSlot(String slotId, bool occupied) {
  for (int i = 0; i < SLOT_COUNT; i++) {
    if (slots[i].id == slotId) {
      if (slots[i].isOccupied != occupied) {
        slots[i].isOccupied = occupied;
        slots[i].lastUpdate = millis();
        drawSlot(i);
        Serial.print("✓ Updated slot ");
        Serial.print(slotId);
        Serial.print(": ");
        Serial.println(occupied ? "OCCUPIED" : "EMPTY");
      }
      return;
    }
  }
  Serial.print(" Slot not found: ");
  Serial.println(slotId);
}

void drawUI() {
  tft.fillScreen(COLOR_BG);
  
  // Draw header với status
  String status = mqttConnected ? "Connected" : "Disconnected";
  drawHeader("Parking Monitor", status);
  
  // Draw all slots
  for (int i = 0; i < SLOT_COUNT; i++) {
    drawSlot(i);
  }
}

void drawHeader(String title, String status) {
  // Header background
  tft.fillRect(0, 0, 320, 30, COLOR_HEADER);
  
  // Title
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(2);
  tft.setCursor(10, 8);
  tft.print(title);
  
  // Status (right side)
  tft.setTextSize(1);
  // TFT_eSPI: tính width = length * 6 * textSize (cho font GLCD)
  int statusWidth = status.length() * 6 * 1;
  tft.setCursor(320 - statusWidth - 10, 10);
  tft.print(status);
  
  // Connection indicators
  // WiFi icon
  if (wifiConnected) {
    tft.fillCircle(300, 20, 3, TFT_GREEN);
  } else {
    tft.fillCircle(300, 20, 3, TFT_RED);
  }
  
  // MQTT icon
  if (mqttConnected) {
    tft.fillCircle(310, 20, 3, TFT_GREEN);
  } else {
    tft.fillCircle(310, 20, 3, TFT_RED);
  }
}

void drawSlot(int index) {
  if (index < 0 || index >= SLOT_COUNT) return;
  
  Slot s = slots[index];
  
  // Background color
  uint16_t bgColor = s.isOccupied ? COLOR_OCCUPIED : COLOR_EMPTY;
  
  // Draw border
  tft.drawRect(s.x, s.y, SLOT_WIDTH, SLOT_HEIGHT, COLOR_BORDER);
  
  // Draw filled rectangle
  tft.fillRect(s.x + 2, s.y + 2, SLOT_WIDTH - 4, SLOT_HEIGHT - 4, bgColor);
  
  // Draw slot ID
  tft.setTextColor(COLOR_TEXT);
  tft.setTextSize(2);
  
  // TFT_eSPI: tính width = length * 6 * textSize
  int textWidth = s.id.length() * 6 * 2;
  int textHeight = 8 * 2;  // Chiều cao font * textSize
  
  int xText = s.x + (SLOT_WIDTH - textWidth) / 2;
  int yText = s.y + (SLOT_HEIGHT - textHeight) / 2 - 8;
  
  tft.setCursor(xText, yText);
  tft.print(s.id);
  
  // Draw status text
  tft.setTextSize(1);
  String statusText = s.isOccupied ? "FULL" : "EMPTY";
  int statusWidth = statusText.length() * 6 * 1;
  int statusHeight = 8 * 1;
  
  xText = s.x + (SLOT_WIDTH - statusWidth) / 2;
  yText = s.y + (SLOT_HEIGHT - statusHeight) / 2 + 10;
  
  tft.setCursor(xText, yText);
  tft.print(statusText);
}
