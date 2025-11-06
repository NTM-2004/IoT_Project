#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include <ESP32Servo.h>

// WiFi credentials của RECEIVER
const char* ssid = "ESP32_CAM_AP";
const char* password = "12345678";

// IP và port của RECEIVER
const char* serverIP = "192.168.4.1";
const int serverPort = 80;

#define flashLight 4  // GPIO pin for the flashlight

// Biến trạng thái
bool imageAcknowledged = false;
bool waitingForAck = false;

// Camera GPIO pins
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

// Servo setup
int servoPin = 14;                       // GPIO pin for the servo motor
Servo myservo;                           // Servo object

// IR Sensor setup
int irSensor = 13;                       // GPIO pin for the IR sensor

// Hàm chụp và gửi ảnh qua WiFi Client (giống code cũ)
bool captureAndSendImage() {
  camera_fb_t* fb = NULL;
  WiFiClient client;
  
  Serial.println("Capturing image...");
  
  // Chụp ảnh
  delay(300);
  fb = esp_camera_fb_get();
  delay(300);
  
  if (!fb) {
    Serial.println("Camera capture failed");
    return false;
  }
  
  Serial.println("Capture Successfully!");
  Serial.printf("Image size: %d bytes\n", fb->len);
  
  // Kết nối đến server
  if (!client.connect(serverIP, serverPort)) {
    Serial.println("Connection to server failed");
    esp_camera_fb_return(fb);
    return false;
  }
  
  Serial.println("Connected to server");
  
  // Tạo boundary cho multipart/form-data
  String boundary = "----ESP32Boundary";
  String startRequest = "--" + boundary + "\r\n"
                        "Content-Disposition: form-data; name=\"upload\"; filename=\"esp32.jpg\"\r\n"
                        "Content-Type: image/jpeg\r\n\r\n";
  String endRequest = "\r\n--" + boundary + "--\r\n";
  
  int contentLength = startRequest.length() + fb->len + endRequest.length();
  
  // Gửi HTTP POST header
  client.printf("POST /upload HTTP/1.1\r\n");
  client.printf("Host: %s\r\n", serverIP);
  client.println("Connection: close");
  client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary.c_str());
  client.printf("Content-Length: %d\r\n", contentLength);
  client.println();  // End of header
  
  // Gửi start boundary
  client.print(startRequest);
  
  // Gửi ảnh theo chunks
  Serial.println("Sending image...");
  size_t index = 0;
  const size_t chunkSize = 1024;
  while (index < fb->len) {
    size_t toSend = min(chunkSize, fb->len - index);
    client.write(fb->buf + index, toSend);
    index += toSend;
    
    // In progress mỗi 10KB
    if (index % 10240 == 0 || index == fb->len) {
      Serial.printf("Sent %d/%d bytes\n", index, fb->len);
    }
  }
  
  // Gửi end boundary
  client.print(endRequest);
  
  Serial.println("Image sent, waiting for response...");
  
  // Đọc response từ server
  unsigned long timeout = millis();
  while (client.connected() && (millis() - timeout < 10000)) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      Serial.println(line);
      
      // Kiểm tra HTTP status code
      if (line.indexOf("HTTP/1.1 200") >= 0) {
        Serial.println("Server accepted image!");
        imageAcknowledged = true;
      }
    }
    delay(10);
  }
  
  // Giải phóng bộ nhớ
  esp_camera_fb_return(fb);
  client.stop();
  
  if (imageAcknowledged) {
    Serial.println("Image transfer successful!");
    return true;
  } else {
    Serial.println("No OK response from server");
    return false;
  }
}

// Hàm mở servo (barrier)
void openBarrier() {
  Serial.println("Opening barrier...");
  myservo.write(90);  // Mở servo (điều chỉnh góc theo servo của bạn)
  delay(3000);        // Giữ mở 3 giây
  myservo.write(0);   // Đóng servo
  Serial.println("Barrier closed");
}

void setup() {
  // Disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(1000);
  
  pinMode(flashLight, OUTPUT);
  pinMode(irSensor, INPUT_PULLUP);
  digitalWrite(flashLight, LOW);

  // Configure camera
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

  // Adjust frame size and quality
  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;  // 640x480
    config.jpeg_quality = 10;
    config.fb_count = 2;
    Serial.println("PSRAM found");
  } else {
    config.frame_size = FRAMESIZE_QVGA;  // 320x240
    config.jpeg_quality = 12;  
    config.fb_count = 1;
    Serial.println("PSRAM not found");
  }

  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    delay(1000);
    ESP.restart();
  }
  Serial.println("Camera initialized!");
  
  sensor_t* s = esp_camera_sensor_get();
  s->set_hmirror(s, 1);
  s->set_vflip(s, 1);

  // Kết nối WiFi đến RECEIVER AP
  Serial.println("Connecting to WiFi AP...");
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
  } else {
    Serial.println("\nFailed to connect to WiFi!");
    Serial.println("Restarting...");
    delay(1000);
    ESP.restart();
  }

  // Khởi tạo servo
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  myservo.setPeriodHertz(50);
  myservo.attach(servoPin, 1000, 2000);
  myservo.write(0);
  
  Serial.println("Setup complete! Waiting for IR trigger...");
}

void loop() {
  // Kiểm tra cảm biến hồng ngoại
  if (digitalRead(irSensor) == LOW && !waitingForAck) {
    Serial.println("IR Sensor triggered!");
    delay(500);  // Debounce
    
    // Kiểm tra WiFi còn kết nối không
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected! Reconnecting...");
      WiFi.reconnect();
      delay(2000);
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Reconnect failed!");
        delay(2000);
        return;
      }
    }
    
    // Reset flag
    imageAcknowledged = false;
    
    // Chụp và gửi ảnh
    bool success = captureAndSendImage();
    
    // Nếu gửi thành công, mở servo
    if (success) {
      Serial.println("Opening barrier...");
      openBarrier();
    } else {
      Serial.println("Failed to send image!");
    }
    
    delay(2000);  // Delay trước khi cho phép trigger tiếp theo
  }
}