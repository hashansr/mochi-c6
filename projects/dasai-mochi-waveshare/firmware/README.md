# Firmware starter (Waveshare ESP32-C6 LCD 1.47)

This starter merges ideas/code style from:
- **#1** `huykhoong/esp32_dasai_mochi_clone_and_how_to` (AnimatedGIF + `playGIF(...)` pattern)
- **#2** `upiir/esp32s3_oled_dasai_mochi` (frame-array loop pattern)

## Board pins used (from Waveshare wiki)
- LCD MOSI: GPIO6
- LCD SCLK: GPIO7
- LCD CS: GPIO14
- LCD DC: GPIO15
- LCD RST: GPIO21
- LCD BL: GPIO22

## External wiring (default in `src/main.cpp`)
- Touch module:
  - VCC -> 3V3
  - GND -> GND
  - I/O -> GPIO2
- Speaker:
  - + -> GPIO3
  - - -> GND

If you used different pins, change `PIN_TOUCH` / `PIN_SPEAKER` in `src/main.cpp`.

## Build/flash (PlatformIO)
```bash
cd projects/dasai-mochi-waveshare/firmware
pio run -t upload
pio device monitor
```

## Notes
- Display init is currently tuned as a first-pass starter (`Adafruit_ST7789`, 172x320).
- If image placement is offset on your unit, we’ll add ST7789 column/row offset handling next.
- `huykhoong` repo currently has no explicit license in metadata; treat copied files as reference until licensing is clarified.
