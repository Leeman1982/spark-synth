#include "sequencer.h"
#include "midi_out.h"

extern MidiOut midiOut;  // defined in main sketch

void Sequencer::begin() {
    _bpm = BPM_DEFAULT;
    computeInterval();
    for (int i = 0; i < NUM_PATTERNS; i++) clearPattern(i);
    _step       = 0;
    _patIdx     = 0;
    _playState  = PlayState::STOPPED;
    _noteActive = false;
    _chainLen   = 0;
}

void Sequencer::computeInterval() {
    // 16th note interval in microseconds: 15,000,000 / BPM
    _stepIntervalUs = 15000000UL / _bpm;
}

void Sequencer::setBPM(uint16_t bpm) {
    _bpm = constrain(bpm, BPM_MIN, BPM_MAX);
    computeInterval();
}

void Sequencer::nudgeBPM(int delta) {
    setBPM((int)_bpm + delta);
}

void Sequencer::play() {
    if (_playState == PlayState::PLAYING) return;
    _playState   = PlayState::PLAYING;
    _step        = 0;
    _lastStepUs  = micros();
    _engine->setParams(_patterns[_patIdx].synth);
}

void Sequencer::stop() {
    _playState = PlayState::STOPPED;
    sendNoteOff();
    if (_engine) _engine->allNotesOff();
    midiOut.allNotesOff(_patterns[_patIdx].midiChannel);
    _step = 0;
}

void Sequencer::pause() {
    if (_playState == PlayState::PLAYING) {
        _playState = PlayState::PAUSED;
        sendNoteOff();
    } else if (_playState == PlayState::PAUSED) {
        _playState = PlayState::PLAYING;
        _lastStepUs = micros();
    }
}

void Sequencer::reset() {
    sendNoteOff();
    _step = 0;
    _lastStepUs = micros();
}

void Sequencer::togglePlay() {
    if (_playState == PlayState::PLAYING) stop();
    else                                  play();
}

void Sequencer::setPattern(uint8_t idx) {
    if (idx >= NUM_PATTERNS) return;
    sendNoteOff();
    if (_engine) _engine->allNotesOff();
    _patIdx = idx;
    if (_playState == PlayState::PLAYING) {
        _engine->setParams(_patterns[_patIdx].synth);
    }
}

// ─── Quantize ─────────────────────────────────────────────────────────────────

void Sequencer::quantizePattern(uint8_t patIdx) {
    Pattern& pat = _patterns[patIdx];
    for (int s = 0; s < NUM_STEPS; s++) {
        if (pat.steps[s].active) {
            pat.steps[s].note = quantizeNote(pat.steps[s].note, pat.rootNote, pat.scaleIdx);
        }
    }
}

// ─── Pattern utilities ────────────────────────────────────────────────────────

void Sequencer::clearPattern(uint8_t idx) {
    if (idx >= NUM_PATTERNS) return;
    Pattern& p = _patterns[idx];
    p.length   = 16;
    p.rootNote = 0;
    p.scaleIdx = 0;
    p.midiChannel = 1;
    p.swing    = 0;
    for (int s = 0; s < NUM_STEPS; s++) {
        p.steps[s] = Step();
    }
}

void Sequencer::copyPattern(uint8_t src, uint8_t dst) {
    if (src >= NUM_PATTERNS || dst >= NUM_PATTERNS) return;
    _patterns[dst] = _patterns[src];
}

// ─── Chain ────────────────────────────────────────────────────────────────────

void Sequencer::setChain(const ChainEntry* chain, uint8_t len) {
    _chainLen   = min(len, (uint8_t)CHAIN_LEN);
    _chainPos   = 0;
    _chainRepeat= 0;
    memcpy(_chain, chain, _chainLen * sizeof(ChainEntry));
    if (_chainLen > 0 && _chain[0].patternIdx >= 0) {
        setPattern(_chain[0].patternIdx);
    }
}

void Sequencer::clearChain() {
    _chainLen = 0;
}

// ─── Step progress ────────────────────────────────────────────────────────────

uint16_t Sequencer::stepProgress() const {
    if (_playState != PlayState::PLAYING) return 0;
    unsigned long now = micros();
    unsigned long elapsed = now - _lastStepUs;
    return (uint16_t)constrain((long)(elapsed * 1000 / _stepIntervalUs), 0, 999);
}

// ─── Note output ──────────────────────────────────────────────────────────────

void Sequencer::sendNoteOff() {
    if (_pendingNoteOff) {
        if (_engine)  _engine->noteOff(_activeNote);
        midiOut.noteOff(_patterns[_patIdx].midiChannel, _activeNote);
        _pendingNoteOff = false;
        _noteActive     = false;
    }
}

void Sequencer::triggerStep(uint8_t stepIdx) {
    Pattern& pat = _patterns[_patIdx];
    const Step& s = pat.steps[stepIdx];

    // Send note off for previous note (if no slide into this step)
    if (_pendingNoteOff && !s.slide) {
        sendNoteOff();
    }

    if (!s.active) return;

    // Probability check
    if (s.probability < 100) {
        if (random(100) >= s.probability) return;
    }

    // Quantize note to scale
    uint8_t note = quantizeNote(s.note, pat.rootNote, pat.scaleIdx);
    uint8_t vel  = s.velocity;
    if (s.accent) vel = min(127, vel + 30);

    // Synth note on
    if (_engine) {
        _engine->noteOn(note, vel, s.accent, s.slide && _pendingNoteOff);
    }

    // MIDI note on
    midiOut.noteOn(pat.midiChannel, note, vel);

    // Schedule note off
    _activeNote         = note;
    _activeVelocity     = vel;
    _pendingNoteOff     = true;
    _noteActive         = true;

    // Gate off time = stepInterval * gate%
    _gateOffUs = _lastStepUs + (_stepIntervalUs * s.gate / 100);
}

void Sequencer::advanceStep() {
    Pattern& pat = _patterns[_patIdx];
    _step = (_step + 1) % pat.length;

    // Pattern chain advance
    if (_step == 0 && _chainLen > 0) {
        _chainRepeat++;
        if (_chainRepeat >= _chain[_chainPos].repeats) {
            _chainRepeat = 0;
            _chainPos = (_chainPos + 1) % _chainLen;
            if (_chain[_chainPos].patternIdx < 0) _chainPos = 0;
            setPattern(_chain[_chainPos].patternIdx);
        }
    }
}

// ─── Main update (call from loop()) ───────────────────────────────────────────

void Sequencer::update() {
    if (_playState != PlayState::PLAYING) return;

    unsigned long now = micros();

    // Handle gate off
    if (_pendingNoteOff && now >= _gateOffUs) {
        // Only send note-off if next step doesn't slide into this
        sendNoteOff();
    }

    // MIDI clock output (24 PPQ — send 6 clocks per 16th note)
    // TODO: add MIDI clock support here

    // Step advance
    if (now - _lastStepUs >= _stepIntervalUs) {
        // Apply swing: even steps play early, odd play late
        unsigned long interval = _stepIntervalUs;
        if (_patterns[_patIdx].swing > 0) {
            long swingUs = (long)(_stepIntervalUs * _patterns[_patIdx].swing / 200);
            interval += (_step & 1) ? swingUs : -swingUs;
        }
        if (now - _lastStepUs >= (unsigned long)interval) {
            _lastStepUs += interval;
            triggerStep(_step);
            advanceStep();
        }
    }
}
