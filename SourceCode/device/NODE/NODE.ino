#include <WiFi.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include "env.h"

// Pins
#define SENSOR1_PIN 34      // Cảm biến hồng ngoại slot 1
#define SENSOR2_PIN 25      // Cảm biến hồng ngoại slot 2
#define LED_OTA_PIN 27      // LED sáng khi đang update OTA
#define LED_STATUS_PIN 12   // LED luôn sáng khi hoạt động bình thường

// MQTT Topics
#define MQTT_TOPIC_SLOT "iot/parking/slots"
#define MQTT_TOPIC_OTA "iot/parking/node/01/ota"

// Firmware version
#define FIRMWARE_VERSION "1.0.1"

// Slot IDs
const char* SLOT1_ID = "A1";
const char* SLOT2_ID = "A2";

// Biến trạng thái
WiFiClient espClient;
PubSubClient mqtt(espClient);

// Trạng thái cảm biến
bool lastState1 = false;  // Trạng thái trước đó của slot 1
bool lastState2 = false;  // Trạng thái trước đó của slot 2

// Debounce
unsigned long lastDebounceTime1 = 0;
unsigned long lastDebounceTime2 = 0;
const unsigned long debounceDelay = 500; // 500ms

// OTA Update
bool otaInProgress = false;

// WiFi
void connectWiFi() {
    Serial.println("[WiFi] Connecting to WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WiFi] Connected!");
        Serial.print("[WiFi] IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\n[WiFi] Connection failed!");
    }
}

// MQTT
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    Serial.print("[MQTT] Message arrived on topic: ");
    Serial.println(topic);
    
    // Parse JSON payload
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload, length);
    
    if (error) {
        Serial.print("[MQTT] JSON parse error: ");
        Serial.println(error.c_str());
        return;
    }
    
    // Kiểm tra nếu là lệnh OTA
    if (strcmp(topic, MQTT_TOPIC_OTA) == 0) {
        const char* command = doc["command"];
        
        if (command && strcmp(command, "update") == 0) {
            const char* firmwareUrl = doc["firmware_url"];
            int firmwareSize = doc["firmware_size"];
            const char* md5Hash = doc["md5"];
            
            Serial.println("[OTA] Update command received!");
            Serial.print("[OTA] Firmware URL: ");
            Serial.println(firmwareUrl);
            Serial.print("[OTA] Size: ");
            Serial.println(firmwareSize);
            Serial.print("[OTA] MD5: ");
            Serial.println(md5Hash ? md5Hash : "Not provided");
            
            // Trigger OTA update
            performOTAUpdate(firmwareUrl, firmwareSize, md5Hash);
        }
    }
}

void connectMQTT() {
    while (!mqtt.connected()) {
        Serial.print("[MQTT] Connecting to broker...");
        Serial.print(MQTT_BROKER);
        Serial.print(":");
        Serial.print(MQTT_PORT);
        Serial.print("...");
        
        String mac = WiFi.macAddress();
        mac.replace(":", ""); 

        String clientId = "ESP32_NODE_" + mac;
        
        // Kết nối MQTT 
        bool connected = false;
       
        connected = mqtt.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD);
        
        if (connected) {
            Serial.println(" Connected!");
            
            // Subscribe to OTA topic
            mqtt.subscribe(MQTT_TOPIC_OTA);
            Serial.print("[MQTT] Subscribed to: ");
            Serial.println(MQTT_TOPIC_OTA);
            
            // Publish online status
            publishStatus();
        } else {
            Serial.print(" Failed, rc=");
            Serial.print(mqtt.state());
            Serial.println(" Retrying in 5s...");
            delay(5000);
        }
    }
}

void publishSlotStatus(const char* slotID, bool isOccupied) {
    // Double check MQTT connection
    if (!mqtt.connected()) {
        Serial.println("[MQTT] MQTT disconnected");
        return;
    }
    
    // Tạo JSON message: {"slot":"A1","occupied":true}
    StaticJsonDocument<128> doc;
    doc["slot"] = slotID;
    doc["occupied"] = isOccupied;
    
    char message[128];
    serializeJson(doc, message);
    
    // Publish lên MQTT
    bool success = mqtt.publish(MQTT_TOPIC_SLOT, message, false);
    
    // Log kết quả
    Serial.print("[SLOT] [");
    Serial.print(slotID);
    Serial.print("] ");
    Serial.print(isOccupied ? "OCCUPIED" : "FREE");
    Serial.print(" → ");
    
    if (success) {
        Serial.print("[MQTT] Published: ");
        Serial.println(message);
    } else {
        Serial.println("[MQTT] Publish failed");
    }
}

void checkSensor(int sensorPin, const char* slotID, bool &lastState, unsigned long &lastDebounceTime) {
    // Đọc trạng thái cảm biến
    // LOW = có vật thể (occupied), HIGH = không có vật thể (free)
    bool currentState = (digitalRead(sensorPin) == LOW);
    
    // Kiểm tra nếu trạng thái thay đổi
    if (currentState != lastState) {
        unsigned long now = millis();
        
        // Debounce
        if (now - lastDebounceTime > debounceDelay) {
            lastDebounceTime = now;
            lastState = currentState;
            
            // Chỉ gửi nếu MQTT đã kết nối và không đang OTA
            if (mqtt.connected() && !otaInProgress) {
                publishSlotStatus(slotID, currentState);
            } else {
                Serial.print("[");
                Serial.print(slotID);
                Serial.println("] State changed but not published (MQTT disconnected or OTA in progress)");
            }
        }
    }
}

void publishStatus() {
    StaticJsonDocument<256> doc;
    doc["device"] = "NODE_01";
    doc["status"] = "online";
    doc["firmware_version"] = FIRMWARE_VERSION;
    doc["ip"] = WiFi.localIP().toString();
    
    char buffer[256];
    serializeJson(doc, buffer);
    
    mqtt.publish("iot/parking/node/01/status", buffer);
}

// OTA Update
void performOTAUpdate(const char* url, int expectedSize, const char* expectedMD5) {
    if (otaInProgress) {
        Serial.println("[OTA] Update already in progress");
        return;
    }
    
    otaInProgress = true;
    digitalWrite(LED_OTA_PIN, HIGH); // Bật LED OTA khi đang update
    
    Serial.println("[OTA] Starting OTA update");
    Serial.print("[OTA] URL: ");
    Serial.println(url);
    Serial.print("[OTA] Expected size: ");
    Serial.println(expectedSize);
    Serial.print("[OTA] Expected MD5: ");
    Serial.println(expectedMD5 ? expectedMD5 : "Not provided");
    
    // Kiểm tra partition space
    size_t updateSize = (expectedSize > 0) ? expectedSize : UPDATE_SIZE_UNKNOWN;
    
    HTTPClient http;
    http.begin(url);
    http.setTimeout(30000); // 30 seconds timeout
    
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
        int contentLength = http.getSize();
        Serial.print("[OTA] Content length from server: ");
        Serial.println(contentLength);
        
        if (contentLength > 0) {
            // Set expected MD5 if provided
            if (expectedMD5 && strlen(expectedMD5) > 0) {
                Update.setMD5(expectedMD5);
                Serial.println("[OTA] MD5 verification enabled");
            }
            
            // Sử dụng contentLength
            if (!Update.begin(contentLength, U_FLASH)) {
                Serial.println("[OTA] ERROR Not enough space for OTA!");
                Serial.print("[OTA] Free sketch space: ");
                Serial.println(ESP.getFreeSketchSpace());
                Serial.print("[OTA] Required: ");
                Serial.println(contentLength);
                Update.printError(Serial);
            } else {
                Serial.println("[OTA] Begin update successful");
                Serial.print("[OTA] Free sketch space: ");
                Serial.println(ESP.getFreeSketchSpace());
                
                WiFiClient* stream = http.getStreamPtr();
                
                // Progress tracking
                size_t written = 0;
                size_t total = contentLength;
                uint8_t buff[128];
                int lastPercent = 0;
                
                while (http.connected() && (written < total)) {
                    size_t available = stream->available();
                    
                    if (available) {
                        int c = stream->readBytes(buff, min(available, sizeof(buff)));
                        
                        if (c > 0) {
                            if (Update.write(buff, c) != c) {
                                Serial.println("[OTA] ERROR Write failed");
                                break;
                            }
                            written += c;
                            
                            // Show progress every 10%
                            int percent = (written * 100) / total;
                            if (percent >= lastPercent + 10) {
                                lastPercent = percent;
                                Serial.print("[OTA] Progress: ");
                                Serial.print(percent);
                                Serial.print("% (");
                                Serial.print(written);
                                Serial.print("/");
                                Serial.print(total);
                                Serial.println(")");
                            }
                        }
                    }
                    delay(1);
                }
                
                Serial.print("[OTA] Written: ");
                Serial.print(written);
                Serial.print(" / ");
                Serial.println(total);
                
                if (written == total) {
                    Serial.println("[OTA] SUCCESS All bytes written successfully!");
                } else {
                    Serial.println("[OTA] ERROR Write size mismatch");
                }
                
                if (Update.end(true)) {
                    if (Update.isFinished()) {
                        Serial.println("[OTA] SUCCESS Update finished successfully");
                        
                        // Check MD5 if it was set
                        if (expectedMD5 && strlen(expectedMD5) > 0) {
                            if (Update.hasError()) {
                                Serial.println("[OTA] ERROR MD5 verification failed");
                                Serial.println("[OTA] Rolling back to previous firmware...");
                                
                                // Publish failure status
                                StaticJsonDocument<128> statusDoc;
                                statusDoc["status"] = "failed";
                                statusDoc["message"] = "MD5 verification failed";
                                
                                char statusBuffer[128];
                                serializeJson(statusDoc, statusBuffer);
                                mqtt.publish("iot/parking/node/01/ota/status", statusBuffer);
                                
                                otaInProgress = false;
                                digitalWrite(LED_OTA_PIN, LOW);
                                return;
                            } else {
                                Serial.println("[OTA] SUCCESS MD5 verification passed!");
                            }
                        }
                        
                        Serial.println("[OTA] Rebooting in 3 seconds");
                        
                        // Publish success status
                        StaticJsonDocument<128> statusDoc;
                        statusDoc["status"] = "success";
                        statusDoc["message"] = "OTA completed, rebooting...";
                        
                        char statusBuffer[128];
                        serializeJson(statusDoc, statusBuffer);
                        mqtt.publish("iot/parking/node/01/ota/status", statusBuffer);
                        
                        delay(3000);
                        ESP.restart();
                    } else {
                        Serial.println("[OTA] ERROR Update not finished");
                    }
                } else {
                    Serial.print("[OTA] ERROR Update error: ");
                    Serial.println(Update.getError());
                    Update.printError(Serial);
                }
            }
        } else {
            Serial.println("[OTA] ERROR Invalid content length");
        }
    } else {
        Serial.print("[OTA] ERROR HTTP error: ");
        Serial.println(httpCode);
    }
    
    http.end();
    otaInProgress = false;
    digitalWrite(LED_OTA_PIN, LOW); // Tắt LED OTA sau khi update xong
}

// Setup & Loop
void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n==================================");
    Serial.println("NODE - Parking Slot Sensor");
    Serial.print("Firmware Version: ");
    Serial.println(FIRMWARE_VERSION);
    Serial.println("==================================\n");
    
    // Setup pins
    pinMode(SENSOR1_PIN, INPUT);
    pinMode(SENSOR2_PIN, INPUT);
    pinMode(LED_OTA_PIN, OUTPUT);
    pinMode(LED_STATUS_PIN, OUTPUT);
    
    digitalWrite(LED_OTA_PIN, LOW);
    digitalWrite(LED_STATUS_PIN, LOW);  // LED status
    
    Serial.println("[SENSOR] SUCCESS Sensors initialized");
    
    // Connect WiFi
    connectWiFi();
    
    // Setup MQTT
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    mqtt.setCallback(mqttCallback);
    mqtt.setBufferSize(512);
    
    // Connect MQTT
    connectMQTT();
    
    Serial.println("[SYSTEM] Initialization complete!");
}

void loop() {
    // Maintain connections
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
    }
    
    if (!mqtt.connected()) {
        connectMQTT();
    }
    
    mqtt.loop();
    
    // Skip sensor reading if OTA in progress
    if (otaInProgress) {
        return;
    }
    
    // Đọc và xử lý trạng thái 2 cảm biến với debounce
    checkSensor(SENSOR1_PIN, SLOT1_ID, lastState1, lastDebounceTime1);
    checkSensor(SENSOR2_PIN, SLOT2_ID, lastState2, lastDebounceTime2);
    
    delay(100);
}
