#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// WiFi AP credentials
const char* ssid = "ESP32_CAM_AP";
const char* password = "12345678";

// Web server on port 80
WebServer server(80);

// Biến lưu trữ ảnh
uint8_t* imageBuffer = NULL;
uint32_t imageSize = 0;
bool imageReceived = false;


// Handler cho trang chủ
void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<title>Image Receiver</title></head><body>";
  html += "<h1>ESP32 Image Receiver</h1>";
  html += "<p>Status: Waiting for images...</p>";
  if (imageReceived) {
    html += "<p>Last image size: " + String(imageSize) + " bytes</p>";
  }
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// Handler nhận ảnh qua POST
void handleImageUpload() {
  HTTPUpload& upload = server.upload();
  
  if (upload.status == UPLOAD_FILE_START) {
    Serial.println("\n=== Image Upload Started ===");
    imageReceived = false;
    
    // Giải phóng buffer cũ nếu có
    if (imageBuffer != NULL) {
      free(imageBuffer);
      imageBuffer = NULL;
    }
    imageSize = 0;
    
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    // Nhận dữ liệu
    if (imageBuffer == NULL) {
      // Cấp phát bộ nhớ lần đầu - dự phòng 100KB
      imageBuffer = (uint8_t*)malloc(100000);
      if (imageBuffer == NULL) {
        Serial.println("Failed to allocate memory!");
        return;
      }
    }
    
    // Copy dữ liệu vào buffer
    if (imageSize + upload.currentSize <= 100000) {
      memcpy(imageBuffer + imageSize, upload.buf, upload.currentSize);
      imageSize += upload.currentSize;
      Serial.printf("Received %d bytes (total: %d)\n", upload.currentSize, imageSize);
    } else {
      Serial.println("Buffer overflow!");
    }
    
  } else if (upload.status == UPLOAD_FILE_END) {
    Serial.println("\n=== Image Upload Complete ===");
    Serial.printf("Total size: %d bytes\n", imageSize);
    imageReceived = true;
    
    // Xử lý ảnh ở đây nếu cần
    processImage();
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Serial.println("Upload aborted!");
    if (imageBuffer != NULL) {
      free(imageBuffer);
      imageBuffer = NULL;
    }
    imageSize = 0;
  }
}

// Handler kết thúc upload
void handleImageUploadEnd() {
  if (imageReceived) {
    server.send(200, "text/plain", "OK");
    Serial.println("Sent ACK to CAM");
  } else {
    server.send(500, "text/plain", "Upload failed");
  }
}

// Hàm xử lý ảnh đã nhận
void processImage() {
  Serial.println("Processing image...");
  Serial.printf("Image ready for processing. Size: %d bytes\n", imageSize);
  
  // Gửi ảnh qua UART để lưu vào máy tính
  sendImageOverUART();
}

// Hàm gửi ảnh qua UART
void sendImageOverUART() {
  if (imageBuffer == NULL || imageSize == 0) {
    Serial.println("No image to send!");
    return;
  }
  
  Serial.println("\n=== SENDING IMAGE VIA UART ===");
  
  // Gửi header: START_IMAGE
  Serial.println("START_IMAGE");
  
  // Gửi kích thước ảnh
  Serial.print("SIZE:");
  Serial.println(imageSize);
  
  // Gửi dữ liệu ảnh dưới dạng Base64 hoặc HEX
  // Sử dụng HEX để dễ parse
  Serial.println("DATA_START");
  
  // Gửi từng byte dưới dạng HEX
  for (uint32_t i = 0; i < imageSize; i++) {
    if (imageBuffer[i] < 0x10) {
      Serial.print("0");
    }
    Serial.print(imageBuffer[i], HEX);
    
    // Xuống dòng mỗi 32 bytes để dễ đọc
    if ((i + 1) % 32 == 0) {
      Serial.println();
    }
  }
  
  Serial.println();
  Serial.println("DATA_END");
  Serial.println("END_IMAGE");
  Serial.println("=== IMAGE SENT VIA UART ===\n");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== ESP32 Image Receiver ===");
  
  // Tạo WiFi Access Point
  Serial.println("Creating WiFi AP...");
  WiFi.mode(WIFI_AP);
  
  // Cấu hình AP với channel cố định
  bool apSuccess = WiFi.softAP(ssid, password, 1, 0, 4);  // channel 1, hidden=0, max_connection=4
  
  if (!apSuccess) {
    Serial.println("AP creation failed!");
    delay(1000);
    ESP.restart();
  }
  
  delay(500);  // Đợi AP khởi động
  
  IPAddress IP = WiFi.softAPIP();
  Serial.println("AP Created!");
  Serial.print("AP IP address: ");
  Serial.println(IP);
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.print("Password: ");
  Serial.println(password);
  
  // Khởi động web server
  server.on("/", HTTP_GET, handleRoot);
  server.on("/upload", HTTP_POST, handleImageUploadEnd, handleImageUpload);
  
  server.begin();
  
  Serial.println("HTTP server started");
  Serial.println("Waiting for images...");
}

void loop() {
  server.handleClient();
  delay(2);
}