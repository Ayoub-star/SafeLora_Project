#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

// -----------------------------------------------------
//  LoRa / E220 pins
// -----------------------------------------------------
#define LORA_RX 38
#define LORA_TX 37
#define E220_SERIAL Serial2

// -----------------------------------------------------
//  Wi-Fi
// -----------------------------------------------------
const char* WIFI_SSID = "*****"; // AP name
const char* WIFI_PASS = "******"; // AP password


// -----------------------------------------------------
//  EMQX Cloud  (MQTT over TLS 8883)
// -----------------------------------------------------
const char ca_cert[] = R"EOF(
-----BEGIN CERTIFICATE-----
// Your Certificate
-----END CERTIFICATE-----
)EOF";

const char* MQTT_SERVER = "your mqtt server";
const int   MQTT_PORT   = 8883; // the port you re using
const char* MQTT_USER   = "esp32"; //username
const char* MQTT_PASSW  = "12345678"; // passeword
const char* MQTT_TOPIC  = "SafeLoRa/vibration"; // you topic

WiFiClientSecure espClient;
PubSubClient mqtt(espClient);

// -----------------------------------------------------
//  App state
// -----------------------------------------------------
static const uint32_t NO_SIGNAL_MS = 5000;

int lastRSSI     = -100;
int lastStrength = 0;
unsigned long lastMsgTime = 0;
bool gStopAlarm  = false;

// Events
struct Event {
  unsigned long ts;
  int strength;
  int rssi;
};
static const int MAX_EVENTS = 8;
Event events[MAX_EVENTS];
int   eventCount = 0;

// RSSI graph buffer
static const int MAX_RSSI_POINTS = 60;
int rssiBuf[MAX_RSSI_POINTS];
int rssiIndex = 0;
bool rssiFilled = false;

// Toast small popup
struct Toast {
  String text;
  uint32_t ts;
  uint32_t duration;
  bool active;
} toast = {"", 0, 0, false};

// UI positions
int stopBtnX, stopBtnY, stopBtnW, stopBtnH;
int themeBtnX, themeBtnY, themeBtnW, themeBtnH;

// Theme
bool darkTheme = true;

// Alert screen
bool alertActive = false;

// Non-blocking beep
bool beepActive = false;
uint8_t beepStage = 0;
uint32_t beepT0 = 0;

// -----------------------------------------------------
// Helpers
// -----------------------------------------------------
int readE220_RSSI_dBm() {
  uint8_t cmd[3] = {0xC1, 0x00, 0x00};
  uint8_t resp[4] = {0};
  while (E220_SERIAL.available()) E220_SERIAL.read();
  E220_SERIAL.write(cmd, 3);
  E220_SERIAL.flush();

  uint32_t t0 = millis();
  int idx = 0;
  while (millis() - t0 < 50) {
    if (E220_SERIAL.available() && idx < 4) {
      resp[idx++] = E220_SERIAL.read();
    }
  }
  if (idx < 4) return lastRSSI;
  return -(resp[3] / 2);
}

void addEvent(unsigned long ts, int strength, int rssi) {
  if (eventCount < MAX_EVENTS) {
    events[eventCount++] = {ts, strength, rssi};
  } else {
    for (int i = 1; i < MAX_EVENTS; ++i) events[i - 1] = events[i];
    events[MAX_EVENTS - 1] = {ts, strength, rssi};
  }
}

void addRSSI(int rssi) {
  rssiBuf[rssiIndex] = rssi;
  rssiIndex++;
  if (rssiIndex >= MAX_RSSI_POINTS) {
    rssiIndex = 0;
    rssiFilled = true;
  }
}

void showToast(const String& txt, uint32_t dur = 1500) {
  toast.text = txt;
  toast.duration = dur;
  toast.ts = millis();
  toast.active = true;
}

bool hasVibrationOne(const String& msg) {
  int i = msg.indexOf("\"vibration\"");
  if (i < 0) return false;
  i = msg.indexOf(':', i);
  while (i < msg.length() && (msg[i] == ':' || msg[i] == ' ')) i++;
  return i < msg.length() && msg[i] == '1';
}

int extractInt(const String& msg, const char* key, int defVal = 0) {
  String pat = String("\"") + key + "\":";
  int i = msg.indexOf(pat);
  if (i < 0) return defVal;
  i += pat.length();
  while (i < msg.length() && msg[i] == ' ') i++;

  long v = 0; bool ok = false;
  while (i < msg.length() && isDigit(msg[i])) {
    v = v * 10 + (msg[i] - '0');
    ok = true; i++;
  }
  return ok ? v : defVal;
}

// -----------------------------------------------------
// MQTT
// -----------------------------------------------------
void mqttReconnect() {
  while (!mqtt.connected()) {
    Serial.print("MQTT connect...");
    if (mqtt.connect("SafeLoRa_Tab5", MQTT_USER, MQTT_PASSW)) {
      Serial.println("OK");
      mqtt.publish("SafeLoRa/status", "Tab5 connected");
    } else {
      Serial.printf("FAILED, state=%d\n", mqtt.state());
      delay(1500);
    }
  }
}

// -----------------------------------------------------
// Theme helpers
// -----------------------------------------------------
uint16_t bgColor()        { return darkTheme ? TFT_BLACK : TFT_WHITE; }
uint16_t panelColor()     { return darkTheme ? TFT_DARKGREY : TFT_LIGHTGREY; }
uint16_t textPrimary()    { return darkTheme ? TFT_WHITE : TFT_BLACK; }
uint16_t textSecondary()  { return darkTheme ? TFT_CYAN : TFT_BLUE; }

// -----------------------------------------------------
// UI drawing
// -----------------------------------------------------
void drawTopBar() {
  auto &d = M5.Display;
  int w = d.width();

  d.fillRect(0, 0, w, 60, bgColor());
  d.setTextSize(3);
  d.setTextColor(textPrimary(), bgColor());
  d.setCursor(20, 15);
  d.print("SafeLoRa");

  // battery
  int batt = M5.Power.getBatteryLevel();
  int bx = w - 120;
  int by = 15;
  int bw = 80, bh = 28;
  d.drawRect(bx, by, bw, bh, textPrimary());
  int fillW = map(batt, 0, 100, 2, bw - 4);
  d.fillRect(bx + 2, by + 2, fillW, bh - 4, textSecondary());
  d.setTextSize(2);
  d.setCursor(bx - 10, by + 5);
  d.printf("%3d%%", batt);

  // theme button
  themeBtnW = 40; themeBtnH = 40;
  themeBtnX = w - themeBtnW - 10;
  themeBtnY = 10;
  d.fillRoundRect(themeBtnX, themeBtnY, themeBtnW, themeBtnH, 8,
                  darkTheme ? TFT_NAVY : TFT_YELLOW);
  d.setTextColor(darkTheme ? TFT_WHITE : TFT_BLACK);
  d.setTextSize(2);
  d.setCursor(themeBtnX + 10, themeBtnY + 10);
  d.print(darkTheme ? "☾" : "☼");
}

void drawSignalBars(int x, int y) {
  auto &d = M5.Display;
  int level;
  if (lastRSSI > -70) level = 3;
  else if (lastRSSI > -85) level = 2;
  else if (lastRSSI > -95) level = 1;
  else level = 0;

  uint16_t colors[4] = {TFT_RED, TFT_ORANGE, TFT_YELLOW, TFT_GREEN};

  for (int i = 0; i < 3; ++i) {
    int h = 10 + i * 7;
    uint16_t col = (i < level) ? colors[level] : (darkTheme ? TFT_DARKGREY : TFT_LIGHTGREY);
    d.fillRect(x + i * 7, y + (24 - h), 5, h, col);
  }
}

void drawDashboard() {
  auto &d = M5.Display;
  int w = d.width();
  int h = d.height();

  d.fillScreen(bgColor());
  drawTopBar();

  // main panel
  d.fillRoundRect(20, 70, w - 40, 170, 10, panelColor());

  d.setTextSize(3);
  d.setTextColor(textPrimary(), panelColor());
  d.setCursor(40, 90);
  bool sigOK = (millis() - lastMsgTime) <= NO_SIGNAL_MS;
  d.printf("Signal: %s", sigOK ? "OK" : "NO SIGNAL");

  uint16_t colRSSI =
      (lastRSSI > -70) ? TFT_GREEN :
      (lastRSSI > -85) ? TFT_YELLOW : TFT_RED;

  d.setCursor(40, 135);
  d.setTextColor(colRSSI, panelColor());
  d.printf("RSSI: %d dBm", lastRSSI);

  d.setTextColor(textPrimary(), panelColor());
  d.setCursor(40, 180);
  d.printf("Strength: %3d", lastStrength);

  drawSignalBars(w - 70, 90);

  d.setTextSize(2);
  d.setTextColor(textPrimary(), panelColor());
  d.setCursor(40, 215);
  d.printf("WiFi: %s", (WiFi.status() == WL_CONNECTED) ? "OK" : "KO");
  d.setCursor(40, 240);
  d.printf("MQTT: %s", (mqtt.connected()) ? "OK" : "KO");

  // gauge
  int cx = 90;
  int cy = h - 120;
  int r  = 60;
  d.fillCircle(cx, cy, r + 4, bgColor());
  d.fillCircle(cx, cy, r, TFT_DARKGREY);
  d.setTextColor(TFT_WHITE, TFT_DARKGREY);
  d.setTextSize(2);
  d.setCursor(cx - 30, cy - 10);
  d.print("VIB");

  // graph frame
  d.drawRect(170, 260, w - 190, h - 280, textPrimary());

  // history panel
  d.fillRoundRect(20, 260, 130, h - 280, 6, panelColor());
}

void drawGaugeNeedle() {
  auto &d = M5.Display;
  int cx = 90;
  int cy = d.height() - 120;
  int r  = 50;

  d.fillCircle(cx, cy, r + 2, TFT_DARKGREY);
  d.setTextColor(TFT_WHITE, TFT_DARKGREY);
  d.setTextSize(2);
  d.setCursor(cx - 30, cy - 10);
  d.print("VIB");

  float a = map(lastStrength, 0, 100, -120, 120) * DEG_TO_RAD;
  int x2 = cx + cos(a) * r;
  int y2 = cy + sin(a) * r;

  d.drawLine(cx, cy, x2, y2, TFT_RED);
}

void drawRSSIGraph() {
  auto &d = M5.Display;
  int x0 = 170;
  int y0 = 260;
  int gw = d.width() - 190;
  int gh = d.height() - 280;

  d.fillRect(x0 + 1, y0 + 1, gw - 2, gh - 2, bgColor());

  int count = rssiFilled ? MAX_RSSI_POINTS : rssiIndex;
  if (count < 2) return;

  int idx = rssiFilled ? rssiIndex : 0;
  int prevX = x0;
  int prevY = y0 + gh - map(rssiBuf[idx], -110, -40, 0, gh - 4);

  for (int i = 1; i < count; ++i) {
    idx = (idx + 1) % MAX_RSSI_POINTS;
    int x = x0 + map(i, 0, MAX_RSSI_POINTS - 1, 0, gw - 4);
    int y = y0 + gh - map(rssiBuf[idx], -110, -40, 0, gh - 4);
    d.drawLine(prevX, prevY, x, y, TFT_CYAN);
    prevX = x;
    prevY = y;
  }
}

void drawHistory() {
  auto &d = M5.Display;
  int x = 24;
  int y = 266;

  d.fillRoundRect(20, 260, 130, d.height() - 280, 6, panelColor());
  d.setTextColor(textPrimary(), panelColor());
  d.setTextSize(2);
  d.setCursor(x, y);
  d.print("History");

  y += 18;
  d.setTextSize(1);
  for (int i = 0; i < eventCount; ++i) {
    const Event &e = events[eventCount - 1 - i];  // last first
    d.setCursor(x, y + i * 12);
    d.printf("%d) S:%3d R:%3d", i + 1, e.strength, e.rssi);
  }
}

void drawStopButton() {
  auto &d = M5.Display;
  stopBtnW = d.width() - 60;
  stopBtnH = 70;
  stopBtnX = 30;
  stopBtnY = d.height() - stopBtnH - 10;

  d.fillRoundRect(stopBtnX, stopBtnY, stopBtnW, stopBtnH, 12, TFT_RED);
  d.drawRoundRect(stopBtnX, stopBtnY, stopBtnW, stopBtnH, 12, TFT_WHITE);

  d.setTextSize(3);
  d.setTextColor(TFT_WHITE, TFT_RED);
  d.setCursor(stopBtnX + 40, stopBtnY + 20);
  d.print("STOP ALERT");
}

void drawToast() {
  if (!toast.active) return;
  if (millis() - toast.ts > toast.duration) {
    toast.active = false;
    return;
  }

  auto &d = M5.Display;
  int w = d.width();
  int h = 40;
  int x = 20;
  int y = d.height() - h - 90;

  d.fillRoundRect(x, y, w - 40, h, 8, TFT_DARKGREY);
  d.setTextColor(TFT_WHITE, TFT_DARKGREY);
  d.setTextSize(2);
  d.setCursor(x + 10, y + 10);
  d.print(toast.text);
}

void drawAlertScreen() {
  auto &d = M5.Display;
  d.fillScreen(TFT_RED);
  d.setTextColor(TFT_WHITE, TFT_RED);
  d.setTextSize(5);
  d.setCursor(40, 60);
  d.print("ALERTE !");
  d.setTextSize(3);
  d.setCursor(40, 140);
  d.printf("Strength: %d", lastStrength);
  d.setCursor(40, 190);
  d.printf("RSSI: %d dBm", lastRSSI);
  drawStopButton();
}

// -----------------------------------------------------
//  Beep non bloquant
// -----------------------------------------------------
void startAlertBeep() {
  if (gStopAlarm) return;
  beepActive = true;
  beepStage = 0;
  beepT0 = millis();
  M5.Speaker.setVolume(180);
  M5.Speaker.tone(700);
}

void updateBeep() {
  if (!beepActive || gStopAlarm) {
    M5.Speaker.stop();
    beepActive = false;
    return;
  }
  uint32_t now = millis();
  if (beepStage == 0 && now - beepT0 > 220) {
    M5.Speaker.tone(2200);
    beepStage = 1;
    beepT0 = now;
  } else if (beepStage == 1 && now - beepT0 > 350) {
    M5.Speaker.stop();
    beepActive = false;
  }
}

// -----------------------------------------------------
//  Touch input
// -----------------------------------------------------
void handleTouch() {
  auto t = M5.Touch.getDetail();
  if (!t.isPressed()) return;

  // theme button
  if (t.x >= themeBtnX && t.x <= themeBtnX + themeBtnW &&
      t.y >= themeBtnY && t.y <= themeBtnY + themeBtnH) {
    darkTheme = !darkTheme;
    // force redraw
    drawDashboard();
    drawGaugeNeedle();
    drawRSSIGraph();
    drawHistory();
    drawStopButton();
    return;
  }

  // stop alert
  if (t.x >= stopBtnX && t.x <= stopBtnX + stopBtnW &&
      t.y >= stopBtnY && t.y <= stopBtnY + stopBtnH) {
    gStopAlarm = true;
    alertActive = false;
    beepActive = false;
    M5.Speaker.stop();
    drawDashboard();
    drawGaugeNeedle();
    drawRSSIGraph();
    drawHistory();
    drawStopButton();
  }
}

// -----------------------------------------------------
//  Setup
// -----------------------------------------------------
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);
  M5.Display.fillScreen(bgColor());

  Serial.begin(115200);
  E220_SERIAL.begin(9600, SERIAL_8N1, LORA_RX, LORA_TX);

  // WiFi SDIO pins Tab5
  WiFi.setPins(GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_11, GPIO_NUM_10,
               GPIO_NUM_9,  GPIO_NUM_8,  GPIO_NUM_15);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(200);
    Serial.print(".");
  }
  Serial.println();

  espClient.setCACert(ca_cert);
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);

  if (WiFi.status() == WL_CONNECTED) mqttReconnect();

  lastMsgTime = millis();
  drawDashboard();
  drawGaugeNeedle();
  drawRSSIGraph();
  drawHistory();
  drawStopButton();
}

// -----------------------------------------------------
//  Loop
// -----------------------------------------------------
void loop() {
  if (WiFi.status() == WL_CONNECTED && !mqtt.connected()) mqttReconnect();
  if (mqtt.connected()) mqtt.loop();

  // Lecture LoRa
  if (E220_SERIAL.available()) {
    String msg;
    while (E220_SERIAL.available()) {
      msg += char(E220_SERIAL.read());
      delay(2);
    }
    msg.trim();

    lastMsgTime = millis();
    lastRSSI = readE220_RSSI_dBm();
    addRSSI(lastRSSI);

    bool vib = hasVibrationOne(msg);
    lastStrength = extractInt(msg, "strength", 0);
    addEvent(millis(), lastStrength, lastRSSI);

    if (vib && !gStopAlarm) {
      alertActive = true;
      startAlertBeep();

      if (mqtt.connected()) {
        String payload = "{\"vibration\":1,\"strength\":" +
                         String(lastStrength) + ",\"rssi\":" +
                         String(lastRSSI) + "}";
        bool ok = mqtt.publish(MQTT_TOPIC, payload.c_str());
        Serial.printf("MQTT publish: %d (state=%d)\n", ok, mqtt.state());
      }

      showToast("Vibration " + String(lastStrength), 1800);
    }
  }

  // UI drawing (limité en fréquence)
  static uint32_t lastMainUI = 0;
  static uint32_t lastAlertUI = 0;

  if (alertActive) {
    if (millis() - lastAlertUI > 150) {
      drawAlertScreen();
      lastAlertUI = millis();
    }
  } else {
    if (millis() - lastMainUI > 200) {
      drawDashboard();
      drawGaugeNeedle();
      drawRSSIGraph();
      drawHistory();
      drawStopButton();
      lastMainUI = millis();
    }
  }

  drawToast();
  updateBeep();
  handleTouch();
  M5.update();
}
