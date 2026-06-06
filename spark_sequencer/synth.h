#pragma once
#include <Arduino.h>
#include <math.h>
#include "config.h"
#include "scales.h"

// ─── Enumerations ─────────────────────────────────────────────────────────────

enum class SynthMode : uint8_t {
    ANALOG = 0,  // Dual oscillator analog (InstrumentAnalog inspired)
    JUNO,        // DCO+PWM+Sub+noise subtractive (InstrumentJuno inspired)
    FM,          // 4-operator FM synthesis (InstrumentDX7 inspired)
    BASS,        // Acid bass (TB-303 style — accent + slide)
    PAD,         // Supersaw pads
    KEYS,        // Karplus-Strong plucked string
    NUM_MODES
};

enum class WaveType : uint8_t {
    SINE = 0, TRIANGLE, SAW, SQUARE, PULSE, NOISE, NUM_WAVES
};

enum class LFODest : uint8_t {
    NONE = 0, FILTER, PITCH, AMP, PWM, NUM_DESTS
};

enum class FilterMode : uint8_t {
    LOWPASS = 0, BANDPASS, HIGHPASS, NOTCH
};

enum class ChorusMode : uint8_t {
    OFF = 0, MODE1, MODE2
};

// ─── FM algorithm descriptor ──────────────────────────────────────────────────

struct FMAlgo {
    const char name[6];
    bool isCarrier[NUM_FM_OPS];   // true = output goes to mix
    int8_t modSource[NUM_FM_OPS]; // which op modulates this one (-1=none, op index)
    bool feedback;                // self-feedback on op[0]
};

// 8 practical FM algorithms (simplified DX7 inspired)
static const FMAlgo FM_ALGOS[8] = {
    // 0: linear chain 4→3→2→1
    { "4-CHN", {true,false,false,false}, {1,2,3,-1}, false },
    // 1: [4→3→2→1], [4→1] — 2 carriers
    { "2CRY1", {true,false,false,false}, {-1,2,3,-1}, false },
    // 2: [4,3,2]→1  — 3 mods into 1 carrier
    { "3MOD1", {true,false,false,false}, {-1,-1,-1,-1}, false },
    // 3: [4→2→1], [3→1] — 2 mod chains into 1 carrier
    { "2CH1C", {true,false,false,false}, {1,2,-1,-1}, false },
    // 4: [4→1], [3→2] — 2 pairs carriers
    { "2PAIR", {true,true,false,false}, {3,-1,-1,-1}, false },
    // 5: [4→1,2,3] — 1 mod into 3 carriers
    { "1TO3C", {true,true,true,false}, {3,3,3,-1}, false },
    // 6: all carriers (additive FM)
    { "ADDTV", {true,true,true,true}, {-1,-1,-1,-1}, false },
    // 7: feedback on op0, stack on 1&2, carrier 3
    { "FDBK",  {false,false,false,true},{-1,-1,-1,0}, true },
};

// ─── FM patch presets ─────────────────────────────────────────────────────────

struct FMPatch {
    char     name[12];
    float    opRatio[NUM_FM_OPS];    // frequency ratio vs note
    float    opLevel[NUM_FM_OPS];    // modulation index / carrier level
    float    opDecay[NUM_FM_OPS];    // operator decay time (s)
    float    opSustain[NUM_FM_OPS];  // operator sustain level
    uint8_t  algo;
    float    feedback;
    float    filterCutoff;
    float    filterRes;
};

static const FMPatch FM_PATCHES[FM_PATCHES_COUNT] = {
    { "E.Piano",   {1,14,1,2},    {1.0,0.6,0.3,0.1}, {1.5,0.8,1.5,2.0}, {0.3,0,0.3,0},   0, 0.1, 6000, 0.7 },
    { "Brass",     {1,1,2,2},     {1.0,0.9,0.7,0.4}, {0.4,0.3,0.4,0.3}, {0.6,0.5,0.6,0.5},1, 0.0, 5000, 1.0 },
    { "Bell",      {1,2.8,5,7},   {1.0,0.6,0.4,0.2}, {2.0,1.0,0.5,0.3}, {0,0,0,0},        6, 0.0, 8000, 0.5 },
    { "DX Bass",   {1,2,2,3},     {1.0,1.0,0.8,0.3}, {0.1,0.1,0.3,0.5}, {0.3,0.1,0,0},    0, 0.4, 3000, 2.0 },
    { "Strings",   {1,1,2,3},     {1.0,0.7,0.5,0.3}, {3.0,2.0,1.5,1.0}, {0.7,0.5,0.3,0.2},3, 0.0, 4000, 0.8 },
    { "Marimba",   {1,4,1,1},     {1.0,0.5,0.3,0.1}, {0.3,0.2,0.5,0.5}, {0,0,0,0},        2, 0.0, 7000, 0.5 },
    { "Lead",      {1,2,3,1},     {1.0,0.3,0.2,0.0}, {1.0,0.5,0.3,0.0}, {0.5,0.3,0.1,0},  5, 0.3, 6000, 1.0 },
    { "Organ",     {1,2,3,4},     {1.0,0.7,0.5,0.3}, {8.0,8.0,8.0,8.0}, {1,1,1,1},         6, 0.0, 8000, 0.5 },
    { "FM Pad",    {1,1,2,2},     {1.0,0.8,0.6,0.4}, {4.0,3.0,2.0,1.5}, {0.6,0.5,0.4,0.3},3, 0.0, 4000, 1.0 },
    { "Synth",     {1,3,5,7},     {1.0,0.5,0.3,0.1}, {0.5,0.3,0.2,0.1}, {0.4,0.2,0.1,0},  0, 0.5, 5000, 1.5 },
    { "Guitar",    {1,2,1,1},     {1.0,0.4,0.2,0.1}, {0.4,0.3,0.8,0.8}, {0,0,0,0},         1, 0.1, 6000, 0.7 },
    { "Clav",      {1,2,4,8},     {1.0,0.6,0.3,0.1}, {0.2,0.1,0.1,0.1}, {0,0,0,0},         0, 0.0, 7000, 0.8 },
    { "Choir",     {1,1.5,2,3},   {1.0,0.7,0.5,0.3}, {5.0,4.0,3.0,2.0}, {0.7,0.6,0.5,0.3},3, 0.0, 3500, 1.0 },
    { "FX Sweep",  {0.5,1,2,4},   {1.0,0.9,0.6,0.3}, {3.0,2.0,1.0,0.5}, {0.4,0.3,0.2,0.1},0, 0.3, 2500, 2.5 },
    { "Digital",   {1,7,13,17},   {1.0,0.4,0.2,0.1}, {0.3,0.2,0.1,0.1}, {0,0,0,0},         6, 0.0, 8000, 0.5 },
    { "Sub Bass",  {1,0.5,2,3},   {1.0,0.8,0.4,0.2}, {0.2,0.4,0.3,0.2}, {0.5,0.4,0,0},     0, 0.2, 2000, 2.0 },
};

// ─── Juno patch presets ───────────────────────────────────────────────────────

struct JunoPatch {
    char    name[12];
    float   pwm;          // pulse width 0.02-0.98
    float   sawLevel;     // 0-1
    float   subLevel;     // 0-1
    float   noiseLevel;   // 0-1
    float   hpfCutoff;    // Hz
    float   lpfCutoff;    // Hz
    float   lpfRes;       // Q
    float   lfoRate;      // Hz
    float   lfoFiltDepth; // 0-1
    float   lfoPwmDepth;  // 0-1
    ChorusMode chorus;
    float   attack, decay, sustain, release;
};

static const JunoPatch JUNO_PATCHES[JUNO_PATCHES_COUNT] = {
    { "JunoStrings",0.5, 0.8, 0.3, 0.0, 40,  1200, 1.2, 0.5, 0.3, 0.2, ChorusMode::MODE1, 0.05,0.5, 0.7,0.8 },
    { "JunoBrass",  0.3, 1.0, 0.0, 0.0, 80,  2500, 2.0, 1.0, 0.4, 0.3, ChorusMode::MODE1, 0.01,0.3, 0.6,0.3 },
    { "JunoBass",   0.5, 0.8, 0.6, 0.0, 40,  800,  2.5, 0.3, 0.2, 0.0, ChorusMode::OFF,   0.01,0.4, 0.4,0.3 },
    { "JunoPad",    0.7, 0.5, 0.0, 0.0, 20,  900,  1.0, 0.4, 0.5, 0.4, ChorusMode::MODE2, 0.3, 0.8, 0.6,1.2 },
    { "JunoLead",   0.5, 0.0, 0.0, 0.0, 200, 3000, 1.5, 2.0, 0.2, 0.3, ChorusMode::OFF,   0.01,0.2, 0.5,0.2 },
    { "JunoArp",    0.4, 0.7, 0.4, 0.0, 60,  1800, 1.8, 3.0, 0.3, 0.2, ChorusMode::MODE1, 0.01,0.1, 0.5,0.1 },
    { "JunoPoly",   0.5, 0.9, 0.2, 0.0, 40,  2000, 1.0, 0.6, 0.2, 0.2, ChorusMode::MODE2, 0.02,0.3, 0.7,0.5 },
    { "JunoFlute",  0.5, 0.0, 0.0, 0.1, 500, 4000, 0.8, 4.0, 0.3, 0.0, ChorusMode::MODE1, 0.05,0.2, 0.6,0.4 },
    { "JunoChoir",  0.6, 0.5, 0.0, 0.1, 60,  1500, 1.5, 0.3, 0.6, 0.5, ChorusMode::MODE2, 0.1, 0.5, 0.7,1.0 },
    { "JunoKeys",   0.5, 0.8, 0.0, 0.0, 120, 3000, 1.2, 0.0, 0.0, 0.0, ChorusMode::OFF,   0.01,0.4, 0.3,0.5 },
    { "JunoFX",     0.9, 0.5, 0.0, 0.3, 30,  700,  3.5, 5.0, 0.7, 0.6, ChorusMode::MODE2, 0.2, 1.0, 0.4,2.0 },
    { "JunoPWM",    0.1, 0.0, 0.0, 0.0, 40,  2500, 1.0, 1.5, 0.0, 0.8, ChorusMode::MODE1, 0.02,0.3, 0.6,0.4 },
    { "JunoSaw",    0.5, 1.0, 0.0, 0.0, 40,  2000, 1.5, 0.0, 0.0, 0.0, ChorusMode::MODE2, 0.01,0.2, 0.8,0.3 },
    { "JunoSub",    0.5, 0.5, 1.0, 0.0, 20,  600,  1.8, 0.2, 0.1, 0.0, ChorusMode::OFF,   0.01,0.6, 0.5,0.4 },
    { "JunoNoise",  0.5, 0.0, 0.0, 1.0, 20,  3000, 2.0, 0.0, 0.0, 0.0, ChorusMode::OFF,   0.05,0.5, 0.3,1.0 },
    { "JunoFull",   0.5, 0.7, 0.4, 0.1, 40,  1600, 1.5, 1.0, 0.3, 0.3, ChorusMode::MODE1, 0.02,0.4, 0.6,0.5 },
};

// ─── Synth Parameters (one patch, saved per pattern) ─────────────────────────

struct SynthParams {
    SynthMode mode        = SynthMode::ANALOG;

    // ── Oscillator 1 ─────────────────────────────────────────────────────────
    WaveType  osc1Wave    = WaveType::SAW;
    float     osc1Level   = 1.0f;

    // ── Oscillator 2 ─────────────────────────────────────────────────────────
    WaveType  osc2Wave    = WaveType::SQUARE;
    float     osc2Level   = 0.5f;
    float     osc2Detune  = 0.07f;   // semitones (fine)
    float     osc2Coarse  = 0.0f;    // semitones (integer)

    // ── Mix ───────────────────────────────────────────────────────────────────
    float     noiseLevel  = 0.0f;
    float     subLevel    = 0.0f;    // Juno sub / BASS fundamentals
    float     pulseWidth  = 0.5f;    // for PULSE / Juno PWM
    float     oscBalance  = 0.5f;    // ANALOG: 0=osc1 only, 1=osc2 only

    // ── Filter ────────────────────────────────────────────────────────────────
    float     filterCutoff  = 3000.0f;
    float     filterRes      = 1.0f;
    FilterMode filterMode   = FilterMode::LOWPASS;
    float     filterEnvDepth= 0.5f;   // amount filter env opens cutoff
    float     filterKeyTrack= 0.0f;   // 0-1 (full key tracking)
    float     hpfCutoff     = 20.0f;  // Juno HPF

    // ── Amp ADSR ─────────────────────────────────────────────────────────────
    float     attack    = 0.005f;
    float     decay     = 0.2f;
    float     sustain   = 0.7f;
    float     release   = 0.3f;

    // ── Filter ADSR ──────────────────────────────────────────────────────────
    float     fEnvAtk   = 0.003f;
    float     fEnvDec   = 0.25f;
    float     fEnvSus   = 0.0f;   // TB-303 default: no sustain
    float     fEnvRel   = 0.15f;

    // ── LFO ──────────────────────────────────────────────────────────────────
    WaveType  lfoWave   = WaveType::SINE;
    float     lfoRate   = 2.0f;
    float     lfoDepth  = 0.0f;
    LFODest   lfoDest   = LFODest::FILTER;
    float     lfoPwmDepth= 0.0f;

    // ── Portamento ────────────────────────────────────────────────────────────
    float     portaTime = 0.0f;   // 0=off

    // ── Chorus (Juno mode) ────────────────────────────────────────────────────
    ChorusMode chorus   = ChorusMode::OFF;
    float     chorusDepth=0.004f; // max delay mod depth (seconds)
    float     chorusRate= 0.5f;

    // ── FM (FM mode) ─────────────────────────────────────────────────────────
    uint8_t   fmAlgo     = 0;
    float     opRatio[NUM_FM_OPS]   = {1,2,3,4};
    float     opLevel[NUM_FM_OPS]   = {1,0.5,0.3,0};
    float     opDecay[NUM_FM_OPS]   = {1,0.5,0.5,0.5};
    float     opSustain[NUM_FM_OPS] = {0.5,0.3,0.2,0.1};
    float     opFeedback = 0.0f;

    // ── Effects ───────────────────────────────────────────────────────────────
    float     reverbAmt  = 0.0f;
    float     delayTime  = 0.0f;   // seconds
    float     delayFeedback=0.4f;
    float     masterVol  = 0.8f;

    // ── Patch name ────────────────────────────────────────────────────────────
    char      name[12]   = "INIT";

    // ── Juno patch index ─────────────────────────────────────────────────────
    uint8_t   junoPatch  = 0;
    uint8_t   fmPatch    = 0;
};

// ─── ADSR Envelope ───────────────────────────────────────────────────────────

struct ADSR {
    enum class State : uint8_t { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE } state = State::IDLE;
    float level = 0;
    float attackCoef, decayCoef, sustainLevel, releaseCoef;

    void setParams(float atk, float dec, float sus, float rel, float sr) {
        // exponential coefficients — smoother than linear
        auto coef = [](float t, float sr) {
            return (t < 0.001f) ? 0.0f : expf(-1.0f / (t * sr));
        };
        attackCoef   = coef(atk, sr);
        decayCoef    = coef(dec, sr);
        sustainLevel = sus;
        releaseCoef  = coef(rel, sr);
    }

    void noteOn()  { state = State::ATTACK; }
    void noteOff() { if (state != State::IDLE) state = State::RELEASE; }
    bool isIdle()  const { return state == State::IDLE; }

    float process() {
        switch(state) {
            case State::ATTACK:
                level = 1.0f - (1.0f - level) * attackCoef;
                if (level >= 0.9999f) { level = 1.0f; state = State::DECAY; }
                break;
            case State::DECAY:
                level = sustainLevel + (level - sustainLevel) * decayCoef;
                if (fabsf(level - sustainLevel) < 0.0001f) { level = sustainLevel; state = State::SUSTAIN; }
                break;
            case State::SUSTAIN:
                break;
            case State::RELEASE:
                level *= releaseCoef;
                if (level < 0.00005f) { level = 0; state = State::IDLE; }
                break;
            default: break;
        }
        return level;
    }
};

// ─── State Variable Filter (Chamberlin) ──────────────────────────────────────

struct SVF {
    float low = 0, band = 0, high = 0, notch = 0;
    float f = 0, q = 0;

    void setParams(float cutoff, float resonance, float sr) {
        float fc = constrain(cutoff, 20.0f, sr * 0.45f);
        f = 2.0f * sinf(M_PI * fc / sr);
        q = 1.0f / constrain(resonance, 0.5f, 20.0f);
    }

    float process(float in, FilterMode mode) {
        // double-sampled for stability at high resonance
        for(int i = 0; i < 2; i++) {
            low  += f * band;
            high  = in - low - q * band;
            band += f * high;
            notch = high + low;
        }
        switch(mode) {
            case FilterMode::LOWPASS:  return low;
            case FilterMode::HIGHPASS: return high;
            case FilterMode::BANDPASS: return band;
            case FilterMode::NOTCH:    return notch;
        }
        return low;
    }

    void reset() { low = band = high = notch = 0; }
};

// ─── Polyphonic Voice ─────────────────────────────────────────────────────────

struct Voice {
    bool    active   = false;
    uint8_t note     = 60;
    uint8_t velocity = 100;
    bool    accent   = false;
    bool    slide    = false;

    float   velScale = 0.8f;
    float   freq     = 440.0f;    // target freq (may differ from current during porta)
    float   currentFreq = 440.0f; // freq actually playing (porta tracking)

    // Oscillator phases
    float   phase1 = 0, phase2 = 0, phaseSub = 0;
    float   phaseInc1, phaseInc2, phaseIncSub;

    // FM operators
    float   opPhase[NUM_FM_OPS]   = {};
    float   opOut[NUM_FM_OPS]     = {};
    float   opEnvLevel[NUM_FM_OPS]= {};
    ADSR    opEnv[NUM_FM_OPS];

    // Envelopes
    ADSR    ampEnv;
    ADSR    filterEnv;

    // Filters
    SVF     lpf;
    SVF     hpf;

    // LFO phase (per-voice, staggered to reduce phasing artifacts)
    float   lfoPhase = 0;

    // Noise state
    uint32_t noiseSeed = 0;

    // Chorus delay lines (per-voice, Juno chorus — ~11ms @ 44100)
    float   chorusBuf[512] = {};
    int     chorusIdx = 0;

    // Karplus-Strong (KEYS mode)
    float   ksBuf[KS_BUF_MAX]  = {};
    int     ksIdx   = 0;
    int     ksBufLen = 100;
    float   ksLP    = 0;  // low-pass filter state for KS

    // PAD supersaw — 6 detuned oscillator phases
    float   sawPhases[6] = {};

    uint32_t age = 0;  // for voice stealing
};

// ─── Synthesis Engine ─────────────────────────────────────────────────────────

class SynthEngine {
public:
    SynthEngine() {}

    void     begin(float sampleRate = SAMPLE_RATE);
    void     setParams(const SynthParams& p);
    SynthParams& getParams() { return _p; }

    void     noteOn(uint8_t note, uint8_t vel, bool accent = false, bool slide = false);
    void     noteOff(uint8_t note);
    void     allNotesOff();

    // Fill stereo interleaved int16_t — called from audio task
    void     process(int16_t* out, int frames);

    // Live knob targets (thread-safe for integer assignments)
    void     setFilterCutoff(float hz)   { _p.filterCutoff = hz; }
    void     setFilterResonance(float q) { _p.filterRes = q; }
    void     setLFORate(float hz)        { _p.lfoRate = hz; }
    void     setLFODepth(float d)        { _p.lfoDepth = d; }
    void     setReverb(float amt)        { _p.reverbAmt = amt; }

    uint8_t  activeVoiceCount() const;

private:
    float       _sr = SAMPLE_RATE;
    SynthParams _p;
    Voice       _voices[NUM_VOICES];
    uint32_t    _age = 0;

    // Global LFO
    float _lfoPhase = 0;

    // Reverb (Freeverb-lite: 6 comb + 2 allpass)
    float _combBuf[REVERB_COMBS][2000]  = {};
    int   _combIdx[REVERB_COMBS]        = {};
    int   _combLen[REVERB_COMBS]        = {};
    float _combFilter[REVERB_COMBS]     = {};
    float _apBuf[REVERB_ALLPASS][600]   = {};
    int   _apIdx[REVERB_ALLPASS]        = {};
    int   _apLen[REVERB_ALLPASS]        = {};

    // Delay line
    float _delayBuf[DELAY_BUF_LEN] = {};
    int   _delayWr  = 0;

    int   allocVoice(uint8_t note, bool slide);
    float sampleVoice(Voice& v);
    float oscillator(float& phase, float inc, WaveType type, float pw, uint32_t& seed);
    float processFM(Voice& v);
    float processKS(Voice& v);
    float processReverb(float in);
    void  initReverb();
    float lfoSample(float phase, WaveType type);
    float noise(uint32_t& seed);
};
