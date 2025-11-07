#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include <ESP32Servo.h>
#include <ArduinoJson.h>
#include "env.h"

// CẤU HÌNH WIFI
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// CẤU HÌNH SERVER TỚI FASTAPI
const char* serverIP = "192.168.137.1";            // IP server Python
const int serverPort = 8000;                       // Port FastAPI
const char* uploadEndpoint = "/api/upload-image";  // API endpoint

// GPIO PINS
#define FLASH_LED 4   // Flash
#define IR_SENSOR 13  // IR
#define SERVO_PIN 14  // Servo

// CAMERA PINS
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

// GLOBAL OBJECTS
Servo gateServo;

void setup() {
  // Disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  Serial.println("ESP32-CAM HTTP Upload System");

  // Khởi tạo GPIO
  pinMode(FLASH_LED, OUTPUT);
  pinMode(IR_SENSOR, INPUT);
  digitalWrite(FLASH_LED, LOW);

  // Khởi tạo Servo
  gateServo.attach(SERVO_PIN);
  gateServo.write(0);  // Đóng cổng
  Serial.println("✓ Servo initialized (closed)");

  // Khởi tạo Camera
  if (initCamera()) {
    Serial.println("Camera initialized");
  } else {
    Serial.println("Camera init failed!");
    ESP.restart();
  }

  // Kết nối WiFi
  connectWiFi();

  Serial.println("✓ System ready! Waiting for IR trigger...");
}

void loop() {
  // Đọc cảm biến IR
  int irValue = digitalRead(IR_SENSOR);

  if (irValue == LOW) {
    Serial.println("\nIR Sensor triggered!");

    // Bật đèn flash
    digitalWrite(FLASH_LED, HIGH);
    delay(100);

    // Chụp và gửi ảnh
    if (captureAndUploadImage()) {
      Serial.println("Image uploaded successfully!");

      // Mở cổng
      openGate();
    } else {
      Serial.println("Upload failed!");
    }

    // Tắt đèn flash
    digitalWrite(FLASH_LED, LOW);

    // Debounce - đợi xe qua
    delay(8000);
  }

  delay(100);
}

void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n WiFi connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n WiFi connection failed!");
    ESP.restart();
  }
}

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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Cài đặt chất lượng ảnh
  if (psramFound()) {
    // Có PSRAM, dùng độ phân giải cao
    config.frame_size = FRAMESIZE_UXGA;     // 1600x1200
    config.jpeg_quality = 6;                
    config.fb_count = 2;                    // Double buffering
    config.grab_mode = CAMERA_GRAB_LATEST;  // Lấy frame mới nhất
  } else {
    // Không có PSRAM, dùng VGA
    config.frame_size = FRAMESIZE_VGA;  // 640x480
    config.jpeg_quality = 10;
    config.fb_count = 1;
  }

  config.fb_location = CAMERA_FB_IN_PSRAM;  // Lưu trong PSRAM nếu có

  // Khởi tạo camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }

  // Điều chỉnh sensor để TĂNG CHẤT LƯỢNG
  sensor_t* s = esp_camera_sensor_get();

  // Brightness & Contrast 
  s->set_brightness(s, -1);  // -2 to 2 
  s->set_contrast(s, 1);     // -2 to 2
  s->set_saturation(s, 0);   // -2 to 2

  // White Balance & Exposure
  s->set_whitebal(s, 1);  // Auto white balance ON
  s->set_awb_gain(s, 1);  // Auto white balance gain ON
  s->set_wb_mode(s, 0);   // 0 = auto, 1 = sunny, 2 = cloudy

  s->set_exposure_ctrl(s, 1);  // Auto exposure ON
  s->set_aec2(s, 0);           // Auto exposure algorithm OFF (để giảm loá)
  s->set_ae_level(s, -2);      // -2 to 2 (giảm exposure xuống -2)
  s->set_aec_value(s, 200);    // 0-1200 (giảm exposure value xuống 200)

  // Gain Control
  s->set_gain_ctrl(s, 0);                   // Auto gain OFF 
  s->set_agc_gain(s, 5);                    // 0-30 (giảm gain xuống 5)
  s->set_gainceiling(s, (gainceiling_t)3);  // 0-6, giới hạn gain tối đa ở mức 3

  // Sharpness & Denoise
  s->set_sharpness(s, 2);  // -2 to 2 
  s->set_denoise(s, 1);    // 0-8

  // Special Effects
  s->set_special_effect(s, 0);  // 0 = No effect
  s->set_bpc(s, 1);             // Black pixel correction
  s->set_wpc(s, 1);             // White pixel correction

  // Lens Correction
  s->set_lenc(s, 1);  // Lens correction ON
  s->set_dcw(s, 1);   // Downsize enable

  // Mirror & Flip
  s->set_hmirror(s, 1);  // 1 = ENABLE horizontal mirror (lật ngang)
  s->set_vflip(s, 1);    // 1 = ENABLE vertical flip (lật dọc)

  // Color settings
  s->set_colorbar(s, 0);  // 0 = disable testbar

  Serial.println("Camera sensor optimized for OCR (Anti-overexposure)");
  Serial.printf("Frame size: %s\n", psramFound() ? "UXGA (1600x1200)" : "VGA (640x480)");
  Serial.printf("JPEG quality: %d\n", psramFound() ? 6 : 10);
  Serial.printf("PSRAM: %s\n", psramFound() ? "Available" : "Not found");
  Serial.println("Brightness: -1 (darker)");
  Serial.println("Exposure: -2 (reduced overexposure)");
  Serial.println("Image orientation: Flipped & Mirrored");

  return true;
}

bool captureAndUploadImage() {
  camera_fb_t* fb = NULL;
  WiFiClient client;

  Serial.println("Capturing image...");

  // CHỤP NHIỀU LẦN, CHỌN ẢNH TỐT NHẤT
  // Bỏ qua 2 frame đầu (thường bị blur)
  for (int i = 0; i < 2; i++) {
    fb = esp_camera_fb_get();
    if (fb) {
      esp_camera_fb_return(fb);
      delay(100);
    }
  }

  // Chụp ảnh thật
  fb = esp_camera_fb_get();

  if (!fb) {
    Serial.println("Camera capture failed");
    return false;
  }

  Serial.printf("Image captured: %d bytes\n", fb->len);

  // Kết nối đến server
  Serial.printf("Connecting to %s:%d...\n", serverIP, serverPort);

  if (!client.connect(serverIP, serverPort)) {
    Serial.println("✗ Connection failed!");
    esp_camera_fb_return(fb);
    return false;
  }

  Serial.println("Connected to server");

  // Tạo boundary cho multipart/form-data
  String boundary = "----ESP32CAMBoundary";
  String startBoundary = "--" + boundary + "\r\n"
                                           "Content-Disposition: form-data; name=\"file\"; filename=\"capture.jpg\"\r\n"
                                           "Content-Type: image/jpeg\r\n\r\n";
  String endBoundary = "\r\n--" + boundary + "--\r\n";

  int contentLength = startBoundary.length() + fb->len + endBoundary.length();

  // Gửi HTTP POST header
  client.printf("POST %s HTTP/1.1\r\n", uploadEndpoint);
  client.printf("Host: %s:%d\r\n", serverIP, serverPort);
  client.println("Connection: close");
  client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary.c_str());
  client.printf("Content-Length: %d\r\n", contentLength);
  client.println();

  // Gửi start boundary
  client.print(startBoundary);

  // Gửi ảnh theo chunks
  Serial.println("Uploading image...");
  size_t index = 0;
  const size_t chunkSize = 1024;

  while (index < fb->len) {
    size_t remaining = fb->len - index;
    size_t toSend = (remaining > chunkSize) ? chunkSize : remaining;

    size_t sent = client.write(fb->buf + index, toSend);
    if (sent != toSend) {
      Serial.println("✗ Send failed!");
      esp_camera_fb_return(fb);
      client.stop();
      return false;
    }

    index += sent;

    // Progress indicator
    if (index % (10 * chunkSize) == 0 || index == fb->len) {
      Serial.printf("Progress: %d/%d bytes (%.1f%%)\n",
                    index, fb->len, (index * 100.0) / fb->len);
    }
  }

  // Gửi end boundary
  client.print(endBoundary);

  Serial.println("✓ Upload complete! Waiting for response...");

  // Giải phóng buffer
  esp_camera_fb_return(fb);

  // Đợi response từ server
  unsigned long timeout = millis();
  while (client.connected() && !client.available()) {
    if (millis() - timeout > 10000) {  // Timeout 10s
      Serial.println("✗ Response timeout!");
      client.stop();
      return false;
    }
    delay(10);
  }

  // Đọc response
  String response = "";
  while (client.available()) {
    response += client.readString();
  }

  client.stop();

  Serial.println("Server response:");
  Serial.println(response);

  // Parse JSON response
  int jsonStart = response.indexOf('{');
  if (jsonStart != -1) {
    String jsonResponse = response.substring(jsonStart);

    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, jsonResponse);

    if (!error) {
      bool success = doc["success"];
      const char* plate = doc["plate"];
      float confidence = doc["confidence"];
      const char* action = doc["action"];

      Serial.println("\n=== OCR Result ===");
      Serial.printf("Success: %s\n", success ? "YES" : "NO");

      if (success) {
        Serial.printf("License Plate: %s\n", plate);
        Serial.printf("Confidence: %.2f\n", confidence);
        Serial.printf("Action: %s\n", action);
        Serial.println("========");

        // Kiểm tra action từ server
        if (String(action) == "open_gate") {
          return true;  // Cho phép mở cổng
        }
      } else {
        const char* message = doc["message"];
        Serial.printf("Message: %s\n", message);
        Serial.println("========");
      }
    } else {
      Serial.println("✗ JSON parse failed!");
    }
  }

  return false;
}

void openGate() {
  Serial.println("\nOpening gate...");

  gateServo.write(90);  // Mở cổng (90 độ)
  delay(5000);          // Giữ mở 5 giây

  Serial.println("Closing gate...");
  gateServo.write(0);  // Đóng cổng

  Serial.println("Gate closed");
}
