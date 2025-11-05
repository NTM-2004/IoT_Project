#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>
#include "FS.h"
#include "SD_MMC.h"
#include "../env.h"

// WiFi credentials
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// API config
const char* host = "api.platerecognizer.com";
const int port = 443;
const char* token = API_TOKEN;

#define flashLight 4  // GPIO pin for the flashlight
int count = 0;        // Counter for slots

WiFiClientSecure client;  // Secure client for HTTPS communication

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

// NTP setup
const char* ntpServer = "pool.ntp.org";  // NTP server
const long utcOffsetInSeconds = 25200;   // IST offset (UTC + 7)
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, utcOffsetInSeconds);
String currentTime = "";

// Servo setup
int servoPin = 14;                       // GPIO pin for the servo motor
Servo myservo;                           // Servo object
int pos = 0;                             

// Sensors setup
int inSensor = 13;                       // GPIO pin for the entry sensor
int outSensor = 15;                      // GPIO pin for the exit sensor

// Web server on port 80
WebServer server(80);

// Define variables hold information
String recognizedPlate = "";  
String imageLink = "";        
String timeStamp = "";
String currentStatus = "Idle";  
int availableSpaces = 8;        
int vehicalCount = 0;          
int barrierDelay = 3000;        
int siteRefreshTime = 1;        

// Plate records
struct PlateEntry {
  String plateNumber;  // Plate number of the vehicle
  String time;         // Entry time of the vehicle
};

std::vector<PlateEntry> plateHistory;  // Vector to store the history of valid plates

// Function to extract a JSON string value by key
String parsePlateJson(const String& jsonString, char param) {
  const size_t capacity = 2048;
  DynamicJsonDocument doc(capacity);

  DeserializationError error = deserializeJson(doc, jsonString);
  if (error) {
    return "error";
  }

  if (param == 'p') {
    // Return plate has highest score
    float highestScore = -1.0;
    String bestPlate = "";
    JsonArray results = doc["results"];
    for (JsonObject result : results) {
      float score = result["score"];
      String plate = result["plate"].as<String>();
      if (score > highestScore) {
        highestScore = score;
        bestPlate = plate;
      }
    }
    return bestPlate;
  } else if (param == 't') {
    // Return timestamp
    return doc["timestamp"].as<String>();
  } else if (param == 'n') {
    // Return file name
    return doc["filename"].as<String>();
  }

  return "invalid_param";
}


// Function to handle the root web page
void handleRoot() {
  String html = "<!DOCTYPE html><html lang='en'><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Smart Parking System</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; background-color: #f4f4f9; margin: 0; padding: 0; color: #333; }";
  html += ".container { max-width: 1200px; margin: 0 auto; padding: 20px; box-sizing: border-box; }";
  html += "header { text-align: center; padding: 15px; background-color: #0e3d79; color: white; }";
  html += "h1, h2 { text-align: center; margin-bottom: 20px; }";  // Center align all headers
  html += "p { margin: 10px 0; }";
  html += "table { width: 100%; border-collapse: collapse; margin: 20px 0; }";
  html += "th, td { padding: 10px; text-align: left; border: 1px solid #ddd; }";
  html += "tr:nth-child(even) { background-color: #f9f9f9; }";
  html += "form { text-align: center; margin: 20px 0; }";
  html += "input[type='submit'] { background-color: #007bff; color: white; border: none; padding: 10px 20px; font-size: 16px; cursor: pointer; border-radius: 5px; }";
  html += "input[type='submit']:hover { background-color: #0056b3; }";
  html += "a { color: #007bff; text-decoration: none; }";
  html += "a:hover { text-decoration: underline; }";
  html += "img { max-width: 100%; height: auto; margin: 20px 0; display: none; }";  // Initially hide the image
  html += "@media (max-width: 768px) { table { font-size: 14px; } }";
  html += "</style>";
  html += "<meta http-equiv='refresh' content='" + String(siteRefreshTime) + "'>";  // Refresh every x second
  html += "</head><body>";
  html += "<header><h1>Smart Parking System</h1></header>";
  html += "<div class='container'>";
  html += "<h1>Smart Parking System using ESP32-CAM</h1>";
  html += "<p><strong>Time:</strong> " + currentTime + "</p>";
  html += "<p><strong>Status:</strong> " + currentStatus + "</p>";
  html += "<p><strong>Last Recognized Plate:</strong> " + recognizedPlate + "</p>";
  html += "<p><strong>Last Captured Image:</strong> <a href=\"" + imageLink + "\" target=\"_blank\">View Image</a></p>";

  // html += "<form action=\"/trigger\" method=\"POST\">";
  // html += "<input type=\"submit\" value=\"Capture Image\">";
  // html += "</form>";

  html += "<p><strong>Spaces available:</strong> " + String(availableSpaces - vehicalCount) + "</p>";

  html += "<h2>Parking Database</h2>";
  if (plateHistory.empty()) {
    html += "<p>No valid number plates recognized yet.</p>";
  } else {
    html += "<table><tr><th>Plate Number</th><th>Time</th></tr>";
    for (const auto& entry : plateHistory) {
      html += "<tr><td>" + entry.plateNumber + "</td><td>" + entry.time + "</td></tr>";
    }
    html += "</table>";
  }

  html += "<script>";
  html += "function toggleImage() {";
  html += "  var img = document.getElementById('capturedImage');";
  html += "  if (img.style.display === 'none') {";
  html += "    img.style.display = 'block';";
  html += "  } else {";
  html += "    img.style.display = 'none';";
  html += "  }";
  html += "}";
  html += "</script>";

  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

// Function to handle image capture trigger
void handleTrigger() {
  currentStatus = "Capturing Image";
  server.handleClient();
  // server.sendHeader("Location", "/");  // Redirect to root to refresh status
  // server.send(303);                    // Send redirect response to refresh the page

  // Perform the image capture and upload
  int status = sendPhoto();

  // Update status based on sendPhoto result
  if (status == -1) {
    currentStatus = "Image Capture Failed";
  } else if (status == -2) {
    currentStatus = "Server Connection Failed";
  } else if (status == 1) {
    currentStatus = "No Parking Space Available";
  } else if (status == 2) {
    currentStatus = "Invalid Plate Recognized [No Entry]";
  } else {
    currentStatus = "Idle";
  }
  server.handleClient();  // Update status on webpage
}

void openBarrier() {
  currentStatus = "Barrier Opening";
  server.handleClient();  // Update status on webpage
  Serial.println("Barrier Opens");
  myservo.write(0);
  delay(barrierDelay);
}

void closeBarrier() {
  currentStatus = "Barrier Closing";
  server.handleClient();  // Update status on webpage
  Serial.println("Barrier Closes");
  myservo.write(180);
  delay(barrierDelay);
}

// Function to capture and send photo to the server
int sendPhoto() {
  camera_fb_t* fb = NULL;
  String response = "";
  // Turn on flashlight and capture image
  // digitalWrite(flashLight, HIGH);

  delay(300);
  fb = esp_camera_fb_get();
  delay(300);

  // digitalWrite(flashLight, LOW);
  if (!fb) {
    Serial.println("Camera capture failed");
    currentStatus = "Image Capture Failed";
    server.handleClient();  // Update status on webpage
    return -1;
  } else {Serial.println("Capture Successfully");}

  client.setInsecure();  // Disable SSL

  if (!client.connect(host, port)) {
    Serial.println("Connection failed");
    return -1;
  }

  String boundary = "----ESP32Boundary";
  String startRequest =
    "--" + boundary + "\r\n"
                      "Content-Disposition: form-data; name=\"upload\"; filename=\"esp32.jpg\"\r\n"
                      "Content-Type: image/jpeg\r\n\r\n";

  String endRequest = "\r\n--" + boundary + "--\r\n";

  int contentLength = startRequest.length() + fb->len + endRequest.length();

  // Header
  client.printf("POST /v1/plate-reader/ HTTP/1.1\r\n");
  client.printf("Host: %s\r\n", host);
  client.println("Connection: close");
  client.printf("Authorization: %s\r\n", token);
  client.printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary.c_str());
  client.printf("Content-Length: %d\r\n", contentLength);
  client.println();  // End of header

  // Send header
  client.print(startRequest);

  // Send image chunk by chunk
  size_t index = 0;
  const size_t chunkSize = 1024;
  while (index < fb->len) {
    size_t toSend = min(chunkSize, fb->len - index);
    client.write(fb->buf + index, toSend);
    index += toSend;
  }

  // End request
  client.print(endRequest);

  // Read response
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;  // End of header
  }

  Serial.println("Server response:");
  while (client.available()) {
    String line = client.readStringUntil('\n');
    response += line;
    response += " ";
    Serial.println(line);
  }

  // Release the frame buffer
  esp_camera_fb_return(fb);
  Serial.println("Image sent successfully");

  server.handleClient();  // Update status on webpage

  // Extract data from response
  recognizedPlate = parsePlateJson(response, 'p');
  timeStamp = parsePlateJson(response, 't');
  imageLink = parsePlateJson(response, 'n');

  currentStatus = "Response Recieved Successfully";
  server.handleClient();  // Update status on webpage

  // Add valid plate to history
  if (vehicalCount > availableSpaces) {
    // Log response and return
    Serial.print("Response: ");
    Serial.println(response);
    client.stop();
    esp_camera_fb_return(fb);
    return 1;

  } else if (recognizedPlate.length() > 4 && recognizedPlate.length() < 11) {
    // Valid plate
    PlateEntry newEntry;
    newEntry.plateNumber = recognizedPlate + "-Entry";
    newEntry.time = currentTime;  // Use the current timestamp
    plateHistory.push_back(newEntry);
    vehicalCount++;

    openBarrier();
    delay(barrierDelay);
    closeBarrier();

    // Save to SD
    // Create name
    // String cleanPlate = recognizedPlate;
    // cleanPlate.replace(" ", "_");  
    // cleanPlate.replace(":", "_");  
    // cleanPlate.replace("-", "_"); 

    // String cleanTime = timeStamp;
    // cleanTime.replace(":", "");
    // cleanTime.replace("-", "");
    // cleanTime.replace(" ", "_");

    // // Truncate if too long 
    // String path = "/pic_" + cleanPlate + "_" + cleanTime + ".jpg";
    // if (path.length() > 31) {
    //   path = path.substring(0, 27) + ".jpg"; 
    // }

    // File file = SD_MMC.open(path.c_str(), FILE_WRITE);

    // if (!file) {
    //   Serial.println("Failed to open file in writing mode");
    // } else {
    //   file.write(fb->buf, fb->len);  // Write
    //   Serial.println("Saved file to: " + path);
    // }

    // file.close();

    // Log response and return
    // Serial.print("Response: ");
    // Serial.println(response);
    client.stop();
    esp_camera_fb_return(fb);
    return 0;

  } else {
    currentStatus = "Invalid Plate Recognized '" + recognizedPlate + "' " + "[NoopenBarrier Entry]";
    server.handleClient();  // Update status on webpage
    // Log response and return
    Serial.print("Response: ");
    Serial.println(response);
    client.stop();
    esp_camera_fb_return(fb);
    return 2;
  }
}

void setup() {
  // Disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  pinMode(flashLight, OUTPUT);
  pinMode(inSensor, INPUT_PULLUP);
  pinMode(outSensor, INPUT_PULLUP);
  digitalWrite(flashLight, LOW);

  // Connect to WiFi
  WiFi.mode(WIFI_STA);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.print("ESP32-CAM IP Address: ");
  Serial.println(WiFi.localIP());

  // Initialize NTPClient
  timeClient.begin();
  timeClient.update();

  // Start the web server
  server.on("/", handleRoot);
  server.on("/trigger", HTTP_POST, handleTrigger);
  server.begin();
  Serial.println("Web server started");

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

  // Adjust frame size and quality based on PSRAM availability
  if (psramFound()) {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 5;  // Lower number means higher quality (0-63)
    config.fb_count = 2;
    Serial.println("PSRAM found");
  } else {
    config.frame_size = FRAMESIZE_CIF;
    config.jpeg_quality = 12;  // Lower number means higher quality (0-63)
    config.fb_count = 1;
  }

  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(1000);
    ESP.restart();
  }
  sensor_t* s = esp_camera_sensor_get();

  // Flip left-right
  s->set_hmirror(s, 1);  // 1 = ON, 0 = OFF
  // Flip up-down
  s->set_vflip(s, 1);  // 1 = ON, 0 = OFF

  // Allow allocation of all timers
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  myservo.setPeriodHertz(50);            // standard 50 hz servo
  myservo.attach(servoPin, 1000, 2000);  // attaches the servo on pin 18 to the servo object
    // Set the initial position of the servo (barrier closed)
  myservo.write(180);

  // Setup SD card
  // if (!SD_MMC.begin()) {
  //   Serial.println("SD Card Mount Failed");
  //   return;
  // }
  // uint8_t cardType = SD_MMC.cardType();
  // if (cardType == CARD_NONE) {
  //   Serial.println("No SD card attached");
  //   return;
  // }
  // Serial.println("SD Card initialized.");
}

void loop() {
  // Update the NTP client to get the current time
  timeClient.update();
  currentTime = timeClient.getFormattedTime();

  // Check the web server for any incoming client requests
  server.handleClient();

  // Monitor sensor states for vehicle entry/exit
  if (digitalRead(inSensor) == LOW && vehicalCount < availableSpaces) {
    delay(2000);      // delay for vehicle need to be in a position
    handleTrigger();  // Trigger image capture for entry
  }

  if (digitalRead(outSensor) == LOW && vehicalCount > 0) {
    delay(2000);  // delay for vehicle need to be in a position

    openBarrier();
    PlateEntry newExit;
    newExit.plateNumber = "NULL-Exit";
    newExit.time = currentTime;  // Use the current timestamp
    plateHistory.push_back(newExit);
    delay(barrierDelay);
    vehicalCount--;
    closeBarrier();

    currentStatus = "Idle";
    server.handleClient();  
  }
}