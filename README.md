# Bird-Box 🐦🔊

An ESP32-based project to play bird songs through a speaker when someone enters the room.
Powered by **ESP32**, **MAX98357A I²S DAC/amp**, and a small 16 ohms mono speaker.

## Features

- Playback of **WAV audio** (16-bit PCM, mono, 44.1 kHz)
- Uses **ESP-IDF** + FreeRTOS task for streaming
- **I²S output** to MAX98357A amplifier
- Partition table with flash storage for audio data
- Optional Git version embedding in firmware

---

## Hardware
- **ESP32-WROOM module** (DevKit or bare module)
- **MAX98357A I²S DAC + Class-D amplifier**
- **Speaker**: 16 Ω (small ceiling/wall speaker)
- **Power**: 230 V AC → 12 V LED driver → LM2596 buck → 5 V for ESP32 & MAX98357A

### Wiring (default)
| ESP32 Pin | Signal | MAX98357A Pin |
|-----------|--------|---------------|
| GPIO25    | BCLK   | BCLK          |
| GPIO22    | LRCLK  | LRC           |
| GPIO26    | DATA   | DIN           |
| 5 V       | VCC    | VIN           |
| GND       | GND    | GND           |
| Speaker+ / Speaker− | Speaker output | OUT+ / OUT− |

---

## Repo Structure
bird-box/
├── flash-data/ # Audio/data assets stored in flash
├── main/ # Firmware source (app_main, I²S, tasks)
├── partitions.csv # Custom partition table
├── sdkconfig # Current ESP-IDF config
├── sdkconfig.defaults # Default ESP-IDF config
├── CMakeLists.txt # Top-level build config
└── dependencies.lock # IDF dependencies

## Build & Flash
```bash
# Activate ESP-IDF environment
. $HOME/.config/esp/esp-idf/export.sh

# Configure (optional)
idf.py menuconfig

# Build
idf.py build

# Flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor
```
