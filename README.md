# Bird-Box 🐦🔊

An ESP32-powered ambient audio box that streams birds and nature sounds to a ceiling speaker — perfect for bathrooms, hallways, or quiet spaces.

Bird-Box automatically plays audio streams when triggered (e.g. via MQTT/Home Assistant), creating a subtle and immersive nature atmosphere.

Powered by **ESP32**, **MAX98357A I²S DAC/amp**, and a compact 16 Ω mono speaker.

## ✨ Features

- Built with **ESP-IDF**
- Streams MP3 over HTTP
- Optimized for **96 kbps MP3 (44.1 kHz, mono)**
- Real-time MP3 decoding on ESP32
- I²S digital audio output
- MQTT integration:
  - Play / Pause
  - Volume control
  - Device status
  - Firmware version reporting
- Home Assistant compatible
- Clean task-based architecture (stream / decode / audio separation)

## 🧠 System Architecture

HTTP Stream → Ringbuffer → MP3 Decoder → PCM → I²S → MAX98357A → Speaker

- Dedicated FreeRTOS tasks
- Ringbuffer-based audio pipeline
- Supervisor-controlled start/stop handling
- Designed to avoid pops, crackles, and race conditions

## 🛠 Hardware

- **MCU**: ESP32-WROOM module
- **DAC + Amplifier**: MAX98357A (I²S Class-D mono amp)
- **Speaker**: 16 Ω ceiling or wall speaker
- **Power Supply**:
  - 230V AC → 5V DC (Hi-Link module)
  - 5V → 3.3V via buck converter

Designed to fit inside a wall or ceiling cavity for a clean invisible install.

## 🙌 Sponsor

Huge thanks to **PCBWay** for sponsoring the PCB manufacturing for this project.
Their support, help and advices helped make this project physically real.

If you're looking for high-quality PCB fabrication or assembly services, check them out:

👉 https://www.pcbway.com

![](./docs/pcbway_logo.png)

## 🖼 Schematics & PCB

![](./docs/schematics.jpg)
![](./docs/birdbox_general.jpg)
![](./docs/birdbox_pcb.jpg)
![](./docs/birdbox_pcb_raw.jpg)

## 🌐 HTTP Stream Server

A simple Python HTTP server is included for testing.

```bash
cd server
python server_http.py
```

You can stream local MP3 files to the device over your LAN.

## Build and flash

# Activate ESP-IDF environment
```bash
. $HOME/.config/esp/esp-idf/export.sh
```

# Configure
```bash
idf.py menuconfig
```

# Build
```bash
idf.py build
```

# Flash and monitor
```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

*Made with ☕ and too many birds.*
