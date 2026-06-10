#include "sequencer.h"
#include "midi_out.h"
#include <string.h>   // memcpy, strcpy

extern MidiOut midiOut;  // defined in main sketch

void Sequencer::begin() {
    _bpm = BPM_DEFAULT;
    computeInterval();
    for (int i = 0; i < NUM_PATTERNS; i++) clearPattern(i);
    _step       = 0;
    _patIdx     = 0;
    _playState  = PlayState::STOPPED;
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
    _chainPos    = 0;
    _chainRepeat = 0;
    if (_engine) _engine->setParams(_patterns[_patIdx].synth);
    if (_midiClock) midiOut.start();
    _lastStepUs  = micros();
    _nextClockUs = _lastStepUs;
    // Fire step 0 immediately — don't wait one full interval
    triggerStep(0);
    advanceStep();
}

void Sequencer::stop() {
    _playState = PlayState::STOPPED;
    sendNoteOff();
    if (_engine) _engine->allNotesOff();
    midiOut.allNotesOff(_patterns[_patIdx].midiChannel);
    if (_midiClock) midiOut.stop();
    _step = 0;
}

void Sequencer::pause() {
    if (_playState == PlayState::PLAYING) {
        _playState = PlayState::PAUSED;
        sendNoteOff();
    } else if (_playState == PlayState::PAUSED) {
        _playState = PlayState::PLAYING;
        _lastStepUs  = micros();
        _nextClockUs = _lastStepUs;
        if (_midiClock) midiOut.continueMsg();
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
    unsigned long elapsed = micros() - _lastStepUs;
    long prog = (long)((uint64_t)elapsed * 1000 / _stepIntervalUs);
    return (uint16_t)constrain(prog, 0L, 999L);
}

// ─── Note output ──────────────────────────────────────────────────────────────

void Sequencer::sendNoteOff() {
    if (_pendingNoteOff) {
        if (_engine) _engine->noteOff(_activeNote);
        // Use the channel the note was started on — the pattern (and its
        // channel) may have changed since, e.g. across a chain transition.
        midiOut.noteOff(_activeChannel, _activeNote);
        _pendingNoteOff = false;
    }
}

void Sequencer::triggerStep(uint8_t stepIdx) {
    Pattern& pat = _patterns[_patIdx];
    const Step& s = pat.steps[stepIdx];

    // Inactive / probability-skipped steps: release any note that was held
    // over for a slide that is no longer going to happen.
    if (!s.active) { sendNoteOff(); return; }
    if (s.probability < 100 && random(100) >= s.probability) {
        sendNoteOff();
        return;
    }

    bool    slideNow = s.slide && _pendingNoteOff;
    uint8_t prevNote = _activeNote;
    uint8_t prevChan = _activeChannel;

    if (_pendingNoteOff && !slideNow) sendNoteOff();

    uint8_t note = quantizeNote(s.note, pat.rootNote, pat.scaleIdx);
    uint8_t vel  = s.velocity;
    if (s.accent) vel = min(127, vel + 30);

    if (_engine) _engine->noteOn(note, vel, s.accent, slideNow);
    midiOut.noteOn(pat.midiChannel, note, vel);

    // Legato overlap: release the previous note AFTER the new one starts,
    // so MIDI synths with mono/legato modes glide instead of retriggering.
    if (slideNow && prevNote != note) {
        if (_engine) _engine->noteOff(prevNote);
        midiOut.noteOff(prevChan, prevNote);
    }

    _activeNote     = note;
    _activeChannel  = pat.midiChannel;
    _pendingNoteOff = true;
    _gateOffUs      = _lastStepUs + (_stepIntervalUs * s.gate / 100UL);
}

void Sequencer::advanceStep() {
    Pattern& pat = _patterns[_patIdx];
    _step = (_step + 1) % pat.length;

    // Pattern chain advance — switch seamlessly without killing the note
    // just triggered (its note-off is tracked by _activeNote/_activeChannel).
    if (_step == 0 && _chainLen > 0) {
        _chainRepeat++;
        if (_chainRepeat >= _chain[_chainPos].repeats) {
            _chainRepeat = 0;
            _chainPos = (_chainPos + 1) % _chainLen;
            if (_chain[_chainPos].patternIdx < 0) _chainPos = 0;
            int8_t next = _chain[_chainPos].patternIdx;
            if (next >= 0 && next < NUM_PATTERNS && next != _patIdx) {
                _patIdx = next;
                if (_engine) _engine->setParams(_patterns[_patIdx].synth);
            }
        }
    }
}

// ─── Main update (call from loop()) ───────────────────────────────────────────
// All time comparisons use signed-difference form so they survive the
// ~71-minute micros() rollover.

void Sequencer::update() {
    if (_playState != PlayState::PLAYING) return;

    unsigned long now = micros();
    Pattern& pat = _patterns[_patIdx];

    // Pattern length may have been shortened mid-play
    if (_step >= pat.length) _step = 0;

    // Gate off — deferred when the upcoming step slides into this note
    if (_pendingNoteOff && (long)(now - _gateOffUs) >= 0) {
        const Step& nx = pat.steps[_step];
        bool slideNext = nx.active && nx.slide;
        if (!slideNext) sendNoteOff();
    }

    // MIDI clock — 24 PPQN (6 ticks per 16th), unswung, bounded catch-up
    if (_midiClock) {
        unsigned long clkInterval = _stepIntervalUs / 6;
        for (int i = 0; i < 4 && (long)(now - _nextClockUs) >= 0; i++) {
            midiOut.clock();
            _nextClockUs += clkInterval;
        }
    }

    // Step advance with swing: odd 16ths delayed, even pulled early
    unsigned long interval = _stepIntervalUs;
    if (pat.swing > 0) {
        unsigned long swingUs = _stepIntervalUs * pat.swing / 200;
        interval += (_step & 1) ? swingUs : -swingUs;
    }
    if ((long)(now - _lastStepUs) >= (long)interval) {
        _lastStepUs += interval;
        triggerStep(_step);
        advanceStep();
    }
}
