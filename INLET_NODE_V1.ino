INLET NODE
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

#define RELAY_PIN   D5
#define LED_PIN     LED_BUILTIN
#define FAILSAFE_MS 6000
#define AP_SSID     "WATERSYSTEM"
#define AP_PASS     "12345678"

ESP8266WebServer server(80);
unsigned long lastCmdTime = 0;
bool valveOpen = false;

inline void valveClose() {
  digitalWrite(RELAY_PIN, HIGH);
  valveOpen = false;
}

inline void valveOpenFn() {
  digitalWrite(RELAY_PIN, LOW);
  valveOpen = true;
  lastCmdTime = millis();
}

void ensureWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  WiFi.begin(AP_SSID, AP_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 5000)
    delay(100);
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  valveClose();

  WiFi.mode(WIFI_STA);
  ensureWiFi();

  server.on("/open", []() {
    valveOpenFn();
    server.send(200, "text/plain", "OK");
  });

  server.on("/close", []() {
    valveClose();
    server.send(200, "text/plain", "OK");
  });

  server.on("/ping", []() {
    lastCmdTime = millis();
    server.send(200, "text/plain", "OK");
  });

  server.begin();
}

void loop() {
  ensureWiFi();
  server.handleClient();

  digitalWrite(LED_PIN, WiFi.status() == WL_CONNECTED ? LOW : HIGH);

  if (valveOpen && millis() - lastCmdTime > FAILSAFE_MS)
    valveClose();
}
