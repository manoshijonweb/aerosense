# AeroSense

A portable desk air-quality monitor built on an ESP32, with a live web dashboard.

It measures airborne particulates and combustible gas, shows the readings and a US EPA
air quality index on a small colour screen, and — optionally — mirrors everything to a
private Firebase database so it can be watched from anywhere.

**Dashboard:** https://manoshijonweb.github.io/aerosense/ (sign-in required)

## Hardware

| Part | Role | Connection |
|---|---|---|
| ESP32 DevKit V1 | controller | — |
| PMS5003 | PM1.0 / PM2.5 / PM10 | UART2, GPIO16 (RX) / GPIO17 (TX) |
| MQ-2 | combustible gas and smoke | ADC1, GPIO34 |
| 1.8" ST7735 TFT, 160x128 | display | VSPI; CS 5, DC 15, RST 2 |
| 2 x push button | navigation | GPIO25, GPIO26, to GND |

The full pin map, timings and thresholds live in one place: `firmware/src/Config.h`.

## Repository layout

```
index.html              the dashboard, served by GitHub Pages from the repo root
firmware/               PlatformIO project for the ESP32
  platformio.ini        board, libraries, partition scheme, upload settings
  src/                  one manager per concern; see the header comments
  FIREBASE_SETUP.md     console walkthrough for the cloud side
docs/                   the printed user guides
```

## Building the firmware

Needs [PlatformIO](https://platformio.org/).

```
cd firmware
copy src\Secrets.example.h src\Secrets.h    # then fill it in
pio run -t upload
pio device monitor
```

`src/Secrets.h` holds the Wi-Fi and Firebase credentials and is **not** in this
repository — `Secrets.example.h` is the template. With it left empty the firmware
builds and runs exactly as an offline monitor: no radio is started and nothing leaves
the device.

For the cloud side — creating the project, the database, the device account and the
security rules — follow `firmware/FIREBASE_SETUP.md`.

## Putting it on Wi-Fi

Wi-Fi can be baked into `Secrets.h`, but it does not have to be. On the device, hold
Button 2 for the Quick Menu and choose **Wi-Fi Setup**: the monitor raises its own
network, you join it from a phone, and a page lists the networks in range for you to
pick one and type its password on a real keyboard. The choice is remembered across
power cuts.

## Design notes

Nothing in the firmware blocks. Sensor reads are parsed as bytes arrive, the UI is a
throttled change-aware redraw, and the Firebase client — the one genuinely slow,
stack-hungry part — runs on its own FreeRTOS task pinned to core 0, while `loop()`
stays on core 1. The two sides share a single struct behind a spinlock.
