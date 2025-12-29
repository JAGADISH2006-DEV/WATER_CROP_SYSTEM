#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

/* ================= CONFIG ================= */
#define RELAY_PIN D1          // ACTIVE-LOW relay
#define FAILSAFE_MS 6000

IPAddress localIP(192,168,4,2);
IPAddress gateway(192,168,4,1);
IPAddress subnet(255,255,255,0);

/* ================= SERVER ================= */
ESP8266WebServer server(80);
unsigned long lastCmdTime = 0;
bool valveOpen = false;

/* ================= RELAY ================= */
inline void valveClose() {
  digitalWrite(RELAY_PIN, HIGH);
  valveOpen = false;
}

inline void valveOpenFn() {
  digitalWrite(RELAY_PIN, LOW);
  valveOpen = true;
}

/* ================= SETUP ================= */
void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  valveClose();

  WiFi.mode(WIFI_STA);
  WiFi.config(localIP, gateway, subnet);
  WiFi.begin("WATER_SYSTEM", "12345678");

  /* ---------- ENDPOINTS ---------- */
  server.on("/open", []() {
    valveOpenFn();
    lastCmdTime = millis();
    server.send(200, "text/plain", "OK");
  });

  server.on("/close", []() {
    valveClose();
    server.send(200, "text/plain", "OK");
  });

  server.begin();
}

/* ================= LOOP ================= */
void loop() {
  server.handleClient();

  /* ----- FAIL SAFE ----- */
  if (valveOpen && millis() - lastCmdTime > FAILSAFE_MS) {
    valveClose();
  }

  yield();
}
