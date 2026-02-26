# Arduino IDE sketch

Sketch folder: `DasaiMochiC6/`

## Files
- `DasaiMochiC6.ino`
- `huykhoong_daichi_intro.h`
- `upiir_sample_frames.h`

## Arduino IDE setup
1. Install **esp32 by Espressif Systems** (v3.x+).
2. Select board: **ESP32C6 Dev Module**.
3. Install libraries:
   - Adafruit GFX Library
   - Adafruit ST7735 and ST7789 Library
4. Open `DasaiMochiC6.ino`, compile, upload.

## Hardware defaults in sketch
- LCD (Waveshare C6 LCD 1.47): GPIO6/7/14/15/21/22
- Touch module I/O: GPIO2
- Speaker: GPIO3

If your touch/speaker use different pins, edit `PIN_TOUCH` and `PIN_SPEAKER` in the sketch.
