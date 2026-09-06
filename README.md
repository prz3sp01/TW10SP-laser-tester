# TW10SP Laser Distance Sensor Tester

<img width="2884" height="1712" alt="Zrzut ekranu z 2026-09-06 08-45-14" src="https://github.com/user-attachments/assets/3fbb5aae-b445-441a-97f1-79f9df0f5492" />


### A desktop application written in pure C using the **Raylib** library. Designed for real-time monitoring, visualization, and data streaming of the **TW10SP** laser distance sensor via a serial port (UART / USB-UART).

---

## 🚀 Key Features

- **High-Speed ASCII Streaming:** Utilizes the sensor's continuous stream mode (`contis`) to achieve a fluid refresh rate of up to **6 Hz** without lagging or dropping frames.
- **Real-Time Trend Chart:** A rolling graphical chart displaying distance history over time.
- **Visual Distance Tracker:** An animated visual progress bar reflecting the target's position up to 40 meters.
- **Digital 7-Segment Display:** Large, industrial-style numerical readout in both millimeters and meters.
- **Multi-language Support:** Dropdown selector for **Polish (PL)**, **Nederlands (NL)**, and **English (EN)** with instant UI localization.
- **Audio Feedback:** Optional acoustic blip confirmation on successful sample reads.
- **Cross-Platform Compatibility:** Easily compiles for both **Linux** and **Windows** from a single C source file.

---

## 🛠️ Hardware & UART Specifications

To communicate properly with the sensor, ensure your hardware interface meets the following parameters:

- **Baud Rate:** 9600
- **Data Bits:** 8
- **Stop Bits:** 1
- **Parity:** None (`8N1`)
- **Logic Level:** TTL (3.3V ) — *requires a USB-UART bridge converter (e.g., CH340, CP2102, FTDI).*

### Wiring Guide:

| TW10SP Sensor | USB-UART Converter |
| --- | --- |
| **VCC** | 5V (or 3.3V depending on module revision) |
| **GND** | GND (Common Ground) |
| **TX** | RX (Receiver) |
| **RX** | TX (Transmitter) |

---

## 🎮 Compilation & Build Instructions

##### Linux:

```bash
gcc tw10sp_gui.c -o tw10sp_gui -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
```

##### Windows (Cross-compilation from Linux via MinGW):

```bash
x86_64-w64-mingw32-gcc tw10sp_gui.c -o tw10sp_gui.exe -lraylib -lopengl32 -lgdi32 -lwinmm
```

## 📄 License & Credits

Copyright (c) 2026 Karol "prz3sp01" Przespolewski
Contact: karol@przespol.eu
