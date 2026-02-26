# mochi-c6

Dasai Mochi-style faces for **Waveshare ESP32-C6-LCD-1.47** with:
(Aligned to Waveshare wiki pin map/offset guidance: https://www.waveshare.com/wiki/ESP32-C6-LCD-1.47)
- expression state machine (`IDLE`, `HAPPY`, `ANGRY`, `SLEEPY`)
- touch gestures (single tap / double tap / long press)
- per-gesture sound profiles
- clock mode toggle on long press

## Repo layout

- `DasaiMochiC6/DasaiMochiC6.ino` → main Arduino sketch
- `DasaiMochiC6/huykhoong_daichi_intro.h` → animation header source (#1)
- `DasaiMochiC6/upiir_sample_frames.h` → sample frame set (#2)
- `THIRD_PARTY_NOTICES.md` → source/license notes
- `docs/RESEARCH.md` → research notes

---

## 1) Arduino IDE setup

1. Install **Arduino IDE**.
2. Install board package: **esp32 by Espressif Systems** (v3.x+).
3. Board selection: **ESP32C6 Dev Module**.
4. Install libraries:
   - **Adafruit GFX Library**
   - **Adafruit ST7735 and ST7789 Library**

---

## 2) Wiring

### LCD (Waveshare ESP32-C6-LCD-1.47 onboard)
Configured to match Waveshare wiki demo:
- MISO GPIO5
- MOSI GPIO6
- SCLK GPIO7
- CS GPIO14
- DC GPIO15
- RST GPIO21
- BL GPIO22
- panel offsets: `X=34`, `Y=0`

### External modules
Default pins in sketch:
- Touch sensor I/O → `GPIO2`
- Speaker signal → `GPIO3`

If different, edit these in `DasaiMochiC6.ino`:
- `PIN_TOUCH`
- `PIN_SPEAKER`
- `TOUCH_ACTIVE_HIGH` (set `false` if your sensor is active-low)

---

## 3) Configure clock mode (optional but recommended)

At top of `DasaiMochiC6.ino` set:

```cpp
static const char* WIFI_SSID = "your-wifi";
static const char* WIFI_PASS = "your-pass";
```

If left empty, clock mode still works but shows uptime-based time instead of NTP time.

---

## 4) Compile and upload

1. Open `DasaiMochiC6/DasaiMochiC6.ino` in Arduino IDE.
2. Select the right COM port.
3. Click **Upload**.
4. Open Serial Monitor at `115200` (optional for logs).

---

## 5) Controls

- **Single tap** → cycle expressions (`IDLE -> HAPPY -> SLEEPY -> IDLE`)
- **Double tap** → `ANGRY`
- **Long press** → toggle **Clock mode** on/off

---

## 6) If display looks wrong (vertical lines / bad render)

The sketch now runs an automatic **SELF TEST** screen on boot.
If this self-test already shows lines/artifacts, it is driver/panel setup (not animation code).

Try these in `DasaiMochiC6.ino`:
- `LCD_SPI_HZ` → `8000000`, `10000000`, `20000000`, `40000000`, `80000000`
- `LCD_COL_OFFSET` / `LCD_ROW_OFFSET` (panel offset tuning)
- `LCD_ROTATION` (`0..3`)
- `LCD_INVERT` (`true/false`)

Note: idle frames are XBM bitmaps and are converted to proper bit order in code (to avoid stripe artifacts).

Important board note:
- This sketch targets **ESP32-C6-LCD-1.47 (non-touch, ST7789)**.
- If your board is **ESP32-C6-Touch-LCD-1.47 (JD9853)**, this ST7789 sketch will not render correctly and needs a JD9853 driver path.

---

## 7) “Complete the INO” checklist

- [ ] Set WiFi SSID/PASS for real clock
- [ ] Confirm touch polarity (`TOUCH_ACTIVE_HIGH`)
- [ ] Confirm speaker pin / volume (use transistor if needed)
- [ ] Tune expression hold times / animation speeds
- [ ] Replace sample faces with your own converted assets

