#include <WiFi.h>
#include <WiFiAP.h>
#include <WiFiClient.h>

#define SERIAL_BAUDRATE 115200
#define TCP_PORT 8888

const char *ssid = "ESP32-Gateway";
const char *password = "12345678";

WiFiServer server(TCP_PORT);
WiFiClient clients[5];
WiFiClient monitorClient;

void setup() {
  Serial.begin(SERIAL_BAUDRATE);

  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP: ");
  Serial.println(IP);

  server.begin();
}

void loop() {
  // Chấp nhận kết nối mới
  if (server.hasClient()) {
    WiFiClient newClient = server.available();

    // Đọc định danh từ client
    String clientType = newClient.readStringUntil('\n');
    clientType.trim();

    if (clientType == "MONITOR") {
      monitorClient = newClient;
      Serial.println("Monitor connected!");
    } else {
      Serial.println("CAM connected");
    }
  }

  // Đọc lệnh Serial từ Py Server
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    if (monitorClient && monitorClient.connected()) {
      monitorClient.println(command);
      Serial.print("Sent to Monitor: ");
      Serial.println(command);
    } else {
      Serial.println("Monitor not connected!");
    }
  }

  // Xử lý dữ liệu từ các client
  for (int i = 0; i < 5; i++) {
    if (clients[i] && clients[i].connected()) {
      while (clients[i].available()) {
        String data = clients[i].readStringUntil('\n');
        Serial.printf("[CAM%d]%s\n", i, data.c_str());
      }
    }
  }
}