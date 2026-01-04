#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

#define RELAY_PIN   D5
#define LED_PIN     LED_BUILTIN   // ACTIVE LOW
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

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  pinMode(LED_PIN, OUTPUT);

  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_PIN, LOW); delay(200);
    digitalWrite(LED_PIN, HIGH); delay(800);

    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
      if (WiFi.SSID(i) == AP_SSID) {
        WiFi.begin(AP_SSID, AP_PASS);
        while (WiFi.status() != WL_CONNECTED) {
          digitalWrite(LED_PIN, LOW); delay(100);
          digitalWrite(LED_PIN, HIGH); delay(100);
        }
        digitalWrite(LED_PIN, LOW); // SOLID ON
        return;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  valveClose();

  connectWiFi();

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
  server.handleClient();

  if (WiFi.status() != WL_CONNECTED)
    digitalWrite(LED_PIN, HIGH); // OFF

  if (valveOpen && millis() - lastCmdTime > FAILSAFE_MS)
    valveClose();
}
