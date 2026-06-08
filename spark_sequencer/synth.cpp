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

// ─── Helper: ms from seconds (1 ms minimum) ───────────────────────────────────

static inline uint16_t secToMs(float sec) {
    return (uint16_t)fmaxf(sec * 1000.0f, 1.0f);
}

// ─── Begin ────────────────────────────────────────────────────────────────────

void SynthEngine::begin(float sr) {
    _sr = sr;
    // AMY is started by the main sketch before begin() is called.
    setParams(_p);
}

// ─── Mode configuration ───────────────────────────────────────────────────────

void SynthEngine::configMode() {
    amy_event e;

    if (_p.mode == SynthMode::DUAL_OSC || _p.mode == SynthMode::BASS) {
        // ── Custom analog patch (1024) ────────────────────────────────────────
        // 1. Reset patch state
        e = amy_default_event();
        e.reset_osc    = RESET_PATCH;
        e.patch_number = 1024;
        amy_add_event(&e);

        // 2. Allocate voices on synth channel
        e = amy_default_event();
        e.synth          = AMY_SYNTH_CH;
        e.patch_number   = 1024;
        e.num_voices     = 8;
        e.oscs_per_voice = 3;
        amy_add_event(&e);

        // 3. Osc 0 — primary wave, follows note pitch, gated by EG0
        uint8_t wave0 = (_p.mode == SynthMode::BASS)
                        ? 1                          // AMY PULSE (integer)
                        : waveToAmy(_p.osc1Wave);
        e = amy_default_event();
        e.synth          = AMY_SYNTH_CH;
        e.osc            = 0;
        e.wave           = wave0;
        e.amp_coefs[0]   = 0.0f;
        e.amp_coefs[1]   = _p.osc1Level * 0.6f;     // velocity scale
        e.amp_coefs[2]   = 1.0f;                     // EG0 amplitude
        e.chained_osc    = 1;
        amy_add_event(&e);

        // 4. Osc 1 — detuned secondary wave
        uint8_t wave1 = (_p.mode == SynthMode::BASS)
                        ? 1                          // AMY PULSE (integer)
                        : waveToAmy(_p.osc2Wave);
        float detuneSemi  = _p.osc2Coarse + _p.osc2Detune;
        float detuneRatio = powf(2.0f, detuneSemi / 12.0f);
        e = amy_default_event();
        e.synth          = AMY_SYNTH_CH;
        e.osc            = 1;
        e.wave           = wave1;
        e.amp_coefs[0]   = 0.0f;
        e.amp_coefs[1]   = _p.osc2Level * 0.6f;
        e.amp_coefs[2]   = 1.0f;
        e.freq_coefs[0]  = 0.0f;
        e.freq_coefs[1]  = detuneRatio;               // ratio relative to note pitch
        e.chained_osc    = 2;
        amy_add_event(&e);

        // 5. Osc 2 — noise layer
        e = amy_default_event();
        e.synth          = AMY_SYNTH_CH;
        e.osc            = 2;
        e.wave           = 5;                         // AMY NOISE (integer)
        e.amp_coefs[0]   = fmaxf(_p.noiseLevel * 0.3f, 0.001f);
        e.amp_coefs[1]   = 0.3f;
        e.amp_coefs[2]   = 1.0f;
        amy_add_event(&e);

    } else {
        // ── Patch-based mode ─────────────────────────────────────────────────
        e = amy_default_event();
        e.synth = AMY_SYNTH_CH;

        switch (_p.mode) {
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
}

// ─── ADSR (amplitude envelope via EG0) ───────────────────────────────────────

void SynthEngine::sendAdsr() {
    if (_p.mode == SynthMode::FM) return;   // DX7 uses built-in operator envelopes

    amy_event e = amy_default_event();
    e.synth = AMY_SYNTH_CH;

    e.eg0_times[0]  = secToMs(_p.attack);
    e.eg0_values[0] = 1.0f;

    e.eg0_times[1]  = secToMs(_p.decay);
    e.eg0_values[1] = _p.sustain;

    // AMY waits for note-off before starting the final release segment
    e.eg0_times[2]  = secToMs(_p.release);
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
