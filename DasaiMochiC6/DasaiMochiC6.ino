#include <Arduino.h>
#include <SPI.h>
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
// Waveshare ESP32-C6-LCD-1.47 pins (from Waveshare wiki)
// NOTE: this sketch is for NON-TOUCH 1.47 (ST7789).
// If your board is Touch-LCD-1.47 (JD9853), use a JD9853 driver instead.
// ==============================
static constexpr uint8_t PIN_LCD_MOSI = 6;
static constexpr uint8_t PIN_LCD_SCLK = 7;
static constexpr int8_t  PIN_LCD_MISO = -1; // not used by LCD
static constexpr uint8_t PIN_LCD_CS   = 14;
static constexpr uint8_t PIN_LCD_DC   = 15;
static constexpr uint8_t PIN_LCD_RST  = 21;
static constexpr uint8_t PIN_LCD_BL   = 22;

// Onboard TF card CS (set HIGH to keep SD inactive on shared SPI bus)
static constexpr uint8_t PIN_SD_CS = 4;

// External modules (change if your wiring is different)
static constexpr uint8_t PIN_TOUCH = 2;   // touch module I/O
static constexpr uint8_t PIN_SPEAKER = 3; // small speaker
static constexpr bool TOUCH_ACTIVE_HIGH = true;

static constexpr uint16_t LCD_WIDTH = 172;
static constexpr uint16_t LCD_HEIGHT = 320;

// Touch timings
static constexpr uint32_t TOUCH_DEBOUNCE_MS = 30;
static constexpr uint32_t DOUBLE_TAP_GAP_MS = 280;
static constexpr uint32_t LONG_PRESS_MS = 700;

// ST7789 on 1.47 non-touch board often needs column offset on 172x320 panel.
// If you still see vertical lines / shifted image, tune LCD_COL_OFFSET / LCD_ROW_OFFSET.
static constexpr uint32_t LCD_SPI_HZ = 10000000; // conservative for stability
static constexpr int8_t LCD_COL_OFFSET = 34;
static constexpr int8_t LCD_ROW_OFFSET = 0;

class WaveshareST7789 : public Adafruit_ST7789 {
public:
  WaveshareST7789(SPIClass *spiClass, int8_t cs, int8_t dc, int8_t rst)
      : Adafruit_ST7789(spiClass, cs, dc, rst) {}

  void applyPanelOffset(int8_t col, int8_t row) { setColRowStart(col, row); }
};

WaveshareST7789 tft(&SPI, PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST);

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

void drawMonoFrame(const uint8_t* frame, uint16_t width, uint16_t height, uint16_t color) {
  const int16_t x = (static_cast<int16_t>(tft.width()) - static_cast<int16_t>(width)) / 2;
  const int16_t y = (static_cast<int16_t>(tft.height()) - static_cast<int16_t>(height)) / 2;

  tft.fillScreen(ST77XX_BLACK);
  tft.drawBitmap(x, y, frame, width, height, color);
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

TouchEvent pollTouchEvent(uint32_t now) {
  const bool rawPressed = digitalRead(PIN_TOUCH) == (TOUCH_ACTIVE_HIGH ? HIGH : LOW);

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
    drawMonoFrame(gif->frames[gGifFrameIndex], gif->width, gif->height, cfg.color);

    const uint16_t baseDelay = gif->delays[gGifFrameIndex];
    gGifFrameIndex = (gGifFrameIndex + 1) % gif->frame_count;
    gNextFrameAt = now + safeDelayScaled(baseDelay, cfg.delayPercent);
  } else {
    drawMonoFrame(kIdleAnimation.frames[gMonoFrameIndex], kIdleAnimation.width, kIdleAnimation.height, cfg.color);

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

  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);

  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);

  pinMode(PIN_TOUCH, INPUT);
  pinMode(PIN_SPEAKER, OUTPUT);

  SPI.begin(PIN_LCD_SCLK, PIN_LCD_MISO, PIN_LCD_MOSI, PIN_LCD_CS);

  tft.init(LCD_WIDTH, LCD_HEIGHT, SPI_MODE0);
  tft.setSPISpeed(LCD_SPI_HZ);
  tft.applyPanelOffset(LCD_COL_OFFSET, LCD_ROW_OFFSET);
  tft.setRotation(0); // 172x320 portrait
  tft.fillScreen(ST77XX_BLACK);

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
