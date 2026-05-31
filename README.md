# Sprinkler Control 🌧️

**Arduino Uno R4 WiFi** multi-zone smart sprinkler controller with local web UI and MQTT integration for Home Assistant.

First module of a larger home automation system.

## ✨ Features

- **6 controllable zones** (easily expandable)
- **Time-based scheduling** with real NTP sync
- **Local web dashboard** (Bootstrap 5 + Material Symbols)
- **Dynamic water drop animation** — activates automatically when a zone is running
- **Manual ON/OFF** with configurable auto-off duration
- **Full zone editing** (name, start time, duration, days of week, enable/disable)
- **Non-blocking scheduler** — web server stays responsive
- **MQTT support** for Home Assistant integration
- **Settings page** for time format and manual run duration

## 🛠️ Hardware

- Arduino Uno R4 WiFi
- 8-channel 5V relay module
- 12V DC solenoid valves (3/4" NPT recommended)
- 12V 2A+ power supply
- 1N4007 flyback diodes on each solenoid

## 📦 Software Requirements

- Arduino IDE
- Libraries:
  - `WiFiS3` (built-in)
  - `NTPClient`
  - `PubSubClient` (for MQTT)

## 🚀 Setup

1. Clone the repository
2. Copy `arduino_secrets_template.h` → `arduino_secrets.h`
3. Fill in your WiFi and MQTT credentials in `arduino_secrets.h`
4. Upload the sketch to your Uno R4 WiFi
5. Open the local web interface at the IP address shown in the Serial Monitor (optional fallback)

## 📡 MQTT Topics (Home Assistant)

```yaml
# Commands (Home Assistant sends these)
sprinkler/zone1/set      → "ON" or "OFF"
sprinkler/zone2/set      → "ON" or "OFF"
...

# Status (Arduino publishes these)
sprinkler/zone1/state    → "ON" or "OFF"
sprinkler/zone2/state    → "ON" or "OFF"

🏠 Home Assistant Integration
This controller is designed to work seamlessly with Home Assistant via MQTT. Once connected, your zones will appear as switches with real-time status.

📸 Screenshots
(Add screenshots of the dashboard, edit page, and settings page here)

🔮 Future Plans

Full MQTT Discovery support for automatic Home Assistant setup
Rain sensor / soil moisture integration
Additional home automation modules (lights, pumps, valves, etc.)
Centralized Home Assistant dashboard

📄 License
MIT License

Made as the first module of a larger home automation system.