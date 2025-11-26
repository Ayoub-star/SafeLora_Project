/************************************************************
 *  SafeLoRa – ESP32 TX (Vibration Sensor + LoRa E220)
 *  - Capteur SW-420 filtré
 *  - Intensité lissée 0–100
 *  - Envoi immédiat JSON enrichi via LoRa (Fixed mode)
 ************************************************************/

#include <Arduino.h>

#define VIB_PIN 13       // D13
#define LORA_RX 16       // E220 TX -> ESP32 RX
#define LORA_TX 17       // E220 RX <- ESP32 TX

// Fixed addressing for E220 (to send to Tab5 addr 0x0002, CH 18)
const uint8_t TARGET_H = 0x00;
const uint8_t TARGET_L = 0x02;
const uint8_t CH = 0x12;   // Channel 18 (0x12)

const int TRIGGER_COUNT = 5;
const int DEBOUNCE_MS = 300;

unsigned long lastTrigger = 0;
int consecutiveHigh = 0;

int vibrationCount = 0;
float intensity = 0.0;        // smoothed intensity score

void sendLoRa(const String &payload) {
  Serial2.write(TARGET_H);
  Serial2.write(TARGET_L);
  Serial2.write(CH);
  Serial2.write((uint8_t*)payload.c_str(), payload.length());
  
  Serial.print("📤 Sent LoRa: ");
  Serial.println(payload);
}

void setup() {
  Serial.begin(115200);
  pinMode(VIB_PIN, INPUT);

  Serial2.begin(9600, SERIAL_8N1, LORA_RX, LORA_TX);

  Serial.println("\n--- SafeLoRa TX Ready (Vibration Sensor) ---");
  Serial.println("Monitoring...");
}

void loop() {
  int reading = digitalRead(VIB_PIN);

  if (reading == HIGH) {
    consecutiveHigh++;
  } else {
    consecutiveHigh = 0;
  }

  if (consecutiveHigh >= TRIGGER_COUNT && (millis() - lastTrigger > DEBOUNCE_MS)) {
    lastTrigger = millis();
    vibrationCount++;

    // Update intensity (smooth 0–100)
    intensity = 0.7 * intensity + 0.3 * 100;

    // Build JSON
    String json = "{";
    json += "\"vibration\":1";
    json += ",\"count\":" + String(vibrationCount);
    json += ",\"strength\":" + String((int)intensity);
    json += ",\"ts\":" + String(millis());
    json += "}";

    sendLoRa(json);
  }

  // Natural decay of intensity (if no new vibration)
  if (millis() - lastTrigger > 800 && intensity > 0) {
    intensity -= 5;
    if (intensity < 0) intensity = 0;
  }

  delay(10);
}
