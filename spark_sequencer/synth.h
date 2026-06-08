#pragma once
#include <Arduino.h>
#include "config.h"
#include "scales.h"

// ─── Enumerations ─────────────────────────────────────────────────────────────
// NOTE: ANALOG  is #defined 0xC0  by esp32-hal-gpio.h — use DUAL_OSC.
// NOTE: NOISE   is #defined 0x5   by esp32-hal-gpio.h — use WAVE_NOISE.
// NOTE: SINE / TRIANGLE / PULSE are #defined by amy.h — prefix all WaveType
//       values with WAVE_ so the enum is never corrupted by those macros.

enum class SynthMode : uint8_t {
    DUAL_OSC = 0, // Dual oscillator analog
    JUNO,         // Juno DCO subtractive — AMY patches 0-127
    FM,           // DX7 FM synthesis    — AMY patches 128-255
    BASS,         // Acid bass (TB-303 style)
    PAD,          // Pad / strings
    KEYS,         // Piano — AMY built-in patch 256
    NUM_MODES
};

enum class WaveType : uint8_t {
    WAVE_SINE = 0, WAVE_TRI, WAVE_SAW, WAVE_SQR, WAVE_PULSE, WAVE_NOISE, NUM_WAVES
};

enum class LFODest : uint8_t {
    DEST_NONE = 0, FILTER, PITCH, AMP, PWM, NUM_DESTS
};

enum class FilterMode : uint8_t {
    LOWPASS = 0, BANDPASS, HIGHPASS, NOTCH
};

enum class ChorusMode : uint8_t {
    OFF = 0, MODE1, MODE2
};

// ─── FM algorithm descriptor (display / UI use) ───────────────────────────────

struct FMAlgo {
    const char name[6];
    bool isCarrier[NUM_FM_OPS];
    int8_t modSource[NUM_FM_OPS];
    bool feedback;
};

static const FMAlgo FM_ALGOS[8] = {
    { "4-CHN", {true,false,false,false}, {1,2,3,-1}, false },
    { "2CRY1", {true,false,false,false}, {-1,2,3,-1}, false },
    { "3MOD1", {true,false,false,false}, {-1,-1,-1,-1}, false },
    { "2CH1C", {true,false,false,false}, {1,2,-1,-1}, false },
    { "2PAIR", {true,true,false,false}, {3,-1,-1,-1}, false },
    { "1TO3C", {true,true,true,false}, {3,3,3,-1}, false },
    { "ADDTV", {true,true,true,true}, {-1,-1,-1,-1}, false },
    { "FDBK",  {false,false,false,true},{-1,-1,-1,0}, true },
};

// ─── FM patch presets ─────────────────────────────────────────────────────────
// These populate SynthParams for UI display; actual sound uses AMY DX7 patches.

struct FMPatch {
    char     name[12];
    float    opRatio[NUM_FM_OPS];
    float    opLevel[NUM_FM_OPS];
    float    opDecay[NUM_FM_OPS];
    float    opSustain[NUM_FM_OPS];
    uint8_t  algo;
    float    feedback;
    float    filterCutoff;
    float    filterRes;
};

static const FMPatch FM_PATCHES[FM_PATCHES_COUNT] = {
    { "E.Piano",  {1,14,1,2},   {1.0,0.6,0.3,0.1}, {1.5,0.8,1.5,2.0}, {0.3,0,0.3,0},    0, 0.1f, 6000, 0.7f },
    { "Brass",    {1,1,2,2},    {1.0,0.9,0.7,0.4}, {0.4,0.3,0.4,0.3}, {0.6,0.5,0.6,0.5}, 1, 0.0f, 5000, 1.0f },
    { "Bell",     {1,2.8,5,7},  {1.0,0.6,0.4,0.2}, {2.0,1.0,0.5,0.3}, {0,0,0,0},         6, 0.0f, 8000, 0.5f },
    { "DX Bass",  {1,2,2,3},    {1.0,1.0,0.8,0.3}, {0.1,0.1,0.3,0.5}, {0.3,0.1,0,0},     0, 0.4f, 3000, 2.0f },
    { "Strings",  {1,1,2,3},    {1.0,0.7,0.5,0.3}, {3.0,2.0,1.5,1.0}, {0.7,0.5,0.3,0.2}, 3, 0.0f, 4000, 0.8f },
    { "Marimba",  {1,4,1,1},    {1.0,0.5,0.3,0.1}, {0.3,0.2,0.5,0.5}, {0,0,0,0},         2, 0.0f, 7000, 0.5f },
    { "Lead",     {1,2,3,1},    {1.0,0.3,0.2,0.0}, {1.0,0.5,0.3,0.0}, {0.5,0.3,0.1,0},   5, 0.3f, 6000, 1.0f },
    { "Organ",    {1,2,3,4},    {1.0,0.7,0.5,0.3}, {8.0,8.0,8.0,8.0}, {1,1,1,1},          6, 0.0f, 8000, 0.5f },
    { "FM Pad",   {1,1,2,2},    {1.0,0.8,0.6,0.4}, {4.0,3.0,2.0,1.5}, {0.6,0.5,0.4,0.3}, 3, 0.0f, 4000, 1.0f },
    { "Synth",    {1,3,5,7},    {1.0,0.5,0.3,0.1}, {0.5,0.3,0.2,0.1}, {0.4,0.2,0.1,0},   0, 0.5f, 5000, 1.5f },
    { "Guitar",   {1,2,1,1},    {1.0,0.4,0.2,0.1}, {0.4,0.3,0.8,0.8}, {0,0,0,0},          1, 0.1f, 6000, 0.7f },
    { "Clav",     {1,2,4,8},    {1.0,0.6,0.3,0.1}, {0.2,0.1,0.1,0.1}, {0,0,0,0},          0, 0.0f, 7000, 0.8f },
    { "Choir",    {1,1.5,2,3},  {1.0,0.7,0.5,0.3}, {5.0,4.0,3.0,2.0}, {0.7,0.6,0.5,0.3}, 3, 0.0f, 3500, 1.0f },
    { "FX Sweep", {0.5,1,2,4},  {1.0,0.9,0.6,0.3}, {3.0,2.0,1.0,0.5}, {0.4,0.3,0.2,0.1}, 0, 0.3f, 2500, 2.5f },
    { "Digital",  {1,7,13,17},  {1.0,0.4,0.2,0.1}, {0.3,0.2,0.1,0.1}, {0,0,0,0},          6, 0.0f, 8000, 0.5f },
    { "Sub Bass", {1,0.5,2,3},  {1.0,0.8,0.4,0.2}, {0.2,0.4,0.3,0.2}, {0.5,0.4,0,0},      0, 0.2f, 2000, 2.0f },
};

// ─── Juno patch presets ───────────────────────────────────────────────────────
// These populate SynthParams for UI display; actual sound uses AMY Juno patches.

struct JunoPatch {
    char       name[12];
    float      pwm;          // pulse width 0.02-0.98
    float      sawLevel;     // 0-1
    float      subLevel;     // 0-1
    float      noiseLevel;   // 0-1
    float      hpfCutoff;    // Hz
    float      lpfCutoff;    // Hz
    float      lpfRes;       // Q
    float      lfoRate;      // Hz
    float      lfoFiltDepth; // 0-1
    float      lfoPwmDepth;  // 0-1
    ChorusMode chorus;
    float      attack, decay, sustain, release;
};

static const JunoPatch JUNO_PATCHES[JUNO_PATCHES_COUNT] = {
    { "JunoStrings", 0.5f, 0.8f, 0.3f, 0.0f, 40,  1200, 1.2f, 0.5f, 0.3f, 0.2f, ChorusMode::MODE1, 0.05f,0.5f,0.7f,0.8f },
    { "JunoBrass",   0.3f, 1.0f, 0.0f, 0.0f, 80,  2500, 2.0f, 1.0f, 0.4f, 0.3f, ChorusMode::MODE1, 0.01f,0.3f,0.6f,0.3f },
    { "JunoBass",    0.5f, 0.8f, 0.6f, 0.0f, 40,  800,  2.5f, 0.3f, 0.2f, 0.0f, ChorusMode::OFF,   0.01f,0.4f,0.4f,0.3f },
    { "JunoPad",     0.7f, 0.5f, 0.0f, 0.0f, 20,  900,  1.0f, 0.4f, 0.5f, 0.4f, ChorusMode::MODE2, 0.3f, 0.8f,0.6f,1.2f },
    { "JunoLead",    0.5f, 0.0f, 0.0f, 0.0f, 200, 3000, 1.5f, 2.0f, 0.2f, 0.3f, ChorusMode::OFF,   0.01f,0.2f,0.5f,0.2f },
    { "JunoArp",     0.4f, 0.7f, 0.4f, 0.0f, 60,  1800, 1.8f, 3.0f, 0.3f, 0.2f, ChorusMode::MODE1, 0.01f,0.1f,0.5f,0.1f },
    { "JunoPoly",    0.5f, 0.9f, 0.2f, 0.0f, 40,  2000, 1.0f, 0.6f, 0.2f, 0.2f, ChorusMode::MODE2, 0.02f,0.3f,0.7f,0.5f },
    { "JunoFlute",   0.5f, 0.0f, 0.0f, 0.1f, 500, 4000, 0.8f, 4.0f, 0.3f, 0.0f, ChorusMode::MODE1, 0.05f,0.2f,0.6f,0.4f },
    { "JunoChoir",   0.6f, 0.5f, 0.0f, 0.1f, 60,  1500, 1.5f, 0.3f, 0.6f, 0.5f, ChorusMode::MODE2, 0.1f, 0.5f,0.7f,1.0f },
    { "JunoKeys",    0.5f, 0.8f, 0.0f, 0.0f, 120, 3000, 1.2f, 0.0f, 0.0f, 0.0f, ChorusMode::OFF,   0.01f,0.4f,0.3f,0.5f },
    { "JunoFX",      0.9f, 0.5f, 0.0f, 0.3f, 30,  700,  3.5f, 5.0f, 0.7f, 0.6f, ChorusMode::MODE2, 0.2f, 1.0f,0.4f,2.0f },
    { "JunoPWM",     0.1f, 0.0f, 0.0f, 0.0f, 40,  2500, 1.0f, 1.5f, 0.0f, 0.8f, ChorusMode::MODE1, 0.02f,0.3f,0.6f,0.4f },
    { "JunoSaw",     0.5f, 1.0f, 0.0f, 0.0f, 40,  2000, 1.5f, 0.0f, 0.0f, 0.0f, ChorusMode::MODE2, 0.01f,0.2f,0.8f,0.3f },
    { "JunoSub",     0.5f, 0.5f, 1.0f, 0.0f, 20,  600,  1.8f, 0.2f, 0.1f, 0.0f, ChorusMode::OFF,   0.01f,0.6f,0.5f,0.4f },
    { "JunoNoise",   0.5f, 0.0f, 0.0f, 1.0f, 20,  3000, 2.0f, 0.0f, 0.0f, 0.0f, ChorusMode::OFF,   0.05f,0.5f,0.3f,1.0f },
    { "JunoFull",    0.5f, 0.7f, 0.4f, 0.1f, 40,  1600, 1.5f, 1.0f, 0.3f, 0.3f, ChorusMode::MODE1, 0.02f,0.4f,0.6f,0.5f },
};

// ─── Synth Parameters (saved per pattern) ─────────────────────────────────────

struct SynthParams {
    SynthMode mode        = SynthMode::DUAL_OSC;

    // ── Oscillators ───────────────────────────────────────────────────────────
    WaveType  osc1Wave    = WaveType::WAVE_SAW;
    float     osc1Level   = 1.0f;
    WaveType  osc2Wave    = WaveType::WAVE_SQR;
    float     osc2Level   = 0.5f;
    float     osc2Detune  = 0.07f;
    float     osc2Coarse  = 0.0f;

    // ── Mix ───────────────────────────────────────────────────────────────────
    float     noiseLevel  = 0.0f;
    float     subLevel    = 0.0f;
    float     pulseWidth  = 0.5f;
    float     oscBalance  = 0.5f;

    // ── Filter ────────────────────────────────────────────────────────────────
    float     filterCutoff   = 3000.0f;
    float     filterRes       = 1.0f;
    FilterMode filterMode    = FilterMode::LOWPASS;
    float     filterEnvDepth = 0.5f;
    float     filterKeyTrack = 0.0f;
    float     hpfCutoff      = 20.0f;

    // ── Amp ADSR ─────────────────────────────────────────────────────────────
    float     attack    = 0.005f;
    float     decay     = 0.2f;
    float     sustain   = 0.7f;
    float     release   = 0.3f;

    // ── Filter ADSR ──────────────────────────────────────────────────────────
    float     fEnvAtk   = 0.003f;
    float     fEnvDec   = 0.25f;
    float     fEnvSus   = 0.0f;
    float     fEnvRel   = 0.15f;

    // ── LFO ──────────────────────────────────────────────────────────────────
    WaveType  lfoWave    = WaveType::WAVE_SINE;
    float     lfoRate    = 2.0f;
    float     lfoDepth   = 0.0f;
    LFODest   lfoDest    = LFODest::DEST_NONE;
    float     lfoPwmDepth= 0.0f;

    // ── Portamento ────────────────────────────────────────────────────────────
    float     portaTime  = 0.0f;

    // ── Chorus ────────────────────────────────────────────────────────────────
    ChorusMode chorus    = ChorusMode::OFF;
    float     chorusDepth= 0.004f;
    float     chorusRate = 0.5f;

    // ── FM ────────────────────────────────────────────────────────────────────
    uint8_t   fmAlgo     = 0;
    float     opRatio[NUM_FM_OPS]   = {1,2,3,4};
    float     opLevel[NUM_FM_OPS]   = {1,0.5,0.3,0};
    float     opDecay[NUM_FM_OPS]   = {1,0.5,0.5,0.5};
    float     opSustain[NUM_FM_OPS] = {0.5,0.3,0.2,0.1};
    float     opFeedback = 0.0f;

    // ── Effects ───────────────────────────────────────────────────────────────
    float     reverbAmt    = 0.0f;
    float     delayTime    = 0.0f;
    float     delayFeedback= 0.4f;
    float     masterVol    = 0.8f;

    // ── Patch selectors ───────────────────────────────────────────────────────
    char      name[12]   = "INIT";
    uint8_t   junoPatch  = 0;    // 0-127: AMY Juno patches
    uint8_t   fmPatch    = 0;    // 0-127: AMY DX7 patches (128-255 in AMY)
};

// ─── Synthesis Engine (AMY-backed) ────────────────────────────────────────────

class SynthEngine {
public:
    SynthEngine() {}

    void     begin(float sampleRate = SAMPLE_RATE);
    void     setParams(const SynthParams& p);
    SynthParams& getParams() { return _p; }

    void     noteOn(uint8_t note, uint8_t vel, bool accent = false, bool slide = false);
    void     noteOff(uint8_t note);
    void     allNotesOff();

    // Live parameter delta updates
    void     setFilterCutoff(float hz);
    void     setFilterResonance(float q);
    void     setLFORate(float hz);
    void     setLFODepth(float d);
    void     setReverb(float amt);

    uint8_t  activeVoiceCount() const { return 0; }

private:
    float       _sr = SAMPLE_RATE;
    SynthParams _p;

    void configMode();
    void sendAdsr();
    void sendFilter();
    void sendEffects();
    uint8_t waveTypeToAmy(WaveType wt) const;
};
