/*
 * Spark Synth — Step Sequencer
 *
 * Hardware:
 *   MCU  : ESP32-S3 N16R8 (16 MB Flash, 8 MB PSRAM)
 *   Audio: PCM5102 I2S DAC → 3.5 mm TRS stereo
 *   OLED : SH1106 1.3" 128×64 I2C + EC11 encoder + BACK/CONFIRM
 *   MIDI : 3.5 mm TRS out (UART1 31250 baud)
 *   Flash: Internal LittleFS (patterns / settings)
 *
 * Libraries (install via Library Manager):
 *   - AMY                  by shorepine / AllMusicYes  (github.com/shorepine/amy)
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

// Local headers first — they must be parsed before amy.h defines its macros
// (SINE, PULSE, TRIANGLE, NOISE etc.) to prevent enum corruption.
#include "config.h"
#include "synth.h"
#include "sequencer.h"
#include "controls.h"
#include "midi_out.h"
#include "storage.h"
#include "ui.h"
#include "scales.h"

// AMY synthesis engine — include AFTER local headers.
// AMY is a pure C library; extern "C" is required.
// Install: https://github.com/shorepine/amy  (Arduino Library Manager: "AMY")
extern "C" {
#include <amy.h>
}

// ─── Global objects ───────────────────────────────────────────────────────────

SynthEngine  synthEngine;
Sequencer    sequencer;
MidiOut      midiOut;
Storage      storage;
Controls     controls;
UI           ui(&sequencer, &synthEngine);

// ─── Setup ────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("[SPARK] Sequencer booting...");

    // Seed RNG (step probability) from hardware entropy
    randomSeed(esp_random());

    // PCM5102 soft-mute pin — pull HIGH to enable DAC output
    pinMode(PIN_PCM_SD, OUTPUT);
    digitalWrite(PIN_PCM_SD, LOW);   // mute during init

    // ── AMY init ───────────────────────────────────────────────────────────
    // AMY manages its own I2S output and FreeRTOS audio task internally.
    amy_config_t amyCfg         = amy_default_config();
    amyCfg.audio                = AMY_AUDIO_IS_I2S;
    amyCfg.i2s_bclk             = PIN_I2S_BCK;
    amyCfg.i2s_lrc              = PIN_I2S_WS;
    amyCfg.i2s_dout             = PIN_I2S_DATA;
    amyCfg.features.reverb      = 1;
    amyCfg.features.echo        = 1;
    amyCfg.features.chorus      = 0;
    amyCfg.features.default_synths = 1;  // creates synth channel 1
    amyCfg.features.startup_bleep = 1;   // audible self-test tone on boot
    amyCfg.max_oscs             = 200;
    amyCfg.max_voices           = 32;
    amyCfg.max_synths           = 16;
    amyCfg.midi                 = AMY_MIDI_IS_NONE;
    amyCfg.platform.multicore   = 1;
    amyCfg.platform.multithread = 1;
    amy_start(amyCfg);
    Serial.println("[AMY] started (listen for startup bleep!)");

    amy_event resetEvent = amy_default_event();
    resetEvent.reset_osc = RESET_AMY;
    amy_add_event(&resetEvent);

    // Unmute PCM5102 after AMY is running
    delay(50);
    digitalWrite(PIN_PCM_SD, HIGH);

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
    bool haveSettings = storage.loadSettings(gs);
    if (haveSettings) {
        sequencer.setBPM(gs.bpm);
        sequencer.setMidiClock(gs.midiClockOut);
    }
    for (uint8_t i = 0; i < NUM_PATTERNS; i++) {
        Pattern pat;
        if (storage.loadPattern(i, pat)) {
            sequencer.getPattern(i) = pat;
        }
    }
    if (haveSettings && gs.lastPattern < NUM_PATTERNS) {
        sequencer.setPattern(gs.lastPattern);
    }

    // Demo pattern on first boot, or if pattern 0 has no active steps
    if (!storage.patternExists(0)) {
        initDemoPattern();
    } else {
        bool anyActive = false;
        Pattern& p0 = sequencer.getPattern(0);
        for (int s = 0; s < NUM_STEPS && !anyActive; s++) anyActive = p0.steps[s].active;
        if (!anyActive) initDemoPattern();
    }

    // Apply current pattern's synth params
    if (haveSettings) {
        sequencer.getCurrentPattern().synth.masterVol = gs.masterVol;
    }
    synthEngine.setParams(sequencer.getCurrentPattern().synth);

    // ── Controls ───────────────────────────────────────────────────────────
    controls.begin();

    // ── Display ────────────────────────────────────────────────────────────
    ui.begin();

    Serial.println("[SPARK] Boot complete.");
}

// ─── Demo pattern ─────────────────────────────────────────────────────────────

void initDemoPattern() {
    Pattern& pat = sequencer.getPattern(0);
    pat.rootNote = 0;  // C
    pat.scaleIdx = 9;  // Pentatonic Minor

    // C3 minor pentatonic MIDI notes
    uint8_t pentatonicNotes[] = { 48, 51, 53, 55, 58, 60, 63, 65 };

    for (int s = 0; s < NUM_STEPS; s++) {
        Step& step       = pat.steps[s];
        step.active      = true;
        step.note        = pentatonicNotes[s % 8];
        step.velocity    = (s % 4 == 0) ? 110 : 80;
        step.gate        = (s % 4 == 0) ? 80 : 60;
        step.probability = 100;
        step.accent      = (s % 8 == 0);
        step.slide       = (s == 3 || s == 7 || s == 11);
    }
    pat.steps[5].active  = false;
    pat.steps[13].active = false;

    // BASS mode via AMY custom patch — acid-style ADSR
    pat.synth.mode           = SynthMode::BASS;
    pat.synth.filterCutoff   = 800.0f;
    pat.synth.filterRes      = 3.5f;
    pat.synth.filterEnvDepth = 0.8f;
    pat.synth.fEnvDec        = 0.12f;
    pat.synth.attack         = 0.002f;
    pat.synth.decay          = 0.3f;
    pat.synth.sustain        = 0.0f;
    pat.synth.release        = 0.08f;
    pat.synth.portaTime      = 0.04f;
}

// ─── Loop (Core 1 — UI + Sequencer) ──────────────────────────────────────────

void loop() {
    // Sequencer clock first — it's the most timing-sensitive consumer,
    // and a display redraw later in the loop can block for milliseconds.
    sequencer.update();

    controls.update();

    // ── Encoder input ─────────────────────────────────────────────────────
    int delta = controls.encoder.getDelta();
    if (delta != 0) {
        ui.handleEncoder(delta);
    }

    if (controls.encoder.wasPressed()) {
        ui.handleEncPress();
    }
    // Encoder long-press: consume it silently so it doesn't trigger back
    // navigation. The BACK button is the dedicated back control.
    controls.encoder.wasLongPress();

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

    // ── SHIFT button — fires on ANY release (toggle, no long-press action)
    if (controls.btnShift.wasReleased()) {
        ui.handleShift();
    }

    // ── Display refresh ───────────────────────────────────────────────────
    ui.update();
}
