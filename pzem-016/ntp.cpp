#include <WiFi.h>
#include <HTTPClient.h>
#include <ModbusMaster.h>
#include <ArduinoJson.h>
#include <time.h>

#define DE_RE_PIN  4
ModbusMaster node;

const char* ssid = "YOUR_WIFI";
const char* password = "YOUR_PASS";
const char* serverUrl = "http://yourserver.com/api/data";

// NTP server
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 7 * 3600;   // GMT+7 สำหรับไทย
const int daylightOffset_sec = 0;

// buffer
#define BUFFER_SIZE 50
struct PZEMData {
  String timestamp;
  float voltage;
  float current;
  float power;
  float energy;
  float frequency;
  float powerFactor;
  uint16_t alarm;
};
PZEMData dataBuffer[BUFFER_SIZE];
int head = 0;
int tail = 0;

void preTransmission() { digitalWrite(DE_RE_PIN, HIGH); }
void postTransmission() { digitalWrite(DE_RE_PIN, LOW); }

void pushData(PZEMData d) {
  int next = (head + 1) % BUFFER_SIZE;
  if (next != tail) {
    dataBuffer[head] = d;
    head = next;
  }
}

bool isBufferEmpty() { return head == tail; }
void clearBuffer() { tail = head; }

// ฟังก์ชันดึงเวลาปัจจุบัน
String getTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "1970-01-01T00:00:00";
  }
  char buf[30];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
  return String(buf);
}

// Task อ่านค่า PZEM ทุก 5 วินาที
void TaskReadPZEM(void *pvParameters) {
  for (;;) {
    PZEMData d;
    d.timestamp = getTimestamp();

    if (node.readInputRegisters(0x0000, 10) == node.ku8MBSuccess) {
      d.voltage     = node.getResponseBuffer(0) / 10.0;
      d.current     = (node.getResponseBuffer(1) | (node.getResponseBuffer(2) << 16)) / 1000.0;
      d.power       = (node.getResponseBuffer(3) | (node.getResponseBuffer(4) << 16)) / 10.0;
      d.energy      = (node.getResponseBuffer(5) | (node.getResponseBuffer(6) << 16)) * 1.0;
      d.frequency   = node.getResponseBuffer(7) / 10.0;
      d.powerFactor = node.getResponseBuffer(8) / 100.0;
      d.alarm       = node.getResponseBuffer(9);
      pushData(d);
    }

    vTaskDelay(5000 / portTICK_PERIOD_MS);
  }
}

// Task ส่งค่า HTTP POST
void TaskSendHTTP(void *pvParameters) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED && !isBufferEmpty()) {
      HTTPClient http;
      http.begin(serverUrl);
      http.addHeader("Content-Type", "application/json");

      DynamicJsonDocument doc(4096);
      JsonArray arr = doc.createNestedArray("records");

      int tempTail = tail;
      while (tempTail != head) {
        PZEMData d = dataBuffer[tempTail];
        JsonObject rec = arr.createNestedObject();
        rec["timestamp"]   = d.timestamp;
        rec["voltage"]     = d.voltage;
        rec["current"]     = d.current;
        rec["power"]       = d.power;
        rec["energy"]      = d.energy;
        rec["frequency"]   = d.frequency;
        rec["powerFactor"] = d.powerFactor;
        rec["alarm"]       = d.alarm;

        tempTail = (tempTail + 1) % BUFFER_SIZE;
      }

      String payload;
      serializeJson(doc, payload);

      int httpResponseCode = http.POST(payload);
      if (httpResponseCode == 200) {
        clearBuffer(); // ลบเฉพาะค่าที่ส่งสำเร็จ
      }
      http.end();
    }
    vTaskDelay(10000 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(DE_RE_PIN, OUTPUT);
  digitalWrite(DE_RE_PIN, LOW);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connected");

  // ตั้งค่า NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  Serial1.begin(9600);
  node.begin(1, Serial1);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  xTaskCreate(TaskReadPZEM, "ReadPZEM", 4096, NULL, 1, NULL);
  xTaskCreate(TaskSendHTTP, "SendHTTP", 8192, NULL, 1, NULL);
}

void loop() {}
