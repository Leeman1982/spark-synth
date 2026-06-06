#pragma once
#include <Arduino.h>
#include "config.h"

// ─── Scale definitions ────────────────────────────────────────────────────────
// Each scale: up to 12 semitone offsets from root, terminated by -1

struct Scale {
    const char name[10];
    int8_t  intervals[12];  // semitone offsets, -1 = end
    uint8_t numNotes;
};

static const Scale SCALES[] = {
    { "CHROMATIC", {0,1,2,3,4,5,6,7,8,9,10,11}, 12 },
    { "MAJOR",     {0,2,4,5,7,9,11,-1},           7  },
    { "MINOR",     {0,2,3,5,7,8,10,-1},            7  },
    { "DORIAN",    {0,2,3,5,7,9,10,-1},            7  },
    { "PHRYGIAN",  {0,1,3,5,7,8,10,-1},            7  },
    { "LYDIAN",    {0,2,4,6,7,9,11,-1},            7  },
    { "MIXOLYD",   {0,2,4,5,7,9,10,-1},            7  },
    { "LOCRIAN",   {0,1,3,5,6,8,10,-1},            7  },
    { "PENT MAJ",  {0,2,4,7,9,-1},                 5  },
    { "PENT MIN",  {0,3,5,7,10,-1},                5  },
    { "BLUES",     {0,3,5,6,7,10,-1},              6  },
    { "HARMMIN",   {0,2,3,5,7,8,11,-1},            7  },
    { "MELODMIN",  {0,2,3,5,7,9,11,-1},            7  },
    { "WHOLE",     {0,2,4,6,8,10,-1},              6  },
    { "DIMINISH",  {0,2,3,5,6,8,9,11,-1},          8  },
    { "ARABIAN",   {0,2,4,5,6,8,10,-1},            7  },
};
static const uint8_t NUM_SCALES = sizeof(SCALES) / sizeof(SCALES[0]);

static const char* const NOTE_NAMES[] = {
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B"
};

// Quantize a MIDI note to the nearest note in the given scale/root
inline uint8_t quantizeNote(uint8_t midiNote, uint8_t root, uint8_t scaleIdx) {
    if (scaleIdx == 0) return midiNote;  // chromatic — no quantize
    const Scale& sc = SCALES[scaleIdx];
    int octave   = midiNote / 12;
    int semitone = midiNote % 12;
    // shift relative to root
    int rel = (semitone - root + 12) % 12;
    // find nearest scale degree
    int best = 0, bestDist = 12;
    for (int i = 0; i < sc.numNotes; i++) {
        int dist = abs(rel - sc.intervals[i]);
        if (dist < bestDist) { bestDist = dist; best = i; }
    }
    int quantRel = (sc.intervals[best] + root) % 12;
    return (uint8_t)constrain(octave * 12 + quantRel, 0, 127);
}

// Get short note name (e.g. "C#4")
inline void noteName(uint8_t midiNote, char* buf) {
    int oct  = (midiNote / 12) - 1;
    int semi = midiNote % 12;
    snprintf(buf, 6, "%s%d", NOTE_NAMES[semi], oct);
}

// Get frequency in Hz from MIDI note
inline float midiToFreq(uint8_t note) {
    return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}
