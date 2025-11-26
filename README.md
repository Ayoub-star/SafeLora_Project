# SafeLoRa Project  
A secure vibration-alert system using LoRa (E220-900T22D) and an M5Stack Tab5 (ESP32-P4).

---

## Overview
SafeLoRa is a low-power wireless alert system designed to detect vibration events and send them securely to a monitoring tablet.  
It combines:

- **E220 LoRa module** at **868 MHz (EU SRD band)**
- **SW-420 vibration sensor**
- **M5Stack Tab5** for alert visualization
- Optional **MQTT(S) cloud integration** for remote alerts

The project focuses on reliability, real-time detection, and clean UI feedback.

---

## System Architecture
[ SW-420 Sensor ] → [ ESP32 Transmitter + E220 Module ]
↓ LoRa (868 MHz)
[ M5Stack Tab5 Receiver ] → Display, Buzzer, MQTT upload (optional)

Main components:
- **TX node**: reads vibration → encodes frame → sends via LoRa  
- **RX tablet**: decodes frame → displays alert → plays sound → logs history  
- **Cloud** (optional): sends strength/RSSI via MQTT over TLS

---

## 📡 LoRa Configuration (E220-900T22D)
- Frequency: **868.125 MHz** (CH18)
- Band: **EU SRD 868 MHz** – legal under *REC 70-03*
- Air rate: **2.4 kbps**
- Mode: **Fixed transmission**
- Addressing: **AddrH/AddrL** = device ID (0x00 to 0xFF)
- Payload format:
{
"vibration": 1,
"strength": XX,
"ts": YYYY
}

---

## 🖥 Tablet (Receiver) Logic
- UART task receives LoRa frames
- JSON parsing (lightweight)
- State machine:  
  - NORMAL  
  - ALERT  
  - NO_SIGNAL (timeout > 5s)
- Real-time UI: gauge, history, total events
- Buzzer alert for vibration detection
- Optional MQTT publish

---

## 🔧 Hardware Used
- **ESP32 transmitter board**  
- **E220-900T22D LoRa module**
- **SW-420 vibration sensor**
- **M5Stack Tab5 (ESP32-P4)**  
- Custom jumpers/cables  
- 5V battery pack (optional)

Datasheets are included in `hardware/`.

---

## Repository Structure
SafeLoRa_Project/
│
├── transmitter_code/ # ESP32 vibration sender
├── receiver_tablet_code/ # Tab5 UI + LoRa + MQTT
├── hardware/ # Schematic, wiring, pinouts, datasheets
├── lora_config/ # Extracted E220 settings + screenshots
├── tests/ # Validation logs, range tests, screenshots
└── README.md # Documentation


---

##  MQTT Secure Communication (Optional)
- Broker: HiveMQ Cloud or EMQX Cloud  
- Port: **8883 (TLS)**  
- Authentication: username/password  
- Transport: `WiFiClientSecure`  
- Certificate stored directly in firmware  

Payload example:
```json
{
  "vibration": 1,
  "strength": 87,
  "rssi": -72
}

## Compliance & Frequency Choice

The 868 MHz band is selected because:

Legal in the EU under CEPT REC 70-03

Allows 25 mW ERP, long-range, low power

Less crowded than 2.4 GHz

E220 modules fully support SRD-EU mode

## Test & Validation Summary

Vibration triggering accuracy

No-signal timeout behavior

LoRa range tests (open area vs. indoor)

RSSI monitoring + packet continuity

MQTT integration validation

Stress test: repeated events + UI refresh stability

Details available in tests/.

## License

This project is released under the MIT License.

 ## Contributing

Feel free to open issues or pull requests to improve UI, parsing logic, or hardware design.
