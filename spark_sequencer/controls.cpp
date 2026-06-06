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
    noInterrupts();
    int d = _count;
    _count = 0;
    interrupts();
    return d;
}

bool RotaryEncoder::wasPressed() {
    uint8_t raw = digitalRead(_pinSW);
    unsigned long now = millis();

    if (raw == LOW && _swLast == HIGH) {
        // Debounce
        if (now - _pressTime > DEBOUNCE_MS) {
            _pressTime  = now;
            _pressed    = true;
            _longFired  = false;
        }
    }
    _swLast = raw;

    if (_pressed && raw == HIGH) {
        _pressed = false;
        if (!_longFired) return true;
    }
    return false;
}

bool RotaryEncoder::isHeld() {
    return digitalRead(_pinSW) == LOW;
}

bool RotaryEncoder::wasLongPress() {
    uint8_t raw = digitalRead(_pinSW);
    unsigned long now = millis();

    if (raw == LOW && !_longFired) {
        if (_pressTime > 0 && (now - _pressTime) >= LONG_PRESS_MS) {
            _longFired = true;
            return true;
        }
    }
    if (raw == HIGH) _pressTime = now;  // reset on release
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
    // Returns true once on long-press threshold
    // Caller checks this each loop; we report it via update()
    // Use the flag pattern: clear after reading
    static bool _lpFlag = false;
    if (_state && _longFired && !_lpFlag) {
        _lpFlag = true;
        return true;
    }
    if (!_state) _lpFlag = false;
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
    btnBack.update();
    btnConfirm.update();
    btnShift.update();
    // encoder is ISR-driven, no poll needed
}
