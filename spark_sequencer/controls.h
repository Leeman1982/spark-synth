#pragma once
#include <Arduino.h>
#include "config.h"

// ─── Rotary encoder with ISR-based counting ───────────────────────────────────

class RotaryEncoder {
public:
    RotaryEncoder(uint8_t pinA, uint8_t pinB, uint8_t pinSW);
    void begin();
    void update();         // call each loop — debounced switch state machine

    int  getDelta();       // detent steps since last call (quadrature ticks / 4)
    bool wasPressed();     // one-shot: short press (fires on release)
    bool isHeld();         // true while switch held
    bool wasLongPress();   // one-shot: fires once while held past LONG_PRESS_MS

    // ISR callback — attach to both A+B pins
    static void IRAM_ATTR isrA(void* arg);
    static void IRAM_ATTR isrB(void* arg);

private:
    uint8_t _pinA, _pinB, _pinSW;
    volatile int  _count = 0;
    volatile uint8_t _last = 0;

    // Switch state machine (mirrors Button)
    bool          _swState     = false;  // debounced pressed state
    bool          _swRaw       = false;
    bool          _pressedFlag = false;
    bool          _longFlag    = false;
    bool          _longFired   = false;
    unsigned long _swDebounceT = 0;
    unsigned long _swPressT    = 0;
};

// ─── Debounced button ─────────────────────────────────────────────────────────

class Button {
public:
    Button(uint8_t pin, bool activeLow = true);
    void begin();
    void update();           // call each loop

    bool wasPressed();       // one-shot on press edge
    bool wasReleased();      // one-shot on release edge
    bool isHeld();           // true while held
    bool wasLongPress();     // one-shot on long-press release

private:
    uint8_t       _pin;
    bool          _activeLow;
    bool          _state = false;
    bool          _lastState = false;
    bool          _pressedFlag  = false;
    bool          _releasedFlag = false;
    bool          _longFired    = false;
    bool          _lpReported   = false;   // guards wasLongPress() one-shot
    unsigned long _pressTime    = 0;
    unsigned long _lastDebounce = 0;
    uint8_t       _rawLast = HIGH;
};

// ─── Controls aggregator ──────────────────────────────────────────────────────

class Controls {
public:
    Controls();
    void begin();
    void update();   // call every loop — polls buttons, not encoder (ISR)

    RotaryEncoder encoder;
    Button        btnBack;
    Button        btnConfirm;
    Button        btnShift;

private:
};
