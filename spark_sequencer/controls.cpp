#include "controls.h"

// ─── RotaryEncoder ────────────────────────────────────────────────────────────

RotaryEncoder::RotaryEncoder(uint8_t pinA, uint8_t pinB, uint8_t pinSW)
    : _pinA(pinA), _pinB(pinB), _pinSW(pinSW) {}

void RotaryEncoder::begin() {
    pinMode(_pinA,  INPUT_PULLUP);
    pinMode(_pinB,  INPUT_PULLUP);
    pinMode(_pinSW, INPUT_PULLUP);
    _last = (digitalRead(_pinA) << 1) | digitalRead(_pinB);
    // Attach interrupts — pass 'this' as arg
    attachInterruptArg(digitalPinToInterrupt(_pinA), isrA, this, CHANGE);
    attachInterruptArg(digitalPinToInterrupt(_pinB), isrB, this, CHANGE);
}

void IRAM_ATTR RotaryEncoder::isrA(void* arg) {
    RotaryEncoder* enc = (RotaryEncoder*)arg;
    uint8_t a = digitalRead(enc->_pinA);
    uint8_t b = digitalRead(enc->_pinB);
    uint8_t state = (a << 1) | b;
    if (state != enc->_last) {
        // Gray code decode: CW = A leads B
        if ((enc->_last == 0b11 && state == 0b01) ||
            (enc->_last == 0b01 && state == 0b00) ||
            (enc->_last == 0b00 && state == 0b10) ||
            (enc->_last == 0b10 && state == 0b11)) {
            enc->_count++;
        } else {
            enc->_count--;
        }
        enc->_last = state;
    }
}

void IRAM_ATTR RotaryEncoder::isrB(void* arg) {
    // Same handler — both pins change
    isrA(arg);
}

int RotaryEncoder::getDelta() {
    // EC11: 4 quadrature transitions per detent — emit whole detents,
    // keep the remainder so no motion is lost between calls.
    noInterrupts();
    int d = _count / ENC_TICKS_PER_DETENT;
    _count -= d * ENC_TICKS_PER_DETENT;
    interrupts();
    return d;
}

void RotaryEncoder::update() {
    bool raw = (digitalRead(_pinSW) == LOW);
    unsigned long now = millis();

    if (raw != _swRaw) {
        _swDebounceT = now;
        _swRaw = raw;
    }

    if ((now - _swDebounceT) > DEBOUNCE_MS) {
        if (raw && !_swState) {
            // Press edge
            _swState   = true;
            _swPressT  = now;
            _longFired = false;
        } else if (!raw && _swState) {
            // Release edge — short press only if long didn't fire
            _swState = false;
            if (!_longFired) _pressedFlag = true;
        }
    }

    // Long press fires once while still held
    if (_swState && !_longFired && (now - _swPressT) >= LONG_PRESS_MS) {
        _longFired = true;
        _longFlag  = true;
    }
}

bool RotaryEncoder::wasPressed() {
    if (_pressedFlag) { _pressedFlag = false; return true; }
    return false;
}

bool RotaryEncoder::isHeld() {
    return _swState;
}

bool RotaryEncoder::wasLongPress() {
    if (_longFlag) { _longFlag = false; return true; }
    return false;
}

// ─── Button ───────────────────────────────────────────────────────────────────

Button::Button(uint8_t pin, bool activeLow) : _pin(pin), _activeLow(activeLow) {}

void Button::begin() {
    pinMode(_pin, _activeLow ? INPUT_PULLUP : INPUT_PULLDOWN);
}

void Button::update() {
    uint8_t raw = digitalRead(_pin);
    bool active = _activeLow ? (raw == LOW) : (raw == HIGH);
    unsigned long now = millis();

    if (active != _lastState) {
        _lastDebounce = now;
    }

    if ((now - _lastDebounce) > DEBOUNCE_MS) {
        if (active && !_state) {
            // Rising edge
            _state       = true;
            _pressedFlag = true;
            _pressTime   = now;
            _longFired   = false;
            _lpReported  = false;
        } else if (!active && _state) {
            // Falling edge
            _state          = false;
            _releasedFlag   = true;
        }
    }
    _lastState = active;

    // Long press detection
    if (_state && !_longFired && (now - _pressTime) >= LONG_PRESS_MS) {
        _longFired = true;
    }
}

bool Button::wasPressed() {
    if (_pressedFlag) { _pressedFlag = false; return true; }
    return false;
}

bool Button::wasReleased() {
    if (_releasedFlag) { _releasedFlag = false; return true; }
    return false;
}

bool Button::isHeld() { return _state; }

bool Button::wasLongPress() {
    if (_longFired && !_lpReported) {
        _lpReported = true;
        return true;
    }
    if (!_state) _lpReported = false;  // reset when released
    return false;
}

// ─── Controls ─────────────────────────────────────────────────────────────────

Controls::Controls()
    : encoder(PIN_ENC_A, PIN_ENC_B, PIN_ENC_SW),
      btnBack(PIN_BTN_BACK),
      btnConfirm(PIN_BTN_CONFIRM),
      btnShift(PIN_BTN_SHIFT) {}

void Controls::begin() {
    encoder.begin();
    btnBack.begin();
    btnConfirm.begin();
    btnShift.begin();
}

void Controls::update() {
    encoder.update();   // rotation is ISR-driven; this polls the push switch
    btnBack.update();
    btnConfirm.update();
    btnShift.update();
}
