#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include "config.h"
#include "synth.h"
#include "sequencer.h"

// ─── UI Screens ───────────────────────────────────────────────────────────────

enum class Screen : uint8_t {
    MAIN = 0,      // Step grid + playback
    STEP_EDIT,     // Edit individual step params
    SYNTH_MODE,    // Choose synth mode (ANALOG/JUNO/FM/BASS/PAD/KEYS)
    SYNTH_PARAMS,  // Edit synth parameters (scrollable list)
    PATTERN_SEL,   // Choose active pattern 1-8
    PATTERN_OPTS,  // Pattern length, MIDI channel, swing
    CHAIN_EDIT,    // Pattern chain setup
    SCALE_SEL,     // Scale + root note picker
    BPM_EDIT,      // BPM editor (big display)
    MIDI_SETTINGS, // MIDI channel, clock out
    SETTINGS,      // Master volume, save/load
    MAIN_MENU,     // Top-level menu overlay
};

// ─── Synth param list entries ─────────────────────────────────────────────────

enum class SynthParamID : uint8_t {
    // Shown in all modes
    MODE = 0,
    MASTER_VOL,
    // Oscillator (ANALOG/BASS)
    OSC1_WAVE, OSC1_LEVEL,
    OSC2_WAVE, OSC2_LEVEL, OSC2_DETUNE, OSC2_COARSE,
    OSC_BALANCE, NOISE_LEVEL, PULSE_WIDTH, SUB_LEVEL,
    // Filter
    FILTER_CUTOFF, FILTER_RES, FILTER_MODE,
    FILTER_ENV_DEPTH, FILTER_KEYTRACK, HPF_CUTOFF,
    // Amp ADSR
    AMP_ATK, AMP_DEC, AMP_SUS, AMP_REL,
    // Filter ADSR
    FENV_ATK, FENV_DEC, FENV_SUS, FENV_REL,
    // LFO
    LFO_WAVE, LFO_RATE, LFO_DEPTH, LFO_DEST, LFO_PWM_DEPTH,
    // Portamento
    PORTA_TIME,
    // Juno
    JUNO_PATCH, CHORUS_MODE, CHORUS_DEPTH, CHORUS_RATE,
    // FM
    FM_ALGO, FM_PATCH,
    FM_OP1_RATIO, FM_OP1_LEVEL, FM_OP1_DECAY,
    FM_OP2_RATIO, FM_OP2_LEVEL, FM_OP2_DECAY,
    FM_OP3_RATIO, FM_OP3_LEVEL, FM_OP3_DECAY,
    FM_OP4_RATIO, FM_OP4_LEVEL, FM_OP4_DECAY,
    FM_FEEDBACK,
    // Effects
    REVERB_AMT, DELAY_TIME, DELAY_FEEDBACK,
    NUM_PARAMS
};

// ─── Step edit focus ──────────────────────────────────────────────────────────

enum class StepField : uint8_t {
    NOTE = 0, VELOCITY, GATE, PROBABILITY, ACCENT, SLIDE, ACTIVE,
    NUM_FIELDS
};

// ─── Menu items ───────────────────────────────────────────────────────────────

enum class MenuItem : uint8_t {
    SEQ_STEP_EDIT = 0,
    SEQ_BPM,
    PAT_SELECT,
    PAT_OPTIONS,
    SCALE_SEL,
    PAT_CHAIN,
    SYNTH_MODE,
    SYNTH_PARAMS,
    MIDI_SETTINGS,
    SETTINGS,
    NUM_ITEMS
};

// ─── UI class ────────────────────────────────────────────────────────────────

class UI {
public:
    UI(Sequencer* seq, SynthEngine* engine);
    void begin();
    void update();    // call every loop — handles input + redraw

    // Called from main loop with button/encoder events
    void handleEncoder(int delta);
    void handleEncPress();
    void handleBack();
    void handleConfirm();
    void handleShift();
    void handleLongBack();
    void handleLongConfirm();

    // Expose currently selected step for external use
    uint8_t selectedStep() const { return _selStep; }

    // Force a full redraw next frame
    void dirty() { _dirty = true; }

private:
    Sequencer*   _seq;
    SynthEngine* _engine;

    // SW_I2C: bit-bangs directly on the GPIO — no Wire library conflicts on ESP32-S3
    U8G2_SH1106_128X64_NONAME_F_SW_I2C _u8g2;

    Screen    _screen    = Screen::MAIN;
    Screen    _prevScreen= Screen::MAIN;
    bool      _dirty     = true;
    unsigned long _lastDraw = 0;

    // Main screen state
    uint8_t   _selStep   = 0;    // cursor step (0-15)
    bool      _editMode  = false; // encoder edits value vs navigates

    // Step edit state
    StepField _stepField  = StepField::NOTE;
    bool      _stepEditing= false;

    // Synth params state
    uint8_t   _synthScroll= 0;   // first visible param row
    SynthParamID _synthSel= SynthParamID::MODE;
    bool      _synthEditing=false;

    // Pattern select state
    uint8_t   _patSel     = 0;

    // Pattern options row cursor (0=length, 1=MIDI ch, 2=swing)
    uint8_t   _patOptSel  = 0;

    // Scale select
    uint8_t   _scaleTmp   = 0;
    uint8_t   _rootTmp    = 0;

    // BPM editing
    int       _bpmTmp     = BPM_DEFAULT;

    // Main menu
    MenuItem  _menuSel    = MenuItem::SEQ_STEP_EDIT;

    // Synth mode selection
    SynthMode _modeTmp    = SynthMode::DUAL_OSC;

    // MIDI settings
    uint8_t   _midiChTmp   = 1;
    bool      _midiClkTmp  = false;
    uint8_t   _midiSel     = 0;   // 0=channel row, 1=clock row
    bool      _midiEditing = false;

    // Settings screen
    bool      _settingsEditing = false;

    // Chain edit
    uint8_t   _chainEditPos = 0;
    ChainEntry _chainBuf[CHAIN_LEN];

    // ── Drawing functions ─────────────────────────────────────────────────────
    void drawAll();
    void drawHeader();
    void drawMainScreen();
    void drawStepEdit();
    void drawSynthMode();
    void drawSynthParams();
    void drawPatternSel();
    void drawPatternOpts();
    void drawChainEdit();
    void drawScaleSel();
    void drawBPMEdit();
    void drawMIDISettings();
    void drawSettings();
    void drawMainMenu();

    // Step cell helpers
    void drawStepCell(uint8_t step, uint8_t x, uint8_t y, bool cursor, bool playing);

    // Param access helpers
    float   getSynthParamF(SynthParamID id);
    void    setSynthParamF(SynthParamID id, float v);
    void    setSynthParamI(SynthParamID id, int v);
    const char* synthParamLabel(SynthParamID id);
    const char* synthParamValue(SynthParamID id, char* buf);
    bool    isSynthParamVisible(SynthParamID id);
    float   synthParamMin(SynthParamID id);
    float   synthParamMax(SynthParamID id);
    float   synthParamStep(SynthParamID id);

    // Navigation helpers
    void pushScreen(Screen s);
    void popScreen();
    void changeSynthParam(int delta);
    void changeStepField(int delta);

    // Visible-param navigation (selection must skip params hidden by mode)
    SynthParamID stepVisibleParam(SynthParamID from, int dir);
    int          visibleIndexOf(SynthParamID id);

    // Push edited params to the audio engine
    void applyEngine(SynthParamID id);
};
