#include <WiFi.h>
#include <esp_now.h>

// Sensor
const int sensor1Pin = 34;
const int sensor2Pin = 35;

// Previous state
bool lastState1 = false;
bool lastState2 = false;

// ID slot
const char* slot1ID = "A1";
const char* slot2ID = "A2";

// Monitor's MAC
uint8_t gatewayMAC[] = {0xEC, 0xE3, 0x34, 0x7B, 0x8D, 0x78}; 

// Flags for retrying send
volatile bool sendSuccess = false;  // Tracks the success of the send operation
String currentMessage = "";         // Holds the message that is being sent

// Callback when sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Trạng thái gửi: ");
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("Thành công");
    sendSuccess = true;  // Set flag to true on success
  } else {
    Serial.println("Thất bại");
    sendSuccess = false; // Set flag to false on failure
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(sensor1Pin, INPUT);
  pinMode(sensor2Pin, INPUT);

  WiFi.mode(WIFI_STA);
  Serial.print("MAC thiết bị: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("Lỗi khi khởi động ESP-NOW");
    return;
  }

  esp_now_register_send_cb(OnDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, gatewayMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Lỗi khi thêm peer");
    return;
  }

  Serial.println("Khởi tạo ESP-NOW xong");
}

void loop() {
  bool state1 = digitalRead(sensor1Pin) == LOW;
  bool state2 = digitalRead(sensor2Pin) == LOW;

  if (state1 != lastState1) {
    lastState1 = state1;
    sendStatus(slot1ID, state1);
  }

  if (state2 != lastState2) {
    lastState2 = state2;
    sendStatus(slot2ID, state2);
  }

  delay(300);
}

void sendStatus(const char* slotID, bool isFull) {
  // Prepare the message to send
  char msg[32];
  snprintf(msg, sizeof(msg), "%s,%s", slotID, isFull ? "full" : "empty");

  // Store the current message for tracking
  currentMessage = String(msg);

  Serial.print("Gửi: ");
  Serial.println(msg);

  // Send the message and wait until it's confirmed successful
  sendSuccess = false;  // Reset the send flag before sending
  esp_now_send(gatewayMAC, (uint8_t*)msg, strlen(msg) + 1);  

  // Retry the message until it succeeds
  while (!sendSuccess) {
    Serial.println("Đang thử lại...");
    delay(500);  // Wait before retrying
    esp_now_send(gatewayMAC, (uint8_t*)msg, strlen(msg) + 1);  // Retry sending
  }

  Serial.println("Gửi thành công!");
}
