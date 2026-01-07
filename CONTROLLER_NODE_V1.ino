#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

/* ================= OLED ================= */
Adafruit_SSD1306 display(128, 64, &Wire, -1);

/* ================= PINS ================= */
#define OLED_SDA 21
#define OLED_SCL 22
#define ENC_CLK 32
#define ENC_DT  33
#define MODE_BTN 26
#define WATER_LEVEL_PIN 27
#define SOIL_PIN 34
#define AUTO_LED 2
#define NORMAL_LED 4

/* ================= WIFI ================= */
WebServer server(80);
IPAddress inletIP(192,168,4,2);
IPAddress outletIP(192,168,4,3);

/* ================= STORAGE ================= */
Preferences prefs;

/* ================= SYSTEM ================= */
enum Mode : uint8_t { OFF, AUTO, MANUAL };
volatile Mode currentMode = AUTO;

volatile uint8_t threshold = 70;
volatile uint16_t soil = 0;
volatile bool soilDry = false;
volatile bool waterDry = false;

volatile bool inletOpen  = false;
volatile bool outletOpen = false;

/* ================= CONNECTION STATUS ================= */
volatile bool inletOnline  = false;
volatile bool outletOnline = false;

unsigned long lastInletSeen  = 0;
unsigned long lastOutletSeen = 0;
const unsigned long OFFLINE_TIMEOUT = 5000;

/* ================= HEARTBEAT (INLET ONLY) ================= */
const unsigned long HEARTBEAT_MS = 2000;
unsigned long lastPing = 0;

/* ================= SYNC ================= */
SemaphoreHandle_t dataMutex;
volatile bool oledDirty = true;

/* ================= ENCODER ================= */
volatile int8_t encDelta = 0;
volatile int lastCLK;

/* ================= ISR ================= */
void IRAM_ATTR encoderISR() {
  int clk = digitalRead(ENC_CLK);
  if (clk != lastCLK)
    encDelta += (digitalRead(ENC_DT) != clk) ? 1 : -1;
  lastCLK = clk;
}

/* ================= NETWORK ================= */
bool sendValve(IPAddress ip, const char* cmd) {
  WiFiClient c;
  c.setTimeout(150);

  if (!c.connect(ip, 80)) return false;

  c.printf("GET /%s HTTP/1.1\r\nConnection: close\r\n\r\n", cmd);

  unsigned long t0 = millis();
  while (!c.available() && millis() - t0 < 120)
    vTaskDelay(1);

  bool ok = false;
  while (c.available()) {
    char line[32];
    c.readBytesUntil('\n', line, sizeof(line));
    if (strstr(line, "200")) ok = true;
  }

  c.stop();
  return ok;
}

/* ================= HEARTBEAT ================= */
inline void sendHeartbeatIfNeeded() {
  if (!inletOpen) return;

  if (millis() - lastPing >= HEARTBEAT_MS) {
    if (sendValve(inletIP, "ping")) {
      inletOnline = true;
      lastInletSeen = millis();
    }
    lastPing = millis();
  }
}

/* ================= WEB API ================= */
void handleStatus() {
  char json[256];
  snprintf(json, sizeof(json),
    "{"
    "\"mode\":\"%s\","
    "\"threshold\":%u,"
    "\"soilDry\":%d,"
    "\"waterDry\":%d,"
    "\"inlet\":{\"open\":%d,\"online\":%d},"
    "\"outlet\":{\"open\":%d,\"online\":%d}"
    "}",
    currentMode == AUTO ? "AUTO" :
    currentMode == MANUAL ? "MANUAL" : "OFF",
    threshold,
    soilDry,
    waterDry,
    inletOpen, inletOnline,
    outletOpen, outletOnline
  );
  server.send(200, "application/json", json);
}

void handleControl() {
  if (server.hasArg("mode")) {
    String m = server.arg("mode");
    if (m == "AUTO") currentMode = AUTO;
    else if (m == "MANUAL") currentMode = MANUAL;
    else if (m == "OFF") currentMode = OFF;
    prefs.putUChar("mode", currentMode);
  }

  if (server.hasArg("thr")) {
    threshold = constrain(server.arg("thr").toInt(), 0, 100);
    prefs.putUChar("thr", threshold);
  }

  oledDirty = true;
  server.send(200, "text/plain", "OK");
}

/* ================= WEB UI ================= */
const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
body{font-family:monospace;background:#0b0f14;color:#00e676}
.card{border:1px solid #00e676;padding:8px;margin:8px}
button,input{margin:4px}
</style>
</head>
<body>
<h3>Water Management System</h3>

<div class="card"><pre id="s">Loading...</pre></div>

<div class="card">
<b>Mode</b><br>
<button onclick="setMode('AUTO')">AUTO</button>
<button onclick="setMode('MANUAL')">MANUAL</button>
<button onclick="setMode('OFF')">OFF</button><br>
Threshold:
<input type="range" min="0" max="100" value="70" oninput="setThr(this.value)">
</div>

<script>
function update(){
 fetch('/status').then(r=>r.json())
 .then(j=>s.textContent=JSON.stringify(j,null,2));
}
function setMode(m){fetch('/control?mode='+m);}
function setThr(v){fetch('/control?thr='+v);}
setInterval(update,1000); update();
</script>
</body>
</html>
)rawliteral";

/* ================= UI TASK (CORE 0) ================= */
void uiTask(void *pv) {
  bool stable = HIGH, last = HIGH;
  unsigned long debounce = 0;

  uint8_t tapCount = 0;
  unsigned long lastTapTime = 0;
  const unsigned long TAP_WINDOW_MS = 700;

  for (;;) {
    bool r = digitalRead(MODE_BTN);
    if (r != last) debounce = millis();

    if (millis() - debounce > 40 && r != stable) {
      stable = r;
      if (stable == HIGH) {
        tapCount++;
        lastTapTime = millis();
      }
    }
    last = r;

    if (tapCount && millis() - lastTapTime > TAP_WINDOW_MS) {
      xSemaphoreTake(dataMutex, portMAX_DELAY);
      if (tapCount == 1) currentMode = AUTO;
      else if (tapCount == 2) currentMode = MANUAL;
      else currentMode = OFF;
      prefs.putUChar("mode", currentMode);
      tapCount = 0;
      oledDirty = true;
      xSemaphoreGive(dataMutex);
    }

    if (encDelta) {
      xSemaphoreTake(dataMutex, portMAX_DELAY);
      threshold = constrain((int)threshold + encDelta, 0, 100);
      encDelta = 0;
      prefs.putUChar("thr", threshold);
      oledDirty = true;
      xSemaphoreGive(dataMutex);
    }

if (oledDirty) {
  xSemaphoreTake(dataMutex, portMAX_DELAY);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  /* ===== HEADER ===== */
  display.setCursor(0, 0);
  display.print("MODE:");
  display.print(
    currentMode == AUTO ? "AUTO" :
    currentMode == MANUAL ? "MAN" : "OFF"
  );

  display.setCursor(80, 0);
  display.print("TH:");
  display.print(threshold);

  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  /* ===== SENSOR STATUS ===== */
  display.setCursor(0, 14);
  display.print("SOIL : ");
  display.print(soilDry ? "DRY " : "WET ");
  display.print("(");
  display.print(soil);
  display.print(")");

  display.setCursor(0, 26);
  display.print("WATER: ");
  display.print(waterDry ? "DRY" : "WET");

  display.drawLine(0, 38, 127, 38, SSD1306_WHITE);

  /* ===== VALVE STATUS ===== */
  display.setCursor(0, 42);
  display.print("IN  : ");
  display.print(inletOpen ? "ON " : "OFF");
  display.print(" ");
  display.print(inletOnline ? "OK" : "--");

  display.setCursor(0, 54);
  display.print("OUT : ");
  display.print(outletOpen ? "ON " : "OFF");
  display.print(" ");
  display.print(outletOnline ? "OK" : "--");

  display.display();
  oledDirty = false;

  xSemaphoreGive(dataMutex);
}

    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}

/* ================= CONTROL TASK (CORE 1) ================= */
void controlTask(void *pv) {
  bool lastIn  = false;
  bool lastOut = false;

  for (;;) {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
     soil = analogRead(SOIL_PIN);

    // DRY when value is LOW
    soilDry = soil < 1300;
     waterDry = digitalRead(WATER_LEVEL_PIN) == LOW;

    if (currentMode == OFF)
      inletOpen = false;
    else if (currentMode == MANUAL)
      inletOpen = soilDry && waterDry;
    else
      inletOpen = soilDry || waterDry;

    if (!waterDry && !soilDry)
      outletOpen = true;
    else if (waterDry)
      outletOpen = false;

    digitalWrite(AUTO_LED, currentMode == AUTO);
    digitalWrite(NORMAL_LED, currentMode == MANUAL);

    bool inChanged  = inletOpen  != lastIn;
    bool outChanged = outletOpen != lastOut;
    lastIn  = inletOpen;
    lastOut = outletOpen;

    xSemaphoreGive(dataMutex);

    if (inChanged) {
      if (sendValve(inletIP, inletOpen ? "open" : "close")) {
        inletOnline = true;
        lastInletSeen = millis();
      }
      lastPing = millis();
    }

    sendHeartbeatIfNeeded();

    if (outChanged) {
      if (sendValve(outletIP, outletOpen ? "open" : "close")) {
        outletOnline = true;
        lastOutletSeen = millis();
      }
    }

    if (millis() - lastInletSeen  > OFFLINE_TIMEOUT) inletOnline  = false;
    if (millis() - lastOutletSeen > OFFLINE_TIMEOUT) outletOnline = false;

    oledDirty = true;
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
}

/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(MODE_BTN, INPUT_PULLUP);
  pinMode(WATER_LEVEL_PIN, INPUT);
  pinMode(AUTO_LED, OUTPUT);
  pinMode(NORMAL_LED, OUTPUT);

  prefs.begin("water", false);
  threshold = prefs.getUChar("thr", 70);
  currentMode = (Mode)prefs.getUChar("mode", AUTO);

  lastCLK = digitalRead(ENC_CLK);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK), encoderISR, CHANGE);

  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  WiFi.softAP("WATERSYSTEM", "12345678");

  server.on("/", [](){ server.send_P(200,"text/html",PAGE); });
  server.on("/status", handleStatus);
  server.on("/control", handleControl);
  server.begin();

  dataMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(uiTask, "UI", 4096, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(controlTask, "CTRL", 4096, NULL, 1, NULL, 1);
}

void loop() {
  server.handleClient();
}
