#pragma once

// ─── Hardware: ESP32-S3 N16R8 (16MB Flash, 8MB PSRAM) ────────────────────────

// PCM5102 I2S DAC  (SD pin HIGH = normal, LOW = mute)
#define PIN_I2S_BCK       6
#define PIN_I2S_WS        4
#define PIN_I2S_DATA      5
#define PIN_PCM_SD        7   // drive HIGH to unmute

// SH1106 1.3" OLED — I2C  (EstarDyn module, same bus as encoder buttons)
#define PIN_OLED_SDA      10
#define PIN_OLED_SCL      11
#define OLED_I2C_ADDR     0x3C
#define OLED_I2C_FREQ     400000

// EC11 Rotary Encoder (on EstarDyn module, direct GPIO)
#define PIN_ENC_A         14  // CLK
#define PIN_ENC_B         15  // DT
#define PIN_ENC_SW        16  // Push / Select

// Control Buttons (on EstarDyn module)
#define PIN_BTN_BACK      17
#define PIN_BTN_CONFIRM   18  // also Play/Stop
// Optional extra button on breadboard
#define PIN_BTN_SHIFT     21

// MIDI Out — UART1 TX, 31250 baud
#define PIN_MIDI_TX       1
#define PIN_MIDI_RX       2   // not used but reserve
#define MIDI_BAUD         31250

// ─── Audio ────────────────────────────────────────────────────────────────────
#define SAMPLE_RATE       44100

// ─── Synthesis ────────────────────────────────────────────────────────────────
#define NUM_FM_OPS        4
#define FM_PATCHES_COUNT  16
#define JUNO_PATCHES_COUNT 16

// ─── Sequencer ────────────────────────────────────────────────────────────────
#define NUM_STEPS         16
#define NUM_PATTERNS      8
#define CHAIN_LEN         8
#define BPM_MIN           40
#define BPM_MAX           240
#define BPM_DEFAULT       120
#define SWING_MAX         30    // max swing offset %

// ─── Display ─────────────────────────────────────────────────────────────────
#define DISP_W            128
#define DISP_H            64
#define HEADER_H          10
#define STEP_CELL_W       16    // 128 / 8 steps per row
#define STEP_CELL_H       12
#define STEP_ROW1_Y       11
#define STEP_ROW2_Y       24    // ROW1_Y + STEP_CELL_H + 1

// ─── Timing ───────────────────────────────────────────────────────────────────
#define UI_REFRESH_MS     33    // ~30 fps
#define DEBOUNCE_MS       20   // 20 ms covers worst-case tact-switch bounce
#define LONG_PRESS_MS     600
// EC11 encoders emit 4 quadrature transitions per detent click.
// Set to 2 or 1 if your encoder feels like it needs two clicks per step.
#define ENC_TICKS_PER_DETENT 4

// ─── Storage ─────────────────────────────────────────────────────────────────
#define STORAGE_MAGIC     0x5351   // "SQ"
#define STORAGE_VERSION   1
