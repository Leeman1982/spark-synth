#pragma once
#include <Arduino.h>
#include "config.h"
#include "scales.h"

// ─── Enumerations ─────────────────────────────────────────────────────────────
// NOTE: ANALOG is #defined 0xC0 by esp32-hal-gpio.h — use DUAL_OSC instead.

enum class SynthMode : uint8_t {
    DUAL_OSC = 0, // Dual oscillator analog (AMY custom patch 1024)
    JUNO,         // DCO subtractive — AMY patches 0-127
    FM,           // DX7 FM synthesis — AMY patches 128-255
    BASS,         // Acid bass (TB-303 style, accent + slide)
    PAD,          // Pad / strings — AMY Juno patch, slow ADSR
    KEYS,         // Piano — AMY built-in patch 256
    NUM_MODES
};

enum class WaveType : uint8_t {
    SINE = 0, TRIANGLE, SAW, SQUARE, PULSE, WAVE_NOISE, NUM_WAVES
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

// ─── FM algorithm descriptor (display only) ───────────────────────────────────

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

// ─── FM patch presets (names only — actual sound from AMY DX7 patches) ────────

struct FMPatch {
    char name[12];
    uint8_t algo;
};

static const FMPatch FM_PATCHES[FM_PATCHES_COUNT] = {
    { "E.Piano",   0 }, { "Brass",     1 }, { "Bell",      6 }, { "DX Bass",   0 },
    { "Strings",   3 }, { "Marimba",   2 }, { "Lead",      5 }, { "Organ",     6 },
    { "FM Pad",    3 }, { "Synth",     0 }, { "Guitar",    1 }, { "Clav",      0 },
    { "Choir",     3 }, { "FX Sweep",  0 }, { "Digital",   6 }, { "Sub Bass",  0 },
};

// ─── Juno patch presets (names only — actual sound from AMY Juno patches) ─────

struct JunoPatch {
    char name[12];
};

static const JunoPatch JUNO_PATCHES[JUNO_PATCHES_COUNT] = {
    { "JunoStrings" }, { "JunoBrass"  }, { "JunoBass"   }, { "JunoPad"    },
    { "JunoLead"   }, { "JunoArp"    }, { "JunoPoly"   }, { "JunoFlute"  },
    { "JunoChoir"  }, { "JunoKeys"   }, { "JunoFX"     }, { "JunoPWM"    },
    { "JunoSaw"    }, { "JunoSub"    }, { "JunoNoise"  }, { "JunoFull"   },
};

// ─── Synth Parameters (saved per pattern) ─────────────────────────────────────

struct SynthParams {
    SynthMode mode        = SynthMode::DUAL_OSC;

    // ── Oscillator ────────────────────────────────────────────────────────────
    WaveType  osc1Wave    = WaveType::SAW;
    float     osc1Level   = 1.0f;
    WaveType  osc2Wave    = WaveType::SQUARE;
    float     osc2Level   = 0.5f;
    float     osc2Detune  = 0.07f;   // semitones (fine)
    float     osc2Coarse  = 0.0f;    // semitones (integer)

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
    WaveType  lfoWave    = WaveType::SINE;
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

    // Live parameter updates (send delta event to AMY)
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
