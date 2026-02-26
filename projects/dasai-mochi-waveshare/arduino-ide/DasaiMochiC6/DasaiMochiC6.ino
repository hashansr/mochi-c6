#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#include "upiir_sample_frames.h"      // from #2 (upiir) subset
#include "huykhoong_daichi_intro.h"  // from #1 (huykhoong)

// Waveshare ESP32-C6-LCD-1.47 (from Waveshare wiki)
static constexpr uint8_t PIN_LCD_MOSI = 6;
static constexpr uint8_t PIN_LCD_SCLK = 7;
static constexpr int8_t  PIN_LCD_MISO = -1; // not used by LCD
static constexpr uint8_t PIN_LCD_CS   = 14;
static constexpr uint8_t PIN_LCD_DC   = 15;
static constexpr uint8_t PIN_LCD_RST  = 21;
static constexpr uint8_t PIN_LCD_BL   = 22;

// Onboard TF card CS (set HIGH to keep SD inactive on shared SPI bus)
static constexpr uint8_t PIN_SD_CS    = 4;

// External modules (change if your wiring is different)
static constexpr uint8_t PIN_TOUCH    = 2;   // simple touch sensor I/O pin
static constexpr uint8_t PIN_SPEAKER  = 3;   // small speaker pin
static constexpr bool TOUCH_ACTIVE_HIGH = true;

static constexpr uint16_t LCD_WIDTH  = 172;
static constexpr uint16_t LCD_HEIGHT = 320;

Adafruit_ST7789 tft(&SPI, PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST);

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

uint32_t gLastTouchMs = 0;

void beep(uint16_t freq, uint16_t durationMs) {
  tone(PIN_SPEAKER, freq, durationMs);
  delay(durationMs + 8);
  noTone(PIN_SPEAKER);
}

void drawMonoFrame(const uint8_t* frame, uint16_t width, uint16_t height, uint16_t color = ST77XX_WHITE) {
  const int16_t x = (static_cast<int16_t>(tft.width()) - static_cast<int16_t>(width)) / 2;
  const int16_t y = (static_cast<int16_t>(tft.height()) - static_cast<int16_t>(height)) / 2;

  tft.fillScreen(ST77XX_BLACK);
  tft.drawBitmap(x, y, frame, width, height, color);
}

// #1 style API (huykhoong): AnimatedGIF + playGIF(...)
void playGIF(const AnimatedGIF* gif, uint16_t loopCount = 1) {
  for (uint16_t loop = 0; loop < loopCount; loop++) {
    for (uint16_t frame = 0; frame < gif->frame_count; frame++) {
      drawMonoFrame(gif->frames[frame], gif->width, gif->height, ST77XX_CYAN);
      delay(gif->delays[frame]);
    }
  }
}

// #2 style API (upiir): array-of-frames + frame cursor loop
void playMonoAnimation(const MonoAnimation& anim, uint16_t loopCount = 1) {
  for (uint16_t loop = 0; loop < loopCount; loop++) {
    for (uint16_t frame = 0; frame < anim.frameCount; frame++) {
      drawMonoFrame(anim.frames[frame], anim.width, anim.height, ST77XX_WHITE);
      delay(anim.delays[frame]);
    }
  }
}

bool touchTriggered() {
  const bool raw = digitalRead(PIN_TOUCH) == (TOUCH_ACTIVE_HIGH ? HIGH : LOW);
  const uint32_t now = millis();
  if (raw && (now - gLastTouchMs) > 180) {
    gLastTouchMs = now;
    return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(250);

  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);

  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);

  pinMode(PIN_TOUCH, INPUT);
  pinMode(PIN_SPEAKER, OUTPUT);

  SPI.begin(PIN_LCD_SCLK, PIN_LCD_MISO, PIN_LCD_MOSI, PIN_LCD_CS);

  tft.init(LCD_WIDTH, LCD_HEIGHT);
  tft.setRotation(0); // 172x320 portrait
  tft.fillScreen(ST77XX_BLACK);

  beep(1600, 60);
  beep(2000, 60);

  // Quick startup animation from #1
  playGIF(&daichi_intro_gif, 1);
}

void loop() {
  if (touchTriggered()) {
    beep(2600, 50);
    beep(3000, 50);

    // On touch: play #1 animation sequence once
    playGIF(&daichi_intro_gif, 1);
    return;
  }

  // Idle: #2 frame-loop style animation
  playMonoAnimation(kIdleAnimation, 1);
}
