#include "synth.h"
#include <string.h>   // memset, memcpy

static const float INV_SR_DEFAULT = 1.0f / SAMPLE_RATE;
// TWO_PI is provided by Arduino.h — do not redefine

// ─── Reverb comb filter lengths (prime-ish, fit inside 2000-element buffer) ───
static const int COMB_LENGTHS[REVERB_COMBS]    = { 1117, 1187, 1061, 997, 901, 983 };
static const int ALLPASS_LENGTHS[REVERB_ALLPASS]= { 211, 449 };
static constexpr float REVERB_DAMP   = 0.5f;
static constexpr float REVERB_ROOM   = 0.84f;
static constexpr float REVERB_SPREAD = 23;

// Supersaw detune offsets (semitones for 6 voices)
static const float SAW_DETUNE[6] = { -0.13f, -0.08f, -0.04f, 0.04f, 0.08f, 0.13f };

// ─── Begin / init ─────────────────────────────────────────────────────────────

void SynthEngine::begin(float sr) {
    _sr = sr;
    memset(_voices, 0, sizeof(_voices));
    memset(_delayBuf, 0, sizeof(_delayBuf));
    initReverb();

    // Stagger LFO start phases across voices to prevent phasing
    for (int i = 0; i < NUM_VOICES; i++)
        _voices[i].noiseSeed = 0xDEAD0000u + i * 0x7F3Bu;
}

void SynthEngine::initReverb() {
    for (int i = 0; i < REVERB_COMBS; i++) {
        _combLen[i]    = COMB_LENGTHS[i];
        _combIdx[i]    = 0;
        _combFilter[i] = 0.0f;
        memset(_combBuf[i], 0, sizeof(float) * _combLen[i]);
    }
    for (int i = 0; i < REVERB_ALLPASS; i++) {
        _apLen[i] = ALLPASS_LENGTHS[i];
        _apIdx[i] = 0;
        memset(_apBuf[i], 0, sizeof(float) * _apLen[i]);
    }
}

void SynthEngine::setParams(const SynthParams& p) {
    _p = p;
}

// ─── Voice allocation ─────────────────────────────────────────────────────────

int SynthEngine::allocVoice(uint8_t note, bool slide) {
    // Reuse voice playing same note
    for (int i = 0; i < NUM_VOICES; i++) {
        if (_voices[i].active && _voices[i].note == note) return i;
    }
    // Look for idle voice
    for (int i = 0; i < NUM_VOICES; i++) {
        if (!_voices[i].active || _voices[i].ampEnv.isIdle()) return i;
    }
    // Steal oldest voice
    int oldest = 0;
    uint32_t minAge = _voices[0].age;
    for (int i = 1; i < NUM_VOICES; i++) {
        if (_voices[i].age < minAge) { minAge = _voices[i].age; oldest = i; }
    }
    return oldest;
}

// ─── Note on / off ────────────────────────────────────────────────────────────

void SynthEngine::noteOn(uint8_t note, uint8_t vel, bool accent, bool slide) {
    int vi = allocVoice(note, slide);
    Voice& v = _voices[vi];

    float freq = midiToFreq(note);

    // Portamento: if slide and already playing, glide from current freq
    // v.freq = target; v.currentFreq tracks toward it each sample
    if (!(slide && v.active)) {
        v.currentFreq = freq;   // instant jump when not sliding
    }
    v.freq    = freq;
    v.note    = note;
    v.velocity= vel;
    v.accent  = accent;
    v.slide   = slide;
    v.age     = ++_age;
    v.velScale= 0.3f + 0.7f * (vel / 127.0f);
    v.active  = true;

    // Set ADSR params
    float atkMult = accent ? 1.0f : 1.0f;
    float decMult = accent ? 0.5f : 1.0f;  // accent = shorter filter decay (acid style)

    v.ampEnv.setParams(_p.attack, _p.decay, _p.sustain, _p.release, _sr);
    v.filterEnv.setParams(_p.fEnvAtk, _p.fEnvDec * decMult, _p.fEnvSus, _p.fEnvRel, _sr);
    v.ampEnv.noteOn();
    v.filterEnv.noteOn();

    // FM operator envelopes
    if (_p.mode == SynthMode::FM) {
        for (int op = 0; op < NUM_FM_OPS; op++) {
            v.opEnv[op].setParams(0.002f, _p.opDecay[op], _p.opSustain[op], 0.3f, _sr);
            v.opEnv[op].noteOn();
            v.opPhase[op] = 0;
        }
    }

    // Karplus-Strong initialisation (KEYS mode)
    if (_p.mode == SynthMode::KEYS) {
        v.ksBufLen = constrain((int)(_sr / freq), 2, KS_BUF_MAX);
        // Fill with band-limited noise
        for (int i = 0; i < v.ksBufLen; i++) {
            v.ksBuf[i] = noise(v.noiseSeed) * v.velScale;
        }
        v.ksIdx = 0;
        v.ksLP  = 0;
    }

    // Reset filter state for non-slide
    if (!slide) {
        v.lpf.reset();
        v.hpf.reset();
    }

    // Supersaw phase init (PAD mode)
    if (_p.mode == SynthMode::PAD) {
        for (int i = 0; i < 6; i++) v.sawPhases[i] = (float)i / 6.0f;
    }
}

void SynthEngine::noteOff(uint8_t note) {
    for (int i = 0; i < NUM_VOICES; i++) {
        if (_voices[i].active && _voices[i].note == note) {
            _voices[i].ampEnv.noteOff();
            _voices[i].filterEnv.noteOff();
            for (int op = 0; op < NUM_FM_OPS; op++) _voices[i].opEnv[op].noteOff();
        }
    }
}

void SynthEngine::allNotesOff() {
    for (int i = 0; i < NUM_VOICES; i++) {
        _voices[i].ampEnv.noteOff();
        _voices[i].filterEnv.noteOff();
    }
}

uint8_t SynthEngine::activeVoiceCount() const {
    uint8_t n = 0;
    for (int i = 0; i < NUM_VOICES; i++) if (_voices[i].active) n++;
    return n;
}

// ─── Primitive generators ─────────────────────────────────────────────────────

float SynthEngine::noise(uint32_t& seed) {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return (int32_t)seed * (1.0f / 2147483648.0f);
}

float SynthEngine::oscillator(float& phase, float inc, WaveType type, float pw, uint32_t& seed) {
    float out = 0;
    switch(type) {
        case WaveType::SINE:
            out = sinf(phase * TWO_PI);
            break;
        case WaveType::TRIANGLE:
            out = (phase < 0.5f) ? (4.0f * phase - 1.0f) : (3.0f - 4.0f * phase);
            break;
        case WaveType::SAW:
            out = phase * 2.0f - 1.0f;
            break;
        case WaveType::SQUARE:
            out = (phase < 0.5f) ? 1.0f : -1.0f;
            break;
        case WaveType::PULSE:
            pw = constrain(pw, 0.02f, 0.98f);
            out = (phase < pw) ? 1.0f : -1.0f;
            break;
        case WaveType::WAVE_NOISE:
            out = noise(seed);
            break;
        default: break;
    }
    phase += inc;
    if (phase >= 1.0f) phase -= 1.0f;
    return out;
}

float SynthEngine::lfoSample(float phase, WaveType type) {
    switch(type) {
        case WaveType::SINE:     return sinf(phase * TWO_PI);
        case WaveType::TRIANGLE: return (phase < 0.5f) ? (4*phase-1) : (3-4*phase);
        case WaveType::SAW:      return phase * 2.0f - 1.0f;
        case WaveType::SQUARE:   return (phase < 0.5f) ? 1.0f : -1.0f;
        default:                 return 0.0f;
    }
}

// ─── FM synthesis ─────────────────────────────────────────────────────────────

float SynthEngine::processFM(Voice& v) {
    const FMAlgo& algo = FM_ALGOS[_p.fmAlgo & 7];
    float base = v.currentFreq;

    // Process operators from last to first (modulators first)
    for (int op = NUM_FM_OPS - 1; op >= 0; op--) {
        float opFreq = base * _p.opRatio[op];
        float inc = opFreq / _sr;

        // Gather modulation
        float mod = 0;
        if (algo.modSource[op] >= 0) {
            mod = v.opOut[algo.modSource[op]] * _p.opLevel[algo.modSource[op]];
        }
        // Self-feedback on op[0] if algorithm has it
        if (algo.feedback && op == 0) {
            mod += v.opOut[0] * _p.opFeedback * 0.1f;
        }

        v.opPhase[op] += inc;
        if (v.opPhase[op] >= 1.0f) v.opPhase[op] -= 1.0f;

        v.opEnvLevel[op] = v.opEnv[op].process();
        v.opOut[op] = sinf((v.opPhase[op] + mod) * TWO_PI) * v.opEnvLevel[op];
    }

    // Sum carriers
    float out = 0;
    int nCarriers = 0;
    for (int op = 0; op < NUM_FM_OPS; op++) {
        if (algo.isCarrier[op]) {
            out += v.opOut[op] * _p.opLevel[op];
            nCarriers++;
        }
    }
    return (nCarriers > 0) ? out / nCarriers : 0.0f;
}

// ─── Karplus-Strong ───────────────────────────────────────────────────────────

float SynthEngine::processKS(Voice& v) {
    if (v.ksBufLen <= 0) return 0;
    float out = v.ksBuf[v.ksIdx];
    int next = (v.ksIdx + 1) % v.ksBufLen;
    // Averaging LP filter + slight decay
    float avg = 0.4985f * (v.ksBuf[v.ksIdx] + v.ksBuf[next]);
    v.ksBuf[v.ksIdx] = avg;
    v.ksIdx = next;
    return out;
}

// ─── Per-voice sample ─────────────────────────────────────────────────────────

float SynthEngine::sampleVoice(Voice& v) {
    if (!v.active) return 0;

    // ── Portamento ────────────────────────────────────────────────────────────
    if (_p.portaTime > 0.001f) {
        float portaCoef = expf(-1.0f / (_p.portaTime * _sr));
        v.currentFreq = v.freq + (v.currentFreq - v.freq) * portaCoef;
    } else {
        v.currentFreq = v.freq;
    }

    float freq   = v.currentFreq;
    float inc1   = freq / _sr;
    float inc2   = midiToFreq(v.note + (int)_p.osc2Coarse) * powf(2.0f, _p.osc2Detune/12.0f) / _sr;
    float incSub = freq * 0.5f / _sr;

    // ── LFO ──────────────────────────────────────────────────────────────────
    v.lfoPhase += _p.lfoRate / _sr;
    if (v.lfoPhase >= 1.0f) v.lfoPhase -= 1.0f;
    float lfoVal = lfoSample(v.lfoPhase, _p.lfoWave) * _p.lfoDepth;

    // Apply LFO to pitch if needed
    if (_p.lfoDest == LFODest::PITCH) {
        float pitchMod = powf(2.0f, lfoVal * 2.0f / 12.0f);
        inc1 *= pitchMod;
        inc2 *= pitchMod;
    }

    // ── Oscillator mix ────────────────────────────────────────────────────────
    float raw = 0;
    float pw  = _p.pulseWidth;
    if (_p.lfoDest == LFODest::PWM) pw += lfoVal * 0.3f;
    pw = constrain(pw, 0.02f, 0.98f);

    switch(_p.mode) {
        case SynthMode::DUAL_OSC: {
            float s1 = oscillator(v.phase1, inc1, _p.osc1Wave, pw, v.noiseSeed) * _p.osc1Level;
            float s2 = oscillator(v.phase2, inc2, _p.osc2Wave, pw, v.noiseSeed) * _p.osc2Level;
            float ns = noise(v.noiseSeed) * _p.noiseLevel;
            // OSC balance: 0=osc1, 0.5=equal, 1=osc2
            float bal = _p.oscBalance;
            raw = s1 * (1.0f - bal) * 2.0f + s2 * bal * 2.0f + ns;
            break;
        }
        case SynthMode::JUNO: {
            // PWM + SAW + SUB + NOISE
            float sawOut  = oscillator(v.phase1, inc1, WaveType::SAW, 0, v.noiseSeed);
            float pwOut   = oscillator(v.phase2, inc1, WaveType::PULSE, pw, v.noiseSeed);
            float subOut  = oscillator(v.phaseSub, incSub, WaveType::SQUARE, 0, v.noiseSeed);
            float nsOut   = noise(v.noiseSeed);

            raw = sawOut * _p.osc1Level
                + pwOut  * _p.osc2Level
                + subOut * _p.subLevel
                + nsOut  * _p.noiseLevel;

            // Juno chorus (simple: 2-tap BBD model with LFO)
            if (_p.chorus != ChorusMode::OFF) {
                float rate  = (_p.chorus == ChorusMode::MODE1) ? 0.513f : 0.863f;
                v.chorusIdx = (v.chorusIdx + 1) & 511;
                v.chorusBuf[v.chorusIdx] = raw;
                float depth = (int)_p.chorus * 0.005f * _sr;
                depth = constrain(depth, 1.0f, 200.0f);
                float mod1  = sinf(v.lfoPhase * TWO_PI) * depth;
                int tap1    = ((v.chorusIdx - (int)(depth + mod1) + 512) & 511);
                int tap2    = ((v.chorusIdx - (int)(depth - mod1) + 512) & 511);
                raw = (raw + v.chorusBuf[tap1] + v.chorusBuf[tap2]) * 0.5f;
            }
            break;
        }
        case SynthMode::FM:
            raw = processFM(v);
            break;
        case SynthMode::BASS: {
            // Acid bass: sqaure (pulse) + sub + resonant filter env
            float s1 = oscillator(v.phase1, inc1, WaveType::PULSE, pw, v.noiseSeed);
            float sub= oscillator(v.phaseSub, incSub, WaveType::SQUARE, 0, v.noiseSeed);
            raw = s1 + sub * _p.subLevel * 0.5f;
            break;
        }
        case SynthMode::PAD: {
            // 6-voice supersaw
            raw = 0;
            for (int i = 0; i < 6; i++) {
                float detFreq = midiToFreq(v.note) * powf(2.0f, SAW_DETUNE[i] / 12.0f);
                v.sawPhases[i] += detFreq / _sr;
                if (v.sawPhases[i] >= 1.0f) v.sawPhases[i] -= 1.0f;
                raw += v.sawPhases[i] * 2.0f - 1.0f;
            }
            raw /= 4.0f;  // normalize (6 voices at lower weight)
            break;
        }
        case SynthMode::KEYS:
            raw = processKS(v);
            break;
        default:
            raw = oscillator(v.phase1, inc1, _p.osc1Wave, pw, v.noiseSeed);
    }

    // ── Filter ────────────────────────────────────────────────────────────────
    float filterEnvAmt = v.filterEnv.process();
    float cutoff = _p.filterCutoff
                 + filterEnvAmt * _p.filterEnvDepth * 8000.0f
                 + (v.accent ? 2000.0f : 0.0f)       // accent bumps cutoff
                 + (freq - 261.63f) * _p.filterKeyTrack * 4.0f; // keytrack

    if (_p.lfoDest == LFODest::FILTER) cutoff += lfoVal * 2000.0f;
    cutoff = constrain(cutoff, 20.0f, _sr * 0.45f);

    float res = _p.filterRes * (v.accent ? 1.5f : 1.0f);
    res = constrain(res, 0.5f, 20.0f);

    // HPF (Juno mode / high-pass)
    if (_p.hpfCutoff > 40.0f) {
        v.hpf.setParams(_p.hpfCutoff, 0.7f, _sr);
        raw = v.hpf.process(raw, FilterMode::HIGHPASS);
    }

    v.lpf.setParams(cutoff, res, _sr);
    raw = v.lpf.process(raw, _p.filterMode);

    // ── Amp envelope ─────────────────────────────────────────────────────────
    float ampEnv = v.ampEnv.process();
    if (_p.lfoDest == LFODest::AMP) ampEnv *= (1.0f + lfoVal * 0.5f);

    raw *= ampEnv * v.velScale;

    // Mark voice idle when release finishes
    if (v.ampEnv.isIdle()) v.active = false;

    return raw;
}

// ─── Reverb (6-comb Schroeder) ────────────────────────────────────────────────

float SynthEngine::processReverb(float in) {
    float out = 0;
    for (int i = 0; i < REVERB_COMBS; i++) {
        float buf_out = _combBuf[i][_combIdx[i]];
        _combFilter[i] = buf_out * (1.0f - REVERB_DAMP) + _combFilter[i] * REVERB_DAMP;
        _combBuf[i][_combIdx[i]] = in + _combFilter[i] * REVERB_ROOM;
        if (++_combIdx[i] >= _combLen[i]) _combIdx[i] = 0;
        out += buf_out;
    }
    out /= REVERB_COMBS;
    // Allpass diffusion
    for (int i = 0; i < REVERB_ALLPASS; i++) {
        float buf_out  = _apBuf[i][_apIdx[i]];
        _apBuf[i][_apIdx[i]] = out + buf_out * 0.5f;
        if (++_apIdx[i] >= _apLen[i]) _apIdx[i] = 0;
        out = buf_out - out;
    }
    return out;
}

// ─── Main audio processing ────────────────────────────────────────────────────

void SynthEngine::process(int16_t* out, int frames) {
    const float masterGain = _p.masterVol * 28000.0f;
    const float revAmt     = _p.reverbAmt;
    const float delAmt     = _p.delayTime > 0.01f ? _p.delayFeedback : 0.0f;
    const int   delSamples = (int)(_p.delayTime * _sr);

    // Update global LFO
    float globalLfoInc = _p.lfoRate / _sr;

    for (int f = 0; f < frames; f++) {
        _lfoPhase += globalLfoInc;
        if (_lfoPhase >= 1.0f) _lfoPhase -= 1.0f;

        float mix = 0;
        for (int v = 0; v < NUM_VOICES; v++) {
            if (_voices[v].active || !_voices[v].ampEnv.isIdle()) {
                mix += sampleVoice(_voices[v]);
            }
        }

        // Delay
        if (delAmt > 0 && delSamples > 0 && delSamples < DELAY_BUF_LEN) {
            int rdIdx = (_delayWr - delSamples + DELAY_BUF_LEN) % DELAY_BUF_LEN;
            float delayed = _delayBuf[rdIdx];
            _delayBuf[_delayWr] = mix + delayed * _p.delayFeedback;
            if (++_delayWr >= DELAY_BUF_LEN) _delayWr = 0;
            mix += delayed * 0.6f;
        }

        // Reverb
        float revOut = (revAmt > 0.01f) ? processReverb(mix) * revAmt : 0.0f;
        mix += revOut;

        // Scale + clip
        int16_t sample = (int16_t)constrain((int)(mix * masterGain), -32767, 32767);
        out[f * 2]     = sample;  // L
        out[f * 2 + 1] = sample;  // R
    }
}
