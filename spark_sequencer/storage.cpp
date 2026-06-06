#include "storage.h"

bool Storage::begin() {
    if (!LittleFS.begin(true)) {  // true = format if mount fails
        Serial.println("[Storage] LittleFS mount failed");
        return false;
    }
    _mounted = true;
    return true;
}

void Storage::patternPath(uint8_t idx, char* buf) {
    snprintf(buf, 24, "/pat%02d.bin", idx);
}

void Storage::patchPath(uint8_t slot, char* buf) {
    snprintf(buf, 24, "/patch%02d.bin", slot);
}

bool Storage::savePattern(uint8_t idx, const Pattern& pat) {
    if (!_mounted || idx >= NUM_PATTERNS) return false;
    char path[24];
    patternPath(idx, path);
    File f = LittleFS.open(path, "w", true);
    if (!f) return false;
    size_t written = f.write((const uint8_t*)&pat, sizeof(Pattern));
    f.close();
    return written == sizeof(Pattern);
}

bool Storage::loadPattern(uint8_t idx, Pattern& pat) {
    if (!_mounted || idx >= NUM_PATTERNS) return false;
    char path[24];
    patternPath(idx, path);
    if (!LittleFS.exists(path)) return false;
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    size_t read = f.read((uint8_t*)&pat, sizeof(Pattern));
    f.close();
    return read == sizeof(Pattern);
}

bool Storage::patternExists(uint8_t idx) {
    if (!_mounted) return false;
    char path[24];
    patternPath(idx, path);
    return LittleFS.exists(path);
}

bool Storage::savePatch(uint8_t slot, const SynthParams& p) {
    if (!_mounted || slot >= 32) return false;
    char path[24];
    patchPath(slot, path);
    File f = LittleFS.open(path, "w", true);
    if (!f) return false;
    size_t written = f.write((const uint8_t*)&p, sizeof(SynthParams));
    f.close();
    return written == sizeof(SynthParams);
}

bool Storage::loadPatch(uint8_t slot, SynthParams& p) {
    if (!_mounted || slot >= 32) return false;
    char path[24];
    patchPath(slot, path);
    if (!LittleFS.exists(path)) return false;
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    size_t read = f.read((uint8_t*)&p, sizeof(SynthParams));
    f.close();
    return read == sizeof(SynthParams);
}

bool Storage::saveSettings(const GlobalSettings& s) {
    if (!_mounted) return false;
    File f = LittleFS.open("/settings.bin", "w", true);
    if (!f) return false;
    f.write((const uint8_t*)&s, sizeof(GlobalSettings));
    f.close();
    return true;
}

bool Storage::loadSettings(GlobalSettings& s) {
    if (!_mounted) return false;
    if (!LittleFS.exists("/settings.bin")) return false;
    File f = LittleFS.open("/settings.bin", "r");
    if (!f) return false;
    f.read((uint8_t*)&s, sizeof(GlobalSettings));
    f.close();
    if (s.magic != STORAGE_MAGIC || s.version != STORAGE_VERSION) {
        s = GlobalSettings();  // reset to defaults
        return false;
    }
    return true;
}

void Storage::formatAll() {
    LittleFS.format();
}
