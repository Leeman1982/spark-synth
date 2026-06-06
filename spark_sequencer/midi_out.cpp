#include "midi_out.h"

void MidiOut::begin() {
    // UART1 for MIDI — remap TX to PIN_MIDI_TX
    Serial1.begin(MIDI_BAUD, SERIAL_8N1, PIN_MIDI_RX, PIN_MIDI_TX);
}

void MidiOut::send(uint8_t b1) {
    if (!_enabled) return;
    Serial1.write(b1);
}

void MidiOut::send(uint8_t b1, uint8_t b2) {
    if (!_enabled) return;
    Serial1.write(b1);
    Serial1.write(b2);
}

void MidiOut::send(uint8_t b1, uint8_t b2, uint8_t b3) {
    if (!_enabled) return;
    Serial1.write(b1);
    Serial1.write(b2);
    Serial1.write(b3);
}

void MidiOut::noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    channel = constrain(channel, 1, 16);
    send(0x90 | (channel - 1), note & 0x7F, velocity & 0x7F);
}

void MidiOut::noteOff(uint8_t channel, uint8_t note) {
    channel = constrain(channel, 1, 16);
    send(0x80 | (channel - 1), note & 0x7F, 0);
}

void MidiOut::controlChange(uint8_t channel, uint8_t cc, uint8_t value) {
    channel = constrain(channel, 1, 16);
    send(0xB0 | (channel - 1), cc & 0x7F, value & 0x7F);
}

void MidiOut::programChange(uint8_t channel, uint8_t prog) {
    channel = constrain(channel, 1, 16);
    send(0xC0 | (channel - 1), prog & 0x7F);
}

void MidiOut::pitchBend(uint8_t channel, int16_t value) {
    channel = constrain(channel, 1, 16);
    uint16_t v14 = (uint16_t)(value + 8192);
    send(0xE0 | (channel - 1), v14 & 0x7F, (v14 >> 7) & 0x7F);
}

void MidiOut::allNotesOff(uint8_t channel) {
    controlChange(channel, 123, 0);
}

void MidiOut::clock()       { send(0xF8); }
void MidiOut::start()       { send(0xFA); }
void MidiOut::stop()        { send(0xFC); }
void MidiOut::continueMsg() { send(0xFB); }
