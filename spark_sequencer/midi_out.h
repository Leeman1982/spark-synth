#pragma once
#include <Arduino.h>
#include "config.h"

// ─── MIDI over UART — 31250 baud, no library needed ──────────────────────────
// Wire TX through a 220Ω resistor to TRS Tip.
// TRS Ring → 220Ω → 3.3V for current-loop drive.
// TRS Sleeve → GND.

class MidiOut {
public:
    void begin();

    // Standard messages
    void noteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    void noteOff(uint8_t channel, uint8_t note);
    void controlChange(uint8_t channel, uint8_t cc, uint8_t value);
    void programChange(uint8_t channel, uint8_t prog);
    void pitchBend(uint8_t channel, int16_t value);  // -8192 to 8191
    void allNotesOff(uint8_t channel);

    // MIDI clock (for sync)
    void clock();
    void start();
    void stop();
    void continueMsg();

    // Enable/disable
    void setEnabled(bool en) { _enabled = en; }
    bool isEnabled()   const { return _enabled; }

    // MIDI channel filter
    void setChannel(uint8_t ch) { _channel = constrain(ch, 1, 16); }

private:
    bool    _enabled = true;
    uint8_t _channel = 1;

    void send(uint8_t b1);
    void send(uint8_t b1, uint8_t b2);
    void send(uint8_t b1, uint8_t b2, uint8_t b3);
};
