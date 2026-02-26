// Forward declarations to avoid Arduino auto-prototype issues with custom types.
enum class Expression : unsigned char;
enum class TouchEvent : unsigned char;
struct ExpressionConfig;
struct NoteStep;

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#include "upiir_sample_frames.h"      // from #2 (upiir) subset
#include "huykhoong_daichi_intro.h"  // from #1 (huykhoong)

// ==============================
// WiFi/NTP (optional)
// ==============================
// Leave empty to skip NTP sync (clock mode will show uptime time).
static const char* WIFI_SSID = "";
static const char* WIFI_PASS = "";
static constexpr long UTC_OFFSET_SEC = 0;
static constexpr int  DST_OFFSET_SEC = 0;

// ==============================
// Waveshare ESP32-C6-Touch-LCD-1.47 (wiki/demo aligned)
// Display chip: JD9853, Touch chip: AXS5106L
// ==============================
static constexpr uint8_t PIN_LCD_MOSI = 2;
static constexpr uint8_t PIN_LCD_SCLK = 1;
static constexpr int8_t  PIN_LCD_MISO = -1;
static constexpr uint8_t PIN_LCD_CS   = 14;
static constexpr uint8_t PIN_LCD_DC   = 15;
static constexpr uint8_t PIN_LCD_RST  = 22;
static constexpr uint8_t PIN_LCD_BL   = 23;

// Built-in touch (AXS5106L)
static constexpr uint8_t PIN_TOUCH_SDA = 18;
static constexpr uint8_t PIN_TOUCH_SCL = 19;
static constexpr uint8_t PIN_TOUCH_RST = 20;
static constexpr uint8_t PIN_TOUCH_INT = 21;
static constexpr uint8_t TOUCH_I2C_ADDR = 0x63;

// Onboard TF card CS (touch board uses shared SPI pins too)
static constexpr uint8_t PIN_SD_CS = 4;

// External modules
static constexpr uint8_t PIN_SPEAKER = 3; // small speaker

static constexpr uint16_t LCD_WIDTH = 172;
static constexpr uint16_t LCD_HEIGHT = 320;

// Touch timings
static constexpr uint32_t TOUCH_DEBOUNCE_MS = 30;
static constexpr uint32_t DOUBLE_TAP_GAP_MS = 280;
static constexpr uint32_t LONG_PRESS_MS = 700;

// Touch 1.47 demo uses col/row offsets 34/0 and custom JD9853 init commands.
// SPI can go higher; keep moderate default first for stability.
static constexpr uint32_t LCD_SPI_HZ = 40000000;
static constexpr int8_t LCD_COL_OFFSET = 34;
static constexpr int8_t LCD_ROW_OFFSET = 0;
static constexpr uint8_t LCD_ROTATION = 0;
static constexpr bool LCD_INVERT = true; // wiki init sends command 0x21 (invert on)

// Backlight PWM (wiki-style)
static constexpr uint16_t BK_PWM_FREQ = 1000;
static constexpr uint8_t BK_PWM_RES_BITS = 10;
static constexpr uint8_t BK_BRIGHTNESS_PERCENT = 100; // 0..100

class WaveshareST7789 : public Adafruit_ST7789 {
public:
  WaveshareST7789(SPIClass *spiClass, int8_t cs, int8_t dc, int8_t rst)
      : Adafruit_ST7789(spiClass, cs, dc, rst) {}

  void applyPanelOffset(int8_t col, int8_t row) { setColRowStart(col, row); }
};

WaveshareST7789 tft(&SPI, PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST);

struct TouchPoint {
  uint16_t x;
  uint16_t y;
};

volatile bool gTouchIntFlag = false;

void IRAM_ATTR onTouchInt() {
  gTouchIntFlag = true;
}

struct MonoAnimation {
  uint16_t frameCount;
  uint16_t width;
  uint16_t height;
  const uint16_t* delays;
  const uint8_t* const* frames;
};

const MonoAnimation kIdleAnimation = {
  static_cast<uint16_t>(sizeof(kUpiirFrames) / sizeof(kUpiirFrames[0])),
  128,
  64,
  kUpiirDelays,
  kUpiirFrames,
};

enum class AppMode : uint8_t {
  Faces,
  Clock
};

enum class Expression : uint8_t {
  Idle,
  Happy,
  Angry,
  Sleepy
};

enum class TouchEvent : uint8_t {
  None,
  SingleTap,
  DoubleTap,
  LongPress
};

struct ExpressionConfig {
  Expression id;
  const char* label;
  bool useGif;            // true -> daichi_intro_gif, false -> kIdleAnimation
  uint16_t color;
  uint16_t delayPercent;  // 100 = original speed
  uint16_t holdMs;        // 0 = stay until changed
};

// Tiny sound engine
struct NoteStep {
  uint16_t freq;
  uint16_t durationMs;
  uint16_t gapMs;
};

static constexpr uint8_t SOUND_QUEUE_MAX = 8;
NoteStep gSoundQueue[SOUND_QUEUE_MAX];
uint8_t gSoundCount = 0;
uint8_t gSoundIndex = 0;
uint32_t gSoundStepEndsAt = 0;
bool gSoundPlaying = false;
bool gSoundInGap = false;

const NoteStep SND_STARTUP[] = {{1600, 60, 25}, {2100, 70, 0}};
const NoteStep SND_SINGLE[]  = {{1900, 45, 18}, {2400, 55, 0}};
const NoteStep SND_DOUBLE[]  = {{2400, 40, 15}, {1800, 40, 15}, {2600, 55, 0}};
const NoteStep SND_LONG[]    = {{900, 90, 20}, {1400, 120, 0}};

// App state
AppMode gMode = AppMode::Faces;
Expression gExpression = Expression::Idle;
uint32_t gExpressionSetAt = 0;

uint16_t gMonoFrameIndex = 0;
uint16_t gGifFrameIndex = 0;
uint32_t gNextFrameAt = 0;

bool gTimeSynced = false;
uint32_t gLastClockDrawMs = 0;

// Touch tracking
bool gTouchStable = false;
bool gTouchLastRaw = false;
uint32_t gTouchLastChangeMs = 0;
uint32_t gTouchPressStartMs = 0;
bool gTouchLongFired = false;
uint8_t gTapCount = 0;
uint32_t gTapDeadlineMs = 0;

uint16_t safeDelayScaled(uint16_t baseMs, uint16_t percent) {
  uint32_t scaled = (static_cast<uint32_t>(baseMs) * percent) / 100;
  if (scaled < 20) scaled = 20;
  if (scaled > 2000) scaled = 2000;
  return static_cast<uint16_t>(scaled);
}

static inline uint8_t reverseBits8(uint8_t b) {
  b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
  b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
  b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
  return b;
}

void drawMonoFrameMSB(const uint8_t* frame, uint16_t width, uint16_t height, uint16_t color) {
  const int16_t x = (static_cast<int16_t>(tft.width()) - static_cast<int16_t>(width)) / 2;
  const int16_t y = (static_cast<int16_t>(tft.height()) - static_cast<int16_t>(height)) / 2;

  tft.fillScreen(ST77XX_BLACK);
  tft.drawBitmap(x, y, frame, width, height, color);
}

void drawMonoFrameXBM(const uint8_t* frame, uint16_t width, uint16_t height, uint16_t color) {
  // upiir frames are XBM bit order (LSB first per byte).
  // Adafruit drawBitmap expects MSB first, so convert once per frame.
  static uint8_t converted[128 * 64 / 8]; // 1024 bytes for 128x64
  const uint16_t bytes = (width * height) / 8;
  for (uint16_t i = 0; i < bytes; i++) {
    converted[i] = reverseBits8(frame[i]);
  }

  drawMonoFrameMSB(converted, width, height, color);
}

void drawCenteredText(const String& text, int16_t y, uint8_t size, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color);

  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(text.c_str(), 0, y, &x1, &y1, &w, &h);
  int16_t x = (static_cast<int16_t>(tft.width()) - static_cast<int16_t>(w)) / 2;
  tft.setCursor(x, y);
  tft.print(text);
}

void jd9853InitByWaveshareSequence() {
  auto send = [](uint8_t cmd, const uint8_t* data, uint8_t len) {
    tft.sendCommand(cmd, data, len);
  };

  tft.sendCommand(0x11); // sleep out
  delay(120);

  { const uint8_t d[] = {0x98, 0x53}; send(0xDF, d, sizeof(d)); }
  { const uint8_t d[] = {0x23}; send(0xB2, d, sizeof(d)); }
  { const uint8_t d[] = {0x00,0x47,0x00,0x6F}; send(0xB7, d, sizeof(d)); }
  { const uint8_t d[] = {0x1C,0x1A,0x55,0x73,0x63,0xF0}; send(0xBB, d, sizeof(d)); }
  { const uint8_t d[] = {0x44,0xA4}; send(0xC0, d, sizeof(d)); }
  { const uint8_t d[] = {0x16}; send(0xC1, d, sizeof(d)); }
  { const uint8_t d[] = {0x7D,0x07,0x14,0x06,0xCF,0x71,0x72,0x77}; send(0xC3, d, sizeof(d)); }
  { const uint8_t d[] = {0x00,0x00,0xA0,0x79,0x0B,0x0A,0x16,0x79,0x0B,0x0A,0x16,0x82}; send(0xC4, d, sizeof(d)); }
  { const uint8_t d[] = {
      0x3F,0x32,0x29,0x29,0x27,0x2B,0x27,0x28,0x28,0x26,0x25,0x17,0x12,0x0D,0x04,0x00,
      0x3F,0x32,0x29,0x29,0x27,0x2B,0x27,0x28,0x28,0x26,0x25,0x17,0x12,0x0D,0x04,0x00};
    send(0xC8, d, sizeof(d)); }
  { const uint8_t d[] = {0x04,0x06,0x6B,0x0F,0x00}; send(0xD0, d, sizeof(d)); }
  { const uint8_t d[] = {0x00,0x30}; send(0xD7, d, sizeof(d)); }
  { const uint8_t d[] = {0x14}; send(0xE6, d, sizeof(d)); }
  { const uint8_t d[] = {0x01}; send(0xDE, d, sizeof(d)); }
  { const uint8_t d[] = {0x03,0x13,0xEF,0x35,0x35}; send(0xB7, d, sizeof(d)); }
  { const uint8_t d[] = {0x14,0x15,0xC0}; send(0xC1, d, sizeof(d)); }
  { const uint8_t d[] = {0x06,0x3A}; send(0xC2, d, sizeof(d)); }
  { const uint8_t d[] = {0x72,0x12}; send(0xC4, d, sizeof(d)); }
  { const uint8_t d[] = {0x00}; send(0xBE, d, sizeof(d)); }
  { const uint8_t d[] = {0x02}; send(0xDE, d, sizeof(d)); }
  { const uint8_t d[] = {0x00,0x02,0x00}; send(0xE5, d, sizeof(d)); }
  { const uint8_t d[] = {0x01,0x02,0x00}; send(0xE5, d, sizeof(d)); }
  { const uint8_t d[] = {0x00}; send(0xDE, d, sizeof(d)); }
  { const uint8_t d[] = {0x00}; send(0x35, d, sizeof(d)); }
  { const uint8_t d[] = {0x05}; send(0x3A, d, sizeof(d)); }
  { const uint8_t d[] = {0x00,0x22,0x00,0xCD}; send(0x2A, d, sizeof(d)); }
  { const uint8_t d[] = {0x00,0x00,0x01,0x3F}; send(0x2B, d, sizeof(d)); }
  { const uint8_t d[] = {0x02}; send(0xDE, d, sizeof(d)); }
  { const uint8_t d[] = {0x00,0x02,0x00}; send(0xE5, d, sizeof(d)); }
  { const uint8_t d[] = {0x00}; send(0xDE, d, sizeof(d)); }
  { const uint8_t d[] = {0x00}; send(0x36, d, sizeof(d)); }

  tft.sendCommand(0x21); // invert on
  delay(10);
  tft.sendCommand(0x29); // display on
}

void runDisplaySelfTest() {
  // 1) solid fills
  tft.fillScreen(ST77XX_RED);
  delay(220);
  tft.fillScreen(ST77XX_GREEN);
  delay(220);
  tft.fillScreen(ST77XX_BLUE);
  delay(220);

  // 2) vertical bars
  tft.fillScreen(ST77XX_BLACK);
  const uint16_t barColors[] = {ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE, ST77XX_YELLOW, ST77XX_CYAN, ST77XX_MAGENTA, ST77XX_WHITE};
  const uint8_t bars = sizeof(barColors) / sizeof(barColors[0]);
  const int16_t barW = tft.width() / bars;
  for (uint8_t i = 0; i < bars; i++) {
    tft.fillRect(i * barW, 0, barW, tft.height(), barColors[i]);
  }
  delay(350);

  // 3) checkerboard (good at exposing row/column offset issues)
  tft.fillScreen(ST77XX_BLACK);
  const uint8_t cell = 12;
  for (int16_t y = 0; y < tft.height(); y += cell) {
    for (int16_t x = 0; x < tft.width(); x += cell) {
      const bool on = ((x / cell) + (y / cell)) & 1;
      tft.fillRect(x, y, cell, cell, on ? ST77XX_WHITE : ST77XX_BLACK);
    }
  }
  drawCenteredText("SELF TEST", 8, 2, ST77XX_ORANGE);
  delay(450);
}

const ExpressionConfig& getExpressionConfig(Expression expression) {
  static const ExpressionConfig kIdle   = {Expression::Idle,   "IDLE",   false, ST77XX_WHITE, 100, 0};
  static const ExpressionConfig kHappy  = {Expression::Happy,  "HAPPY",  true,  ST77XX_GREEN, 90,  4500};
  static const ExpressionConfig kAngry  = {Expression::Angry,  "ANGRY",  true,  ST77XX_RED,   55,  4500};
  static const ExpressionConfig kSleepy = {Expression::Sleepy, "SLEEPY", false, ST77XX_CYAN,  220, 6000};

  switch (expression) {
    case Expression::Happy:  return kHappy;
    case Expression::Angry:  return kAngry;
    case Expression::Sleepy: return kSleepy;
    case Expression::Idle:
    default:                 return kIdle;
  }
}

void setExpression(Expression expression) {
  gExpression = expression;
  gExpressionSetAt = millis();
  gMonoFrameIndex = 0;
  gGifFrameIndex = 0;
  gNextFrameAt = 0;
}

void soundStartStep(uint32_t now) {
  if (gSoundIndex >= gSoundCount) {
    noTone(PIN_SPEAKER);
    gSoundPlaying = false;
    return;
  }

  const NoteStep& step = gSoundQueue[gSoundIndex];
  if (step.freq > 0) {
    tone(PIN_SPEAKER, step.freq, step.durationMs);
  } else {
    noTone(PIN_SPEAKER);
  }

  gSoundStepEndsAt = now + step.durationMs;
  gSoundInGap = false;
  gSoundPlaying = true;
}

void playSound(const NoteStep* pattern, uint8_t count) {
  if (count == 0) return;
  if (count > SOUND_QUEUE_MAX) count = SOUND_QUEUE_MAX;

  for (uint8_t i = 0; i < count; i++) {
    gSoundQueue[i] = pattern[i];
  }
  gSoundCount = count;
  gSoundIndex = 0;
  soundStartStep(millis());
}

void soundTick(uint32_t now) {
  if (!gSoundPlaying) return;
  if (now < gSoundStepEndsAt) return;

  const NoteStep& step = gSoundQueue[gSoundIndex];

  if (!gSoundInGap && step.gapMs > 0) {
    noTone(PIN_SPEAKER);
    gSoundInGap = true;
    gSoundStepEndsAt = now + step.gapMs;
    return;
  }

  gSoundIndex++;
  soundStartStep(now);
}

void initBuiltInTouch() {
  pinMode(PIN_TOUCH_RST, OUTPUT);
  pinMode(PIN_TOUCH_INT, INPUT_PULLUP);

  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(120);
  digitalWrite(PIN_TOUCH_RST, HIGH);
  delay(220);

  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL);
  attachInterrupt(digitalPinToInterrupt(PIN_TOUCH_INT), onTouchInt, FALLING);

  // Optional: probe ID register 0x08
  Wire.beginTransmission(TOUCH_I2C_ADDR);
  Wire.write(0x08);
  if (Wire.endTransmission() == 0 && Wire.requestFrom(static_cast<int>(TOUCH_I2C_ADDR), 1) == 1) {
    const uint8_t id = Wire.read();
    Serial.printf("[touch] AXS5106L ID: 0x%02X\n", id);
  } else {
    Serial.println("[touch] AXS5106L ID probe failed (will still try runtime reads)");
  }
}

bool readBuiltInTouchPressed(TouchPoint &p) {
  // Fast path: only read when interrupt fired (or force read every ~60ms when idle)
  static uint32_t lastForcedRead = 0;
  const uint32_t now = millis();
  const bool shouldRead = gTouchIntFlag || (now - lastForcedRead > 60);
  if (!shouldRead) return false;

  gTouchIntFlag = false;
  lastForcedRead = now;

  uint8_t data[14] = {0};
  Wire.beginTransmission(TOUCH_I2C_ADDR);
  Wire.write(0x01); // touch data reg
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(static_cast<int>(TOUCH_I2C_ADDR), 14) != 14) return false;
  for (uint8_t i = 0; i < 14; i++) data[i] = Wire.read();

  const uint8_t touchNum = data[1];
  if (touchNum == 0) return false;

  uint16_t x = ((static_cast<uint16_t>(data[2] & 0x0F)) << 8) | data[3];
  uint16_t y = ((static_cast<uint16_t>(data[4] & 0x0F)) << 8) | data[5];

  // Rotation mapping from Waveshare driver (default rotation=0)
  if (LCD_ROTATION == 1) {
    uint16_t ox = x, oy = y;
    y = ox;
    x = oy;
  } else if (LCD_ROTATION == 2) {
    y = LCD_HEIGHT - 1 - y;
  } else if (LCD_ROTATION == 3) {
    uint16_t ox = x, oy = y;
    y = LCD_HEIGHT - 1 - ox;
    x = LCD_WIDTH - 1 - oy;
  } else {
    x = LCD_WIDTH - 1 - x;
  }

  p.x = x;
  p.y = y;
  return true;
}

TouchEvent pollTouchEvent(uint32_t now) {
  TouchPoint tp{};
  const bool rawPressed = readBuiltInTouchPressed(tp);

  if (rawPressed != gTouchLastRaw) {
    gTouchLastRaw = rawPressed;
    gTouchLastChangeMs = now;
  }

  if ((now - gTouchLastChangeMs) > TOUCH_DEBOUNCE_MS && rawPressed != gTouchStable) {
    gTouchStable = rawPressed;

    if (gTouchStable) {
      gTouchPressStartMs = now;
      gTouchLongFired = false;
    } else {
      if (!gTouchLongFired) {
        gTapCount++;
        gTapDeadlineMs = now + DOUBLE_TAP_GAP_MS;
      }
    }
  }

  if (gTouchStable && !gTouchLongFired && (now - gTouchPressStartMs) >= LONG_PRESS_MS) {
    gTouchLongFired = true;
    gTapCount = 0;
    return TouchEvent::LongPress;
  }

  if (gTapCount > 0 && now >= gTapDeadlineMs) {
    TouchEvent event = (gTapCount >= 2) ? TouchEvent::DoubleTap : TouchEvent::SingleTap;
    gTapCount = 0;
    return event;
  }

  return TouchEvent::None;
}

void trySyncTimeWithNtp() {
  if (strlen(WIFI_SSID) == 0) {
    Serial.println("[time] WIFI_SSID empty, skipping NTP sync.");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("[time] Connecting WiFi");
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 9000) {
    Serial.print(".");
    delay(250);
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[time] WiFi connect failed. Clock mode will show uptime time.");
    return;
  }

  configTime(UTC_OFFSET_SEC, DST_OFFSET_SEC, "pool.ntp.org", "time.nist.gov");
  start = millis();
  while ((millis() - start) < 9000) {
    const time_t now = time(nullptr);
    if (now > 1700000000) {
      gTimeSynced = true;
      Serial.println("[time] NTP sync OK.");
      break;
    }
    delay(250);
  }

  if (!gTimeSynced) {
    Serial.println("[time] NTP timeout. Clock mode will show uptime time.");
  }
}

String getClockString(uint32_t nowMs) {
  char buf[16];

  if (gTimeSynced) {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    return String(buf);
  }

  const uint32_t totalSec = nowMs / 1000;
  const uint32_t hh = (totalSec / 3600) % 24;
  const uint32_t mm = (totalSec / 60) % 60;
  const uint32_t ss = totalSec % 60;
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", static_cast<unsigned long>(hh), static_cast<unsigned long>(mm), static_cast<unsigned long>(ss));
  return String(buf);
}

void renderExpressionFrame(uint32_t now) {
  const ExpressionConfig& cfg = getExpressionConfig(gExpression);
  if (now < gNextFrameAt) return;

  if (cfg.useGif) {
    const AnimatedGIF* gif = &daichi_intro_gif;
    drawMonoFrameMSB(gif->frames[gGifFrameIndex], gif->width, gif->height, cfg.color);

    const uint16_t baseDelay = gif->delays[gGifFrameIndex];
    gGifFrameIndex = (gGifFrameIndex + 1) % gif->frame_count;
    gNextFrameAt = now + safeDelayScaled(baseDelay, cfg.delayPercent);
  } else {
    drawMonoFrameXBM(kIdleAnimation.frames[gMonoFrameIndex], kIdleAnimation.width, kIdleAnimation.height, cfg.color);

    const uint16_t baseDelay = kIdleAnimation.delays[gMonoFrameIndex];
    gMonoFrameIndex = (gMonoFrameIndex + 1) % kIdleAnimation.frameCount;
    gNextFrameAt = now + safeDelayScaled(baseDelay, cfg.delayPercent);
  }

  drawCenteredText(cfg.label, 16, 1, ST77XX_YELLOW);
}

void renderClock(uint32_t now) {
  if ((now - gLastClockDrawMs) < 200) return;
  gLastClockDrawMs = now;

  tft.fillScreen(ST77XX_BLACK);
  drawCenteredText("CLOCK", 24, 2, ST77XX_CYAN);
  drawCenteredText(getClockString(now), 138, 3, ST77XX_WHITE);

  if (gTimeSynced) {
    drawCenteredText("long press: faces", 280, 1, ST77XX_GREEN);
  } else {
    drawCenteredText("uptime time (no NTP)", 264, 1, ST77XX_MAGENTA);
    drawCenteredText("long press: faces", 284, 1, ST77XX_GREEN);
  }
}

void handleTouchEvent(TouchEvent event) {
  if (event == TouchEvent::None) return;

  if (event == TouchEvent::LongPress) {
    playSound(SND_LONG, sizeof(SND_LONG) / sizeof(SND_LONG[0]));

    if (gMode == AppMode::Faces) {
      gMode = AppMode::Clock;
      gLastClockDrawMs = 0;
    } else {
      gMode = AppMode::Faces;
      setExpression(Expression::Idle);
    }
    return;
  }

  if (gMode != AppMode::Faces) {
    return;
  }

  if (event == TouchEvent::SingleTap) {
    playSound(SND_SINGLE, sizeof(SND_SINGLE) / sizeof(SND_SINGLE[0]));

    // Cycle: IDLE -> HAPPY -> SLEEPY -> IDLE
    switch (gExpression) {
      case Expression::Idle:   setExpression(Expression::Happy);  break;
      case Expression::Happy:  setExpression(Expression::Sleepy); break;
      case Expression::Sleepy: setExpression(Expression::Idle);   break;
      case Expression::Angry:  setExpression(Expression::Happy);  break;
    }
  } else if (event == TouchEvent::DoubleTap) {
    playSound(SND_DOUBLE, sizeof(SND_DOUBLE) / sizeof(SND_DOUBLE[0]));
    setExpression(Expression::Angry);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // Backlight (wiki style PWM control)
  ledcAttach(PIN_LCD_BL, BK_PWM_FREQ, BK_PWM_RES_BITS);
  const uint32_t bkMax = (1UL << BK_PWM_RES_BITS) - 1;
  const uint32_t bkDuty = (bkMax * BK_BRIGHTNESS_PERCENT) / 100;
  ledcWrite(PIN_LCD_BL, bkDuty);

  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);

  pinMode(PIN_SPEAKER, OUTPUT);

  SPI.begin(PIN_LCD_SCLK, PIN_LCD_MISO, PIN_LCD_MOSI, PIN_LCD_CS);

  tft.init(LCD_WIDTH, LCD_HEIGHT, SPI_MODE0);
  tft.setSPISpeed(LCD_SPI_HZ);
  tft.applyPanelOffset(LCD_COL_OFFSET, LCD_ROW_OFFSET);
  jd9853InitByWaveshareSequence();
  tft.setRotation(LCD_ROTATION);
  tft.invertDisplay(LCD_INVERT);
  tft.fillScreen(ST77XX_BLACK);

  initBuiltInTouch();

  runDisplaySelfTest();
  playSound(SND_STARTUP, sizeof(SND_STARTUP) / sizeof(SND_STARTUP[0]));

  trySyncTimeWithNtp();

  setExpression(Expression::Idle);
}

void loop() {
  const uint32_t now = millis();

  soundTick(now);
  handleTouchEvent(pollTouchEvent(now));

  if (gMode == AppMode::Clock) {
    renderClock(now);
    return;
  }

  // Auto-return non-idle expressions back to idle after hold duration
  const ExpressionConfig& cfg = getExpressionConfig(gExpression);
  if (cfg.holdMs > 0 && (now - gExpressionSetAt) >= cfg.holdMs) {
    setExpression(Expression::Idle);
  }

  renderExpressionFrame(now);
}
