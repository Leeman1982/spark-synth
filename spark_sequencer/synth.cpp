#include "synth.h"

extern "C" {
#include <amy.h>
}

// AMY synth channel used for all voices
#define AMY_SYNTH_CH  1

// ─── Wave type mapping ────────────────────────────────────────────────────────
// AMY wave_t integers: SINE=0, PULSE=1, SAW_DOWN=2, SAW_UP=3, TRIANGLE=4, NOISE=5
//
// Case labels use explicit casts to uint8_t so that AMY's #define macros
// (SINE 0, PULSE 1, TRIANGLE 4, etc.) do not expand inside the case expression.

static uint8_t waveToAmy(WaveType wt) {
    switch ((uint8_t)wt) {
        case (uint8_t)WaveType::WAVE_SINE:  return 0;   // AMY SINE
        case (uint8_t)WaveType::WAVE_TRI:   return 4;   // AMY TRIANGLE
        case (uint8_t)WaveType::WAVE_SAW:   return 2;   // AMY SAW_DOWN
        case (uint8_t)WaveType::WAVE_SQR:   return 1;   // AMY PULSE (square)
        case (uint8_t)WaveType::WAVE_PULSE: return 1;   // AMY PULSE
        case (uint8_t)WaveType::WAVE_NOISE: return 5;   // AMY NOISE
        default:                             return 0;
    }
}

// ─── Helper: clamp envelope time to 1 ms minimum ─────────────────────────────

static inline float clampEnvSec(float sec) {
    return fmaxf(sec, 0.001f);
}

// ─── Begin ────────────────────────────────────────────────────────────────────

void SynthEngine::begin(float sr) {
    _sr = sr;
    // AMY is started by the main sketch before begin() is called.
    setParams(_p);
}

// ─── Mode configuration ───────────────────────────────────────────────────────

void SynthEngine::configMode() {
    amy_event e = amy_default_event();
    e.synth = AMY_SYNTH_CH;

    switch (_p.mode) {
        case SynthMode::DUAL_OSC:
            e.num_voices   = 8;
            e.patch_number = 0;    // Juno saw — analog dual-osc character
            break;
        case SynthMode::BASS:
            e.num_voices   = 8;
            e.patch_number = 0;    // Juno saw — acid bass foundation
            break;
        case SynthMode::JUNO:
            e.num_voices   = 6;
            e.patch_number = _p.junoPatch;               // AMY patches 0-127
            break;
        case SynthMode::FM:
            e.num_voices   = 8;
            e.patch_number = (uint16_t)_p.fmPatch + 128; // AMY patches 128-255
            break;
        case SynthMode::KEYS:
            e.num_voices   = 8;
            e.patch_number = 256;                         // AMY built-in piano
            break;
        case SynthMode::PAD:
            e.num_voices   = 8;
            e.patch_number = _p.junoPatch;
            break;
        default:
            e.num_voices   = 8;
            e.patch_number = 0;
            break;
    }
    amy_add_event(&e);
}

// ─── ADSR (amplitude envelope via EG0) ───────────────────────────────────────

void SynthEngine::sendAdsr() {
    if (_p.mode == SynthMode::FM) return;   // DX7 uses built-in operator envelopes

    amy_event e = amy_default_event();
    e.synth = AMY_SYNTH_CH;

    e.eg0_times[0]  = clampEnvSec(_p.attack);
    e.eg0_values[0] = 1.0f;

    e.eg0_times[1]  = clampEnvSec(_p.decay);
    e.eg0_values[1] = _p.sustain;

    // AMY waits for note-off before starting the final release segment
    e.eg0_times[2]  = clampEnvSec(_p.release);
    e.eg0_values[2] = 0.0f;

    amy_add_event(&e);
}

// ─── Filter ───────────────────────────────────────────────────────────────────

void SynthEngine::sendFilter() {
    if (_p.mode == SynthMode::FM) return;   // FM patches manage their own timbre

    amy_event e = amy_default_event();
    e.synth = AMY_SYNTH_CH;

    e.filter_freq_coefs[0] = _p.filterCutoff;
    e.resonance            = _p.filterRes;

    // AMY filter_type: 0=none, 1=LP, 2=BP, 3=HP, 4=double-LP
    switch (_p.filterMode) {
        case FilterMode::LOWPASS:  e.filter_type = 1; break;
        case FilterMode::BANDPASS: e.filter_type = 2; break;
        case FilterMode::HIGHPASS: e.filter_type = 3; break;
        case FilterMode::NOTCH:    e.filter_type = 1; break;
    }

    amy_add_event(&e);
}

// ─── Effects ──────────────────────────────────────────────────────────────────
// config_reverb(bus, level, liveness, damping, xover_hz)
// config_echo  (bus, level, delay_ms, max_delay_ms, feedback, filter_coef)

void SynthEngine::sendEffects() {
    if (_p.reverbAmt > 0.01f) {
        config_reverb(0, _p.reverbAmt * 2.0f, 0.85f, 0.5f, 3000.0f);
    } else {
        config_reverb(0, 0.0f, 0.85f, 0.5f, 3000.0f);
    }

    if (_p.delayTime > 0.01f) {
        float delayMs = _p.delayTime * 1000.0f;
        config_echo(0, _p.delayFeedback * 0.8f, delayMs, 3000.0f, _p.delayFeedback, 0.0f);
    } else {
        config_echo(0, 0.0f, 500.0f, 3000.0f, 0.0f, 0.0f);
    }

    // Master volume via synth volume coefficient array
    amy_event e = amy_default_event();
    e.synth      = AMY_SYNTH_CH;
    e.volume[0]  = _p.masterVol * 4.0f;
    amy_add_event(&e);
}

// ─── Apply full parameter set ─────────────────────────────────────────────────

void SynthEngine::setParams(const SynthParams& p) {
    _p = p;
    configMode();
    sendAdsr();
    sendFilter();
    sendEffects();
}

// Re-send envelope/filter/effects without re-running configMode() —
// use for live parameter tweaks where voice reallocation would glitch.
void SynthEngine::refresh() {
    sendAdsr();
    sendFilter();
    sendEffects();
}

// ─── Note on / off ────────────────────────────────────────────────────────────

void SynthEngine::noteOn(uint8_t note, uint8_t vel, bool accent, bool slide) {
    float velocity = vel / 127.0f;
    if (accent) velocity = fminf(velocity + 0.25f, 1.0f);

    amy_event e = amy_default_event();
    e.synth     = AMY_SYNTH_CH;
    e.midi_note = note;
    e.velocity  = velocity;
    amy_add_event(&e);
}

void SynthEngine::noteOff(uint8_t note) {
    amy_event e = amy_default_event();
    e.synth     = AMY_SYNTH_CH;
    e.midi_note = note;
    e.velocity  = 0.0f;
    amy_add_event(&e);
}

void SynthEngine::allNotesOff() {
    amy_event e = amy_default_event();
    e.synth    = AMY_SYNTH_CH;
    e.velocity = 0.0f;
    amy_add_event(&e);
}

// ─── Live parameter delta updates ─────────────────────────────────────────────

void SynthEngine::setFilterCutoff(float hz) {
    _p.filterCutoff = hz;
    sendFilter();
}

void SynthEngine::setFilterResonance(float q) {
    _p.filterRes = q;
    sendFilter();
}

void SynthEngine::setLFORate(float hz)  { _p.lfoRate  = hz; }
void SynthEngine::setLFODepth(float d)  { _p.lfoDepth = d; }

void SynthEngine::setReverb(float amt) {
    _p.reverbAmt = amt;
    sendEffects();
}

uint8_t SynthEngine::waveTypeToAmy(WaveType wt) const {
    return waveToAmy(wt);
}
