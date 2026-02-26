# mochi-c6

Dasai Mochi-style faces for **Waveshare ESP32-C6-LCD-1.47** with:
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
Already configured in sketch:
- MOSI GPIO6
- SCLK GPIO7
- CS GPIO14
- DC GPIO15
- RST GPIO21
- BL GPIO22

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

## 6) If display looks wrong

Common fixes in sketch:
- `tft.setRotation(0..3)`
- color order init (if needed): `tft.init(172, 320);`
- small x/y offsets (panel-dependent) by shifting draw position in `drawMonoFrame(...)`

---

## 7) “Complete the INO” checklist

- [ ] Set WiFi SSID/PASS for real clock
- [ ] Confirm touch polarity (`TOUCH_ACTIVE_HIGH`)
- [ ] Confirm speaker pin / volume (use transistor if needed)
- [ ] Tune expression hold times / animation speeds
- [ ] Replace sample faces with your own converted assets

