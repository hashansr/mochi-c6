# Dasai Mochi Waveshare (new project)

This is a fresh project workspace for building a Dasai Mochi-style multi-face animation app on a Waveshare ESP32 board.

- We are **not** reusing code from old local projects.
- Research notes: `RESEARCH.md`

## Hardware target
- Waveshare ESP32-C6-LCD-1.47 (172x320, ST7789)
- External touch sensor (digital I/O)
- Small speaker

## Current starter
- Firmware scaffold: `firmware/`
- Main app: `firmware/src/main.cpp`
- Notes and wiring: `firmware/README.md`
- Copied source references:
  - `third_party/huykhoong_*` (#1)
  - `third_party/upiir_dasai_mochi.ino` (#2)
