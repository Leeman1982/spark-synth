#pragma once
#include <Arduino.h>
#include "config.h"
#include "scales.h"
#include "synth.h"

// ─── Step data ────────────────────────────────────────────────────────────────

struct Step {
    uint8_t note        = 60;   // MIDI note 0-127
    uint8_t velocity    = 100;  // 0-127
    uint8_t gate        = 75;   // % 0-100 of step interval
    uint8_t probability = 100;  // % chance of playing (0-100)
    bool    accent      = false;
    bool    slide       = false;
    bool    active      = false; // step is on/off
};

// ─── Pattern data ─────────────────────────────────────────────────────────────

struct Pattern {
    Step    steps[NUM_STEPS];
    uint8_t length      = 16;   // 1-16 active steps
    uint8_t rootNote    = 0;    // 0=C, 1=C# ... 11=B
    uint8_t scaleIdx    = 0;    // index into SCALES[]
    uint8_t midiChannel = 1;    // 1-16
    uint8_t swing       = 0;    // 0-SWING_MAX %
    SynthParams synth;
};

// ─── Sequencer state ──────────────────────────────────────────────────────────

enum class PlayState : uint8_t { STOPPED, PLAYING, PAUSED };

struct ChainEntry {
    int8_t patternIdx = -1;  // -1 = end of chain
    uint8_t repeats   = 1;
};

class Sequencer {
public:
    Sequencer() {}

    void begin();
    void setPattern(uint8_t idx);
    void setEngine(SynthEngine* engine) { _engine = engine; }

    // Transport
    void play();
    void stop();
    void pause();
    void reset();
    void togglePlay();
    PlayState getPlayState() const { return _playState; }

    // BPM
    void    setBPM(uint16_t bpm);
    uint16_t getBPM() const { return _bpm; }
    void    nudgeBPM(int delta);

    // MIDI clock out (24 PPQN) + start/stop messages
    void setMidiClock(bool en) { _midiClock = en; }
    bool midiClockOut() const  { return _midiClock; }

    // Called from main loop — handles all timing via micros()
    void update();

    // Current state
    uint8_t  currentStep()    const { return _step; }
    uint8_t  currentPattern() const { return _patIdx; }
    uint16_t stepProgress()   const;  // 0-1000 (progress through current step)

    // Pattern/step editing
    Pattern&       getPattern(uint8_t idx) { return _patterns[idx]; }
    Pattern&       getCurrentPattern()     { return _patterns[_patIdx]; }
    Step&          getStep(uint8_t s)      { return _patterns[_patIdx].steps[s]; }
    const Step&    getStep(uint8_t s)      const { return _patterns[_patIdx].steps[s]; }

    // Chain
    void     setChain(const ChainEntry* chain, uint8_t len);
    void     clearChain();
    bool     isChaining() const { return _chainLen > 0; }

    // MIDI channel for current pattern
    uint8_t midiChannel() const { return _patterns[_patIdx].midiChannel; }

    // Quantize all steps in pattern to current scale
    void quantizePattern(uint8_t patIdx);

    // Copy / clear patterns
    void copyPattern(uint8_t src, uint8_t dst);
    void clearPattern(uint8_t idx);

private:
    SynthEngine* _engine     = nullptr;
    PlayState    _playState  = PlayState::STOPPED;
    uint8_t      _patIdx     = 0;
    uint8_t      _step       = 0;
    uint16_t     _bpm        = BPM_DEFAULT;
    bool         _midiClock  = false;

    unsigned long _lastStepUs  = 0;
    unsigned long _stepIntervalUs = 0;  // microseconds per step
    unsigned long _gateOffUs   = 0;    // when to send note off
    unsigned long _nextClockUs = 0;    // next MIDI clock tick
    uint8_t       _activeNote  = 0;
    uint8_t       _activeChannel = 1;  // channel the active note was sent on
    bool          _pendingNoteOff = false;

    Pattern      _patterns[NUM_PATTERNS];

    // Chain
    ChainEntry   _chain[CHAIN_LEN];
    uint8_t      _chainLen      = 0;
    uint8_t      _chainPos      = 0;
    uint8_t      _chainRepeat   = 0;

    void     computeInterval();
    void     triggerStep(uint8_t stepIdx);
    void     sendNoteOff();
    void     advanceStep();
};
