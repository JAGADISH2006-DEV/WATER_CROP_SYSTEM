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
enum Mode : uint8_t { OFF, AUTO, NORMAL };
volatile Mode currentMode = OFF;

volatile uint8_t threshold = 70;
volatile uint16_t soil = 0;
volatile bool waterHigh = false;

volatile bool inletOpen = false;
volatile bool outletOpen = false;

volatile bool inletOnline = false;
volatile bool outletOnline = false;

unsigned long lastInletSeen = 0;
unsigned long lastOutletSeen = 0;
const unsigned long OFFLINE_TIMEOUT = 5000;

/* ================= SYNC ================= */
SemaphoreHandle_t dataMutex;
volatile bool oledDirty = true;

/* ================= ENCODER ================= */
volatile int8_t encDelta = 0;
int lastCLK;

/* ================= ISR ================= */
void IRAM_ATTR encoderISR() {
  int clk = digitalRead(ENC_CLK);
  if (clk != lastCLK) {
    encDelta += (digitalRead(ENC_DT) != clk) ? 1 : -1;
  }
  lastCLK = clk;
}

/* ================= HELPERS ================= */
const char* moistureText(uint16_t v) {
  if (v > 2500) return "DRY";
  if (v > 1200) return "MOIST";
  return "HIGH";
}

/* ================= NETWORK (NO String, ACK SAFE) ================= */
bool sendValve(IPAddress ip, const char* cmd) {
  WiFiClient c;
  c.setTimeout(150);

  if (!c.connect(ip, 80)) return false;

  char req[64];
  snprintf(req, sizeof(req),
           "GET /%s HTTP/1.1\r\nConnection: close\r\n\r\n",
           cmd);
  c.print(req);

  unsigned long t0 = millis();
  while (!c.available() && millis() - t0 < 120) {
    vTaskDelay(1);
  }

  bool ok = false;
  if (c.available()) {
    char line[32];
    c.readBytesUntil('\n', line, sizeof(line));
    if (strstr(line, "200")) ok = true;
  }

  c.stop();
  return ok;
}

/* ================= UI TASK (CORE 0) ================= */
void uiTask(void *pv) {
  bool stable = HIGH, last = HIGH;
  unsigned long debounce = 0, pressStart = 0;

  for (;;) {
    /* ----- MODE BUTTON ----- */
    bool r = digitalRead(MODE_BTN);
    if (r != last) debounce = millis();

    if (millis() - debounce > 40) {
      if (r != stable) {
        stable = r;
        if (stable == LOW) {
          pressStart = millis();
        } else {
          unsigned long d = millis() - pressStart;
          xSemaphoreTake(dataMutex, portMAX_DELAY);

          if (d < 500) currentMode = AUTO;
          else if (d < 1500) currentMode = NORMAL;
          else currentMode = OFF;

          prefs.putUChar("mode", currentMode);
          oledDirty = true;

          xSemaphoreGive(dataMutex);
        }
      }
    }
    last = r;

    /* ----- ENCODER ----- */
    if (encDelta) {
      xSemaphoreTake(dataMutex, portMAX_DELAY);
      threshold = constrain((int)threshold + encDelta, 0, 100);
      encDelta = 0;
      prefs.putUChar("thr", threshold);
      oledDirty = true;
      xSemaphoreGive(dataMutex);
    }

    /* ----- OLED ----- */
    if (oledDirty) {
      xSemaphoreTake(dataMutex, portMAX_DELAY);

      display.clearDisplay();
      display.setTextColor(SSD1306_WHITE);

      display.setCursor(0, 0);
      display.print("MODE:");
      display.print(currentMode == AUTO ? "AUTO" :
                    currentMode == NORMAL ? "NORM" : "OFF");

      display.setCursor(80, 0);
      display.print("TH:");
      display.print(threshold);

      display.setCursor(0, 12);
      display.print("WATER:");
      display.print(waterHigh ? "HIGH" : "LOW");

      display.drawLine(0, 22, 127, 22, SSD1306_WHITE);

      display.setCursor(0, 25);
      display.print("SOIL:");
      display.print(moistureText(soil));
      display.print(" (");
      display.print(soil);
      display.print(")");

      display.drawLine(0, 38, 127, 38, SSD1306_WHITE);

      display.setCursor(0, 41);
      display.print("IN :");
      display.print(inletOpen ? "ON " : "OFF");
      display.print(inletOnline ? " OK" : " --");

      display.setCursor(0, 52);
      display.print("OUT:");
      display.print(outletOpen ? "ON " : "OFF");
      display.print(outletOnline ? " OK" : " --");

      display.display();
      oledDirty = false;

      xSemaphoreGive(dataMutex);
    }

    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}

/* ================= CONTROL TASK (CORE 1) ================= */
void controlTask(void *pv) {
  unsigned long highStart = 0;
  bool lastIn = false, lastOut = false;

  for (;;) {
    xSemaphoreTake(dataMutex, portMAX_DELAY);

    soil = analogRead(SOIL_PIN);
    waterHigh = digitalRead(WATER_LEVEL_PIN);

    if (currentMode != OFF) {
      inletOpen = !waterHigh;

      if (waterHigh) {
        if (!highStart) highStart = millis();
        if (millis() - highStart > 3000) outletOpen = true;
      } else {
        highStart = 0;
        outletOpen = false;
      }
    } else {
      inletOpen = false;
      outletOpen = false;
    }

    digitalWrite(AUTO_LED, currentMode == AUTO);
    digitalWrite(NORMAL_LED, currentMode == NORMAL);

    bool sendIn = inletOpen != lastIn;
    bool sendOut = outletOpen != lastOut;

    lastIn = inletOpen;
    lastOut = outletOpen;

    xSemaphoreGive(dataMutex);

    if (sendIn) {
      inletOnline = sendValve(inletIP, inletOpen ? "open" : "close");
      if (inletOnline) lastInletSeen = millis();
    }

    if (sendOut) {
      outletOnline = sendValve(outletIP, outletOpen ? "open" : "close");
      if (outletOnline) lastOutletSeen = millis();
    }

    if (millis() - lastInletSeen > OFFLINE_TIMEOUT) inletOnline = false;
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
  Wire.setClock(100000);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);

  WiFi.softAP("WATER_SYSTEM", "12345678");
  WiFi.softAPConfig(
    IPAddress(192,168,4,1),
    IPAddress(192,168,4,1),
    IPAddress(255,255,255,0)
  );

  dataMutex = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(uiTask, "UI",   4096, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(controlTask, "CTRL", 4096, NULL, 1, NULL, 1);
}

void loop() {}
