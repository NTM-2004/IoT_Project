#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include <ArduinoJson.h>
#include "env.h"

// WiFi 
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// Server 
const char* serverIP = "192.168.137.1";
const int serverPort = 8000;
const char* uploadEndpoint = "/api/upload-image";

// MQTT Topics - Nhận trigger out
#define TOPIC_TRIGGER_OUT  "iot/parking/trigger/out"
// Publish metadata 
#define TOPIC_CAM_STATUS   "iot/parking/cam/status"

// FLASH PINS
#define FLASH_LED 4

// Camera Pins (AI-Thinker)
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

// Global Objects
WiFiClient espClient;
PubSubClient mqtt(espClient);

String currentDirection = "";

// Setup
void setup() {
  Serial.begin(115200);
  delay(500);  
  
  Serial.println("\n\n========================================");
  Serial.println("ESP32-CAM-OUT");
  Serial.println("========================================");
  
  // Khởi tạo Flash LED
  pinMode(FLASH_LED, OUTPUT);
  digitalWrite(FLASH_LED, LOW);
  
  // Khởi tạo Camera
  Serial.println("[CAMERA] Initializing camera");
  if (!initCamera()) {
    Serial.println("[CAMERA] ERROR Camera init failed!");
    delay(3000);
    ESP.restart();
  }
  Serial.println("[CAMERA] SUCCESS Camera initialized");
  Serial.println("Free heap: " + String(ESP.getFreeHeap()) + " bytes");
  Serial.println("PSRAM free: " + String(ESP.getFreePsram()) + " bytes");
  
  // Feed watchdog
  yield();
  delay(100);
  
  // Kết nối WiFi 
  connectWiFi();
  
  Serial.println("[WIFI] SUCCESS WiFi connected");
  yield();
  delay(100);
  
  // Cấu hình MQTT
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(512); 
  
  Serial.println("[MQTT] MQTT configured, connecting");
  yield();
  delay(100);
  
  // Kết nối MQTT 
  connectMQTT();
  
  Serial.println("========================================");
  Serial.println("System Ready");
  Serial.println("========================================\n");
}

void loop() {
  // Kiểm tra kết nối WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[WIFI] ERROR WiFi disconnected! Reconnecting");
    connectWiFi();
  }
  
  // Duy trì kết nối MQTT
  if (!mqtt.connected()) {
    connectMQTT();
  }
  mqtt.loop();
  
  // Feed watchdog
  yield();
  delay(10);
}

// WiFi Functions
void connectWiFi() {
  Serial.print("[WIFI] Connecting to WiFi: ");
  Serial.println(ssid);
  
  // Disconnect first
  WiFi.disconnect(true);
  delay(1000);
  
  // Set mode
  WiFi.mode(WIFI_STA);
  
  // Begin connection
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  const int maxAttempts = 60;  // 30 giây timeout
  
  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(500);
    Serial.print(".");
    attempts++;
    
    // Feed watchdog timer 
    yield();
    
    // Debug every 10 attempts
    if (attempts % 10 == 0) {
      Serial.print("\n[WiFi Status: ");
      Serial.print(WiFi.status());
      Serial.print("] ");
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] WiFi Connected");
    Serial.print("[WIFI] IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("[WIFI] Signal: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("\n[WIFI] ERROR WiFi Connection Failed");
    Serial.println("[SYSTEM] Timeout after 30 seconds - Restarting...");
    delay(2000);
    ESP.restart();
  }
}

// MQTT Functions
void connectMQTT() {
  int retries = 0;
  const int maxRetries = 5;  // Giới hạn retry 
  
  while (!mqtt.connected() && retries < maxRetries) {
    Serial.print("[MQTT] Connecting to MQTT...");
    
    String mac = WiFi.macAddress();
    mac.replace(":", ""); 

    String clientId = "ESP32_CAM_" + mac;
    
    if (mqtt.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD)) {
      Serial.println("[MQTT] SUCCESS Connected");
      
      // Subscribe CHỈ vào trigger OUT/EXIT
      mqtt.subscribe(TOPIC_TRIGGER_OUT);
      
      Serial.println("[MQTT] Subscribed to:");
      Serial.print("  - ");
      Serial.println(TOPIC_TRIGGER_OUT);
      Serial.println("  (EXIT/OUT camera only)");
      
      return;  
      
    } else {
      Serial.print("[MQTT] ERROR Failed, rc=");
      Serial.print(mqtt.state());
      Serial.print(" Retry ");
      Serial.print(retries + 1);
      Serial.print("/");
      Serial.println(maxRetries);
      
      retries++;
      delay(5000);
      yield();  
    }
  }
  
  if (!mqtt.connected()) {
    Serial.println("\n[MQTT] ERROT MQTT connection failed, continue without MQTT");
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.println("\n=== MQTT TRIGGER RECEIVED ===");
  Serial.print("Topic: ");
  Serial.println(topic);
  Serial.print("Message: ");
  Serial.println(message);
  
  if (String(topic) == TOPIC_TRIGGER_OUT) {
    currentDirection = "out";
    Serial.println("Direction: ENTRANCE (CAM_OUT)");
    
    // Chụp và upload ảnh
    captureAndUpload();
  } else {
    Serial.println("[MQTT] ERROR Ignoring non-EXIT trigger");
  }
}

// Camera Functions
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;
  
  // Cấu hình độ phân giải 
  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;   // 1600x1200 - Độ phân giải cao nhất
    config.jpeg_quality = 10;             // 0-63
    config.fb_count = 2;
    Serial.println("[CAMERA] UXGA mode with PSRAM");
  } else {
    config.frame_size = FRAMESIZE_SVGA;   // 800x600
    config.jpeg_quality = 8;
    config.fb_count = 1;
    Serial.println("[CAMERA] SVGA mode, no PSRAM");
  }
  
  // Khởi tạo camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAMERA] ERROR Camera init failed: 0x%x\n", err);
    return false;
  }
  
  // Tối ưu hóa sensor 
  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL) {
    s->set_brightness(s, -1);      // -2 to 2 (-1 = giảm độ sáng)
    s->set_contrast(s, 1);         // -2 to 2 (tăng contrast cho rõ nét)
    s->set_saturation(s, 0);       // -2 to 2 (giữ màu tự nhiên)
    s->set_sharpness(s, 2);        // -2 to 2 (MAX sharpness cho OCR)
    s->set_denoise(s, 0);          // 0 to 8 (TẮT denoise để giữ chi tiết)
    s->set_special_effect(s, 0);   // 0 = no effect
    s->set_wb_mode(s, 1);          // 0=auto, 1=sunny (cho ánh sáng nhân tạo)
    s->set_ae_level(s, -1);        // -2 to 2 (-1 = giảm auto exposure)
    s->set_aec_value(s, 200);      // 0 to 1200 (giảm exposure từ 300 xuống 200)
    s->set_gain_ctrl(s, 1);        // 1 = auto gain
    s->set_agc_gain(s, 0);         // 0 to 30 (auto)
    s->set_gainceiling(s, (gainceiling_t)2);  // 0 to 6 (giảm gain ceiling từ 3 xuống 2)
    s->set_bpc(s, 1);              // Black pixel correction ON
    s->set_wpc(s, 1);              // White pixel correction ON
    s->set_raw_gma(s, 1);          // Gamma correction ON
    s->set_lenc(s, 1);             // Lens correction ON
    s->set_hmirror(s, 1);          // Horizontal mirror
    s->set_vflip(s, 1);            // Vertical flip
    s->set_dcw(s, 0);              // Downsize EN (TẮT để giữ full resolution)
    s->set_colorbar(s, 0);         // Color bar OFF
    
    Serial.println("[CAMERA] Sensor configed");
  }
  
  return true;
}

void captureAndUpload() {
  Serial.println("\n=== CAPTURE & UPLOAD ===");
  
  // Clear old frames
  Serial.println("[CAMERA] Clearing camera buffer");
  camera_fb_t *fb_clear = esp_camera_fb_get();
  if (fb_clear) {
    esp_camera_fb_return(fb_clear);
  }
  delay(200); 
  
  // Bật flash
  digitalWrite(FLASH_LED, HIGH);
  delay(150);  
  
  // Chụp ảnh
  Serial.println("[CAMERA] Capturing image");
  camera_fb_t *fb = esp_camera_fb_get();
  
  // Tắt flash
  digitalWrite(FLASH_LED, LOW);
  
  if (!fb) {
    Serial.println("[CAMERA] ERROR Camera capture failed");
    notifyCamStatus("capture_failed");
    return;
  }
  
  // Verify JPEG header
  if (fb->len < 1000 || fb->buf[0] != 0xFF || fb->buf[1] != 0xD8) {
    Serial.println("[CAMERA] ERROR Invalid JPEG data!");
    esp_camera_fb_return(fb);
    notifyCamStatus("invalid_image");
    return;
  }
  
  Serial.printf("[CAMERA] SUCCESS Image captured: %d bytes\n", fb->len);
  //Serial.printf("  Resolution: %dx%d\n", fb->width, fb->height);
  
  // Upload qua HTTP 
  bool upload_success = uploadToServer(fb);
  
  // Clear buffer
  esp_camera_fb_return(fb);
  
  // Delay sau khi return buffer để tránh EV-EOF-OVF
  delay(500);
  
  if (upload_success) {
    Serial.println("[SYSTEM] SUCCESS Image captured and uploaded");
  } else {
    Serial.println("[SYSTEM] ERROR Upload failed");
  }
}

bool uploadToServer(camera_fb_t *fb) {
  Serial.println("\n=== Uploading ===");
  
  WiFiClient client;
  
  if (!client.connect(serverIP, serverPort)) {
    Serial.println("[WIFI_CLIENT] ERROR Connection failed");
    notifyCamStatus("upload_failed");
    return false;
  }
  
  Serial.println("[WIFI_CLIENT] Connected to server");
  
  // Tạo boundary cho multipart
  String boundary = "----ESP32CAM" + String(millis());
  
  // Tạo header
  String head = "--" + boundary + "\r\n";
  head += "Content-Disposition: form-data; name=\"file\"; filename=\"capture.jpg\"\r\n";
  head += "Content-Type: image/jpeg\r\n\r\n";
  
  String tail = "\r\n--" + boundary + "--\r\n";
  
  uint32_t totalLen = head.length() + fb->len + tail.length();
  
  // Gửi HTTP request
  client.println("POST " + String(uploadEndpoint) + " HTTP/1.1");
  client.println("Host: " + String(serverIP));
  client.println("Content-Type: multipart/form-data; boundary=" + boundary);
  client.println("Content-Length: " + String(totalLen));
  client.println("Connection: close");
  client.println();
  
  // Gửi header
  client.print(head);
  
  // Gửi ảnh
  uint8_t *buf = fb->buf;
  size_t size = fb->len;
  size_t sent = 0;
  
  Serial.printf("[WIFI_CLIENT] Uploading %d bytes", size);
  unsigned long upload_start = millis();
  
  while (sent < size) {
    size_t chunk = (size - sent < 1024) ? (size - sent) : 1024;
    size_t written = client.write(buf + sent, chunk);
    
    if (written != chunk) {
      Serial.printf("\n[WIFI_CLIENT] Upload error: Expected %d, wrote %d\n", chunk, written);
      client.stop();
      return false;
    }
    
    sent += chunk;
    
    if (sent % 10240 == 0) {
      Serial.print(".");
    }
    
    yield();  // Feed watchdog
  }
  
  unsigned long upload_time = millis() - upload_start;
  Serial.printf("[WIFI_CLIENT] Done! (%lu ms)\n", upload_time);
  
  // Gửi tail
  client.print(tail);
  
  Serial.printf("[WIFI_CLIENT]  Uploaded %d bytes in %lu ms (%.2f KB/s)\n", 
    sent, upload_time, (sent / 1024.0) / (upload_time / 1000.0));
  
  // Đọc response
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 10000) {
      Serial.println("[WIFI_CLIENT] Response timeout");
      client.stop();
      notifyCamStatus("timeout");
      return false;
    }
  }
  
  // Log response
  Serial.println("\n=== Server Response ===");
  while (client.available()) {
    String line = client.readStringUntil('\n');
    if (line.startsWith("{")) {
      Serial.println(line);
    }
  }
  
  client.stop();
  
  // Thông báo upload thành công
  notifyCamStatus("uploaded");
  
  Serial.println("[WIFI_CLIENT] SUCCESS Upload complete, Server received");
  
  return true;
}

void notifyCamStatus(String status) {
  // Gửi status về server qua MQTT (optional, cho logging)
  String message = "{\"status\":\"" + status + "\",\"direction\":\"" + currentDirection + "\"}";
  
  if (mqtt.publish(TOPIC_CAM_STATUS, message.c_str())) {
    Serial.print("[CAM STATUS] ");
    Serial.println(status);
  }
}
