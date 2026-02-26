# Dasai Mochi Clone Research (ESP32, multi-face)

Date: 2026-02-26 (UTC)

## Goal
Find a **simple** ESP32 Dasai Mochi-style project with **multiple faces** that can be ported to a Waveshare ESP32 board.

## Shortlist

### 1) huykhoong/esp32_dasai_mochi_clone_and_how_to
- URL: https://github.com/huykhoong/esp32_dasai_mochi_clone_and_how_to
- Why it fits:
  - Has a clean `playGIF(const AnimatedGIF*)` renderer.
  - Supports multiple animations/faces by including multiple generated `.h` files.
  - Includes many source GIFs (angry/happy/love/sleepy/etc.) and sample code with two animations.
- Tech:
  - ESP32 + `U8g2` on 128x64 monochrome OLED.
  - GIFs converted into C arrays (`gif2cpp` flow).
- Notes:
  - Repository has **no explicit license**; treat as reference unless permission/license is clarified.

### 2) upiir/esp32s3_oled_dasai_mochi
- URL: https://github.com/upiir/esp32s3_oled_dasai_mochi
- Why it fits (partially):
  - Very simple baseline.
  - MIT licensed.
- Limitation:
  - It is effectively one long 90-frame loop, not expression/state-based face switching out of the box.

### 3) HARAJIT05/XERO
- URL: https://github.com/HARAJIT05/XERO
- Why considered:
  - Large frame set and Dasai-like output.
- Limitation:
  - Huge monolithic bitmap source; not ideal as a simple starting point.

## Recommendation
Use **huykhoong/esp32_dasai_mochi_clone_and_how_to** as the technical reference for a new clean-room implementation because it already has:
- multi-face architecture,
- a reusable animation player,
- a practical asset pipeline (GIF -> header).

Then build a fresh project around that idea for Waveshare-specific hardware.

## Waveshare Port Plan (new project)
1. Confirm exact Waveshare board model (display driver + pins).
2. Keep renderer API generic:
   - `playAnimation(animation_t*)`
   - `setExpression(EXPR_HAPPY/ANGRY/SLEEPY/...)`
3. Create a board abstraction layer:
   - `display_init()`
   - `display_draw_frame(bitmap, w, h)`
4. Start with 3 expressions: `idle`, `blink`, `happy`.
5. Add an expression scheduler (timed random + manual trigger).

## Risks / Caveats
- Face assets from original Dasai media may be copyrighted; for distributable code, use original or clearly permitted assets.
- If Waveshare board uses color TFT (e.g., ST7789/GC9A01), monochrome frame format must be adapted or replaced.
