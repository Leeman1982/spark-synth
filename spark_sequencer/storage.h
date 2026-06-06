#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"
#include "sequencer.h"
#include "synth.h"

struct GlobalSettings {
    uint16_t magic   = STORAGE_MAGIC;
    uint8_t  version = STORAGE_VERSION;
    uint16_t bpm     = BPM_DEFAULT;
    uint8_t  lastPattern = 0;
    bool     midiClockOut = false;
    uint8_t  midiChannel  = 1;
    float    masterVol    = 0.8f;
};

class Storage {
public:
    bool begin();   // mounts LittleFS

    // Patterns
    bool savePattern(uint8_t idx, const Pattern& pat);
    bool loadPattern(uint8_t idx, Pattern& pat);
    bool patternExists(uint8_t idx);

    // Synth patches (save/load patch independently)
    bool savePatch(uint8_t slot, const SynthParams& p);
    bool loadPatch(uint8_t slot, SynthParams& p);

    // Global settings
    bool saveSettings(const GlobalSettings& s);
    bool loadSettings(GlobalSettings& s);

    // Wipe all
    void formatAll();

private:
    bool _mounted = false;
    void patternPath(uint8_t idx, char* buf);
    void patchPath(uint8_t slot, char* buf);
};
