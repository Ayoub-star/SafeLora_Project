#include <Arduino.h>
#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#define LORA_RX 38
#define LORA_TX 37
#define E220_SERIAL Serial2

// Wi-Fi
const char* WIFI_SSID = "*****"; // AP name
const char* WIFI_PASS = "******"; // AP password

// EMQX Cloud
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

// state
int lastRSSI = -100;
int lastStrength = 0;
unsigned long lastMsgTime = 0;
bool alertActive = false;
bool stopAlert = false;
uint32_t alertStart = 0;

const uint32_t ALERT_SCREEN_MS = 3000;
const uint32_t NO_SIGNAL_MS = 5000;

// ========== E220 RSSI ==========
int readE220_RSSI() {
  uint8_t cmd[3] = {0xC1, 0x00, 0x00};
  uint8_t resp[4] = {0};
  while (E220_SERIAL.available()) E220_SERIAL.read();
  E220_SERIAL.write(cmd, 3);
  E220_SERIAL.flush();
  uint32_t t0 = millis();
  int i = 0;
  while (millis()-t0 < 50 && i < 4) {
    if (E220_SERIAL.available()) resp[i++] = E220_SERIAL.read();
  }
  if (i < 4) return lastRSSI;
  return -(resp[3] / 2);
}

// ========== MQTT Reconnect ==========
void mqttReconnect() {
  while (!mqtt.connected()) {
    mqtt.connect("Tab5Client", MQTT_USER, MQTT_PASSW);
    delay(500);
  }
}

// ========== UI Minimal ==========
void drawNormalScreen() {
  auto &d = M5.Display;

  d.fillScreen(TFT_BLACK);
  d.setTextColor(TFT_WHITE);
  d.setTextSize(3);

  d.setCursor(20, 20);
  d.print("SafeLoRa Minimal");

  // signal
  bool ok = (millis() - lastMsgTime) < NO_SIGNAL_MS;
  d.setCursor(20, 80);
  d.printf("Signal: %s", ok ? "OK" : "NO SIGNAL");

  d.setCursor(20, 130);
  d.printf("RSSI: %d dBm", lastRSSI);

  d.setCursor(20, 180);
  d.printf("Strength: %d", lastStrength);

  // stop button
  d.fillRoundRect(20, 240, d.width()-40, 70, 8, TFT_RED);
  d.setCursor(60, 265);
  d.setTextColor(TFT_WHITE);
  d.print("STOP ALERT");
}

void drawAlertScreen() {
  auto &d = M5.Display;
  d.fillScreen(TFT_RED);

  d.setTextColor(TFT_WHITE);
  d.setTextSize(5);
  d.setCursor(50, 80);
  d.print("ALERTE !");

  d.setTextSize(3);
  d.setCursor(50, 170);
  d.printf("Strength: %d", lastStrength);

  d.setCursor(50, 220);
  d.printf("RSSI: %d dBm", lastRSSI);
}

// ========== SETUP ==========
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);

  Serial.begin(115200);
  E220_SERIAL.begin(9600, SERIAL_8N1, LORA_RX, LORA_TX);

  WiFi.setPins(GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_11, GPIO_NUM_10,
               GPIO_NUM_9, GPIO_NUM_8, GPIO_NUM_15);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) delay(200);

  espClient.setCACert(ca_cert);  
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);

  mqttReconnect();
  drawNormalScreen();
}

// ========== LOOP ==========
void loop() {
  if (!mqtt.connected()) mqttReconnect();
  mqtt.loop();

  // read LoRa
  if (E220_SERIAL.available()) {
    String msg;
    while (E220_SERIAL.available()) msg += char(E220_SERIAL.read());
    msg.trim();

    lastMsgTime = millis();
    lastRSSI = readE220_RSSI();
    lastStrength = 0;

    int s = msg.indexOf("strength");
    if (s >= 0) {
      int colon = msg.indexOf(":", s);
      lastStrength = msg.substring(colon+1).toInt();
    }

    // ALERT
    if (msg.indexOf("vibration") >= 0 && msg.indexOf("1") >= 0) {
      alertActive = true;
      alertStart = millis();

      M5.Speaker.setVolume(180);
      M5.Speaker.tone(1000);
      delay(200);
      M5.Speaker.stop();

      // safe publish
      static uint32_t lastPub = 0;
      if (millis() - lastPub > 300) {
        lastPub = millis();
        String p = "{\"vibration\":1,\"strength\":"+String(lastStrength)+
                   ",\"rssi\":"+String(lastRSSI)+"}";
        mqtt.publish(MQTT_TOPIC, p.c_str());
      }
    }

    drawNormalScreen();
  }

  // Alert mode
  if (alertActive) {
    drawAlertScreen();

    // stop auto
    if (millis() - alertStart > ALERT_SCREEN_MS) {
      alertActive = false;
      drawNormalScreen();
    }

    // stop with touch
    auto t = M5.Touch.getDetail();
    if (t.isPressed() && t.y > 240) {
      alertActive = false;
      M5.Speaker.stop();
      drawNormalScreen();
    }
  }

  M5.update();
}
