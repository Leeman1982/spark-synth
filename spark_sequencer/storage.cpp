#include "storage.h"

// Every file starts with a validated header so stale data from an older
// firmware (different struct layout) is rejected instead of loaded as garbage.
struct FileHeader {
    uint16_t magic;
    uint8_t  version;
    uint8_t  kind;     // 'P' = pattern, 'Y' = synth patch
    uint32_t size;     // payload size — must match sizeof(struct)
};

static bool writeBlob(const char* path, uint8_t kind, const void* data, uint32_t size) {
    File f = LittleFS.open(path, "w", true);
    if (!f) return false;
    FileHeader h = { STORAGE_MAGIC, STORAGE_VERSION, kind, size };
    bool ok = f.write((const uint8_t*)&h, sizeof(h)) == sizeof(h) &&
              f.write((const uint8_t*)data, size) == size;
    f.close();
    return ok;
}

static bool readBlob(const char* path, uint8_t kind, void* data, uint32_t size) {
    if (!LittleFS.exists(path)) return false;
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    FileHeader h = {};
    bool ok = f.read((uint8_t*)&h, sizeof(h)) == sizeof(h) &&
              h.magic == STORAGE_MAGIC && h.version == STORAGE_VERSION &&
              h.kind == kind && h.size == size &&
              f.read((uint8_t*)data, size) == size;
    f.close();
    return ok;
}

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
    return writeBlob(path, 'P', &pat, sizeof(Pattern));
}

bool Storage::loadPattern(uint8_t idx, Pattern& pat) {
    if (!_mounted || idx >= NUM_PATTERNS) return false;
    char path[24];
    patternPath(idx, path);
    return readBlob(path, 'P', &pat, sizeof(Pattern));
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
    return writeBlob(path, 'Y', &p, sizeof(SynthParams));
}

bool Storage::loadPatch(uint8_t slot, SynthParams& p) {
    if (!_mounted || slot >= 32) return false;
    char path[24];
    patchPath(slot, path);
    return readBlob(path, 'Y', &p, sizeof(SynthParams));
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
    size_t got = f.read((uint8_t*)&s, sizeof(GlobalSettings));
    f.close();
    if (got != sizeof(GlobalSettings) ||
        s.magic != STORAGE_MAGIC || s.version != STORAGE_VERSION) {
        s = GlobalSettings();  // reset to defaults
        return false;
    }
    return true;
}

void Storage::formatAll() {
    LittleFS.format();
}
