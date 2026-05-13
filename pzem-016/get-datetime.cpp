#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

const char* ssid = "YOUR_WIFI";
const char* password = "YOUR_PASS";
const char* timeApiUrl = "http://yourserver.com/api/time";

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connected");

  // เรียก API เพื่อ sync เวลา
  HTTPClient http;
  http.begin(timeApiUrl);
  int httpResponseCode = http.GET();
  if (httpResponseCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(512);
    deserializeJson(doc, payload);
    const char* ts = doc["timestamp"]; // เช่น "2026-05-13T11:12:00"

    struct tm tm;
    strptime(ts, "%Y-%m-%dT%H:%M:%S", &tm);
    time_t t = mktime(&tm);

    struct timeval now = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&now, NULL);

    Serial.println("Time synced from API");
  }
  http.end();
}

void loop() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char buf[30];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &timeinfo);
    Serial.println(buf);
  }
  delay(5000);