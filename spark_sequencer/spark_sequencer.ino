/*
 * Spark Synth — Step Sequencer
 *
 * Hardware:
 *   MCU  : ESP32-S3 N16R8 (16 MB Flash, 8 MB PSRAM)
 *   Audio: PCM5102 I2S DAC → 3.5 mm TRS stereo
 *   OLED : EstarDyn 1.3" SH1106 128×64 I2C + EC11 encoder + BACK/CONFIRM
 *   MIDI : 3.5 mm TRS out (UART1 31250 baud)
 *   Flash: W25Q external SPI (optional; patterns on internal LittleFS)
 *
 * Libraries (install via Library Manager / Arduino IDE):
 *   - arduino-audio-tools  by Phil Schatzmann  (AudioTools)
 *   - U8g2                 by Oliver Kraus
 *   - LittleFS             (built-in ESP32 Arduino core)
 *
 * Board: "ESP32S3 Dev Module" — PSRAM: "OPI PSRAM", Flash size: 16MB
 *
 * Controls:
 *   Encoder rotate  → navigate / change value
 *   Encoder push    → select / enter edit mode
 *   BACK button     → cancel / back (long = main menu from main screen)
 *   CONFIRM button  → play/stop (on main) / confirm
 *   SHIFT button    → modifier (hold + rotate = BPM on main screen)
 *   Long CONFIRM    → toggle active on selected step
 *   Long BACK       → save current pattern
 *
 * UI Layout (main screen, 128×64):
 *   ┌───BPM─►─PAT──MODE───SCALE──────────┐  (header, 10px)
 *   │ [1][2][3][4][5][6][7][8]           │  (step row 1, y=11)
 *   │ [9][A][B][C][D][E][F][G]           │  (step row 2, y=25)
 *   │▓▓▓░░░░░░░░░░░░░░░░░░░░░            │  (progress bar)
 *   ├─────────────────────────────────────┤
 *   │ C#4  V:80  G:75%                   │
 *   │ P:90%  ACC  SLD                    │
 *   └─────────────────────────────────────┘
 */

#include "AudioTools.h"  // pschatzmann/arduino-audio-tools

#include "config.h"
#include "synth.h"
#include "sequencer.h"
#include "controls.h"
#include "midi_out.h"
#include "storage.h"
#include "ui.h"
#include "scales.h"

// ─── Global objects ───────────────────────────────────────────────────────────

SynthEngine  synthEngine;
Sequencer    sequencer;
MidiOut      midiOut;
Storage      storage;
Controls     controls;
UI           ui(&sequencer, &synthEngine);

// AudioTools I2S output
I2SStream    i2sOut;

// Audio task handle
TaskHandle_t audioTaskHandle = nullptr;

// ─── Audio task (Core 0) ──────────────────────────────────────────────────────

static int16_t audioBuf[AUDIO_BUFFER_SZ * 2];  // stereo frames, static = DRAM

void audioTask(void* param) {
    for (;;) {
        synthEngine.process(audioBuf, AUDIO_BUFFER_SZ);
        i2sOut.write((uint8_t*)audioBuf, sizeof(audioBuf));
    }
}

// ─── Setup ────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("[SPARK] Sequencer booting...");

    // PCM5102 soft-mute pin — pull HIGH to enable DAC
    pinMode(PIN_PCM_SD, OUTPUT);
    digitalWrite(PIN_PCM_SD, LOW);  // mute during init

    // ── Storage ────────────────────────────────────────────────────────────
    if (!storage.begin()) {
        Serial.println("[WARN] Storage mount failed — using RAM only");
    }

    // ── MIDI ───────────────────────────────────────────────────────────────
    midiOut.begin();

    // ── Synth engine ───────────────────────────────────────────────────────
    synthEngine.begin(SAMPLE_RATE);

    // ── Sequencer ──────────────────────────────────────────────────────────
    sequencer.begin();
    sequencer.setEngine(&synthEngine);

    // Load saved patterns and settings
    GlobalSettings gs;
    if (storage.loadSettings(gs)) {
        sequencer.setBPM(gs.bpm);
    }
    for (uint8_t i = 0; i < NUM_PATTERNS; i++) {
        Pattern pat;
        if (storage.loadPattern(i, pat)) {
            sequencer.getPattern(i) = pat;
        }
    }

    // Load first pattern's synth params
    synthEngine.setParams(sequencer.getCurrentPattern().synth);

    // ── I2S / PCM5102 ──────────────────────────────────────────────────────
    auto cfg          = i2sOut.defaultConfig(TX_MODE);
    cfg.pin_bck       = PIN_I2S_BCK;
    cfg.pin_ws        = PIN_I2S_WS;
    cfg.pin_data      = PIN_I2S_DATA;
    cfg.sample_rate   = SAMPLE_RATE;
    cfg.channels      = AUDIO_CHANNELS;
    cfg.bits_per_sample = BITS_PER_SAMPLE;
    cfg.buffer_size   = AUDIO_BUFFER_SZ * AUDIO_CHANNELS * (BITS_PER_SAMPLE / 8);
    cfg.buffer_count  = DMA_BUF_COUNT;
    cfg.i2s_format    = I2S_STD_FORMAT;

    if (!i2sOut.begin(cfg)) {
        Serial.println("[ERR] I2S init failed!");
    }

    // Unmute PCM5102
    delay(50);
    digitalWrite(PIN_PCM_SD, HIGH);

    // ── Audio task on Core 0 ───────────────────────────────────────────────
    xTaskCreatePinnedToCore(
        audioTask,
        "audio",
        4096,               // stack (synthesis is mostly register-based)
        nullptr,
        configMAX_PRIORITIES - 1,  // highest priority
        &audioTaskHandle,
        0                   // Core 0
    );

    // ── Controls ───────────────────────────────────────────────────────────
    controls.begin();

    // ── Display ────────────────────────────────────────────────────────────
    ui.begin();

    Serial.println("[SPARK] Boot complete.");

    // Demo: init first pattern with a simple C minor pentatonic line
    initDemoPattern();
}

// ─── Demo pattern ─────────────────────────────────────────────────────────────

void initDemoPattern() {
    Pattern& pat = sequencer.getPattern(0);
    pat.rootNote = 0;  // C
    pat.scaleIdx = 9;  // Pentatonic Minor

    // C3 minor pentatonic — MIDI notes: 48, 51, 53, 55, 58
    uint8_t pentatonicNotes[] = { 48, 51, 53, 55, 58, 60, 63, 65 };

    for (int s = 0; s < NUM_STEPS; s++) {
        Step& step = pat.steps[s];
        step.active      = true;
        step.note        = pentatonicNotes[s % 8];
        step.velocity    = (s % 4 == 0) ? 110 : 80;  // strong beats
        step.gate        = (s % 4 == 0) ? 80 : 60;
        step.probability = 100;
        step.accent      = (s % 8 == 0);  // accent every half-bar
        step.slide       = (s == 3 || s == 7 || s == 11);
    }
    // Leave a few steps silent for groove
    pat.steps[5].active  = false;
    pat.steps[13].active = false;

    pat.synth.mode        = SynthMode::BASS;
    pat.synth.filterCutoff= 800;
    pat.synth.filterRes   = 3.5f;
    pat.synth.filterEnvDepth = 0.8f;
    pat.synth.fEnvDec     = 0.12f;
    pat.synth.attack      = 0.002f;
    pat.synth.decay       = 0.3f;
    pat.synth.sustain     = 0.0f;
    pat.synth.portaTime   = 0.04f;

    synthEngine.setParams(pat.synth);
}

// ─── Loop (Core 1 — UI + Sequencer) ──────────────────────────────────────────

void loop() {
    controls.update();

    // ── Encoder input ─────────────────────────────────────────────────────
    int delta = controls.encoder.getDelta();
    if (delta != 0) {
        ui.handleEncoder(delta);
    }

    if (controls.encoder.wasPressed()) {
        ui.handleEncPress();
    }
    if (controls.encoder.wasLongPress()) {
        // Long encoder push = enter main menu from anywhere
        ui.handleBack();
    }

    // ── BACK button ───────────────────────────────────────────────────────
    if (controls.btnBack.wasPressed()) {
        ui.handleBack();
    }
    if (controls.btnBack.wasLongPress()) {
        ui.handleLongBack();
    }

    // ── CONFIRM button (also Play/Stop on main screen) ────────────────────
    if (controls.btnConfirm.wasPressed()) {
        ui.handleConfirm();
    }
    if (controls.btnConfirm.wasLongPress()) {
        ui.handleLongConfirm();
    }

    // ── SHIFT button (modifier) ───────────────────────────────────────────
    if (controls.btnShift.wasPressed()) {
        ui.handleShift();
    }

    // ── Sequencer clock ───────────────────────────────────────────────────
    sequencer.update();

    // ── Display refresh ───────────────────────────────────────────────────
    ui.update();
}

// ─── Arduino entry point for Core 0 pre-task ─────────────────────────────────
// (audioTask runs on Core 0 via FreeRTOS — setup1()/loop1() not used here)
