#include "ui.h"
#include "storage.h"
#include "scales.h"
#include <math.h>
#include <string.h>

extern Storage storage;

// Synth mode names
static const char* const SYNTH_MODE_NAMES[] = {
    "ANALOG", "JUNO", "FM", "BASS", "PAD", "KEYS"
};

static const char* const WAVEFORM_NAMES[] = {
    "SINE", "TRI", "SAW", "SQR", "PLS", "NOIS"
};

static const char* const LFO_DEST_NAMES[] = {
    "NONE", "FILT", "PTCH", "AMP", "PWM"
};

static const char* const FILTER_MODE_NAMES[] = {
    "LP", "BP", "HP", "NOTCH"
};

static const char* const CHORUS_NAMES[] = {
    "OFF", "I", "II"
};

// ─── Constructor ─────────────────────────────────────────────────────────────

UI::UI(Sequencer* seq, SynthEngine* engine)
    : _seq(seq), _engine(engine),
      _u8g2(U8G2_R0, PIN_OLED_SCL, PIN_OLED_SDA, U8X8_PIN_NONE) {}

void UI::begin() {
    _u8g2.begin();
    _u8g2.setContrast(220);

    _u8g2.clearBuffer();

    // Thick top and bottom accent bars
    _u8g2.drawBox(0, 0,          DISP_W, 3);
    _u8g2.drawBox(0, DISP_H - 3, DISP_W, 3);

    // Main title — large bold
    _u8g2.setFont(u8g2_font_9x18B_tr);
    const char* title = "SPARK";
    uint8_t tw = _u8g2.getStrWidth(title);
    _u8g2.drawStr((DISP_W - tw) / 2, 34, title);

    // Thin separator below title
    _u8g2.drawHLine(24, 38, DISP_W - 48);

    // Subtitle
    _u8g2.setFont(u8g2_font_4x6_tr);
    const char* sub = "STEP SEQUENCER";
    tw = _u8g2.getStrWidth(sub);
    _u8g2.drawStr((DISP_W - tw) / 2, 49, sub);

    _u8g2.sendBuffer();
    delay(1200);
    _dirty = true;
}

// ─── Navigation helpers ───────────────────────────────────────────────────────

void UI::pushScreen(Screen s) {
    _prevScreen = _screen;
    _screen     = s;
    _dirty      = true;
}

void UI::popScreen() {
    _screen = _prevScreen;
    _dirty  = true;
}

// ─── Main update ─────────────────────────────────────────────────────────────

void UI::update() {
    // A full SW-I2C frame blocks the loop for many milliseconds, so only
    // draw when something changed (dirty) or the screen is animated
    // (playhead/progress while playing, tempo dot on BPM edit) — and never
    // faster than UI_REFRESH_MS.
    unsigned long now = millis();
    bool animated = (_seq->getPlayState() == PlayState::PLAYING) ||
                    (_screen == Screen::BPM_EDIT);
    if ((_dirty || animated) && (now - _lastDraw >= UI_REFRESH_MS)) {
        _lastDraw = now;
        drawAll();
        _dirty = false;
    }
}

// ─── Input handlers ───────────────────────────────────────────────────────────

void UI::handleEncoder(int delta) {
    if (delta == 0) return;
    _dirty = true;

    switch(_screen) {
        case Screen::MAIN:
            if (_editMode) {
                // Edit BPM directly from main screen with shift held
                _seq->nudgeBPM(delta);
            } else {
                _selStep = (uint8_t)((_selStep + delta + NUM_STEPS) % NUM_STEPS);
            }
            break;

        case Screen::STEP_EDIT:
            if (_stepEditing) {
                changeStepField(delta);
            } else {
                int f = (int)_stepField + delta;
                _stepField = (StepField)constrain(f, 0, (int)StepField::NUM_FIELDS - 1);
            }
            break;

        case Screen::SYNTH_PARAMS:
            if (_synthEditing) {
                changeSynthParam(delta);
            } else {
                // Walk selection through VISIBLE params only
                int dir = (delta > 0) ? 1 : -1;
                for (int n = 0; n < abs(delta); n++) {
                    _synthSel = stepVisibleParam(_synthSel, dir);
                }
                // Scroll window tracks the visible-row index (6 rows shown)
                int vis = visibleIndexOf(_synthSel);
                if (vis < _synthScroll)      _synthScroll = vis;
                if (vis >= _synthScroll + 6) _synthScroll = vis - 5;
            }
            break;

        case Screen::PATTERN_SEL:
            _patSel = (uint8_t)((_patSel + delta + NUM_PATTERNS) % NUM_PATTERNS);
            break;

        case Screen::SCALE_SEL:
            if (_editMode) {
                // Edit root note
                _rootTmp = (uint8_t)((_rootTmp + delta + 12) % 12);
            } else {
                _scaleTmp = (uint8_t)((_scaleTmp + delta + NUM_SCALES) % NUM_SCALES);
            }
            break;

        case Screen::BPM_EDIT:
            _bpmTmp = constrain(_bpmTmp + delta, BPM_MIN, BPM_MAX);
            _seq->setBPM(_bpmTmp);
            break;

        case Screen::MAIN_MENU: {
            int m = (int)_menuSel + delta;
            _menuSel = (MenuItem)constrain(m, 0, (int)MenuItem::NUM_ITEMS - 1);
            break;
        }

        case Screen::SYNTH_MODE: {
            int m = (int)_modeTmp + delta;
            _modeTmp = (SynthMode)constrain(m, 0, (int)SynthMode::NUM_MODES - 1);
            break;
        }

        case Screen::MIDI_SETTINGS:
            _midiChTmp = (uint8_t)constrain((int)_midiChTmp + delta, 1, 16);
            break;

        case Screen::PATTERN_OPTS: {
            Pattern& p = _seq->getCurrentPattern();
            switch (_patOptSel) {
                case 0: p.length      = (uint8_t)constrain((int)p.length + delta, 1, NUM_STEPS); break;
                case 1: p.midiChannel = (uint8_t)constrain((int)p.midiChannel + delta, 1, 16);   break;
                case 2: p.swing       = (uint8_t)constrain((int)p.swing + delta, 0, SWING_MAX);  break;
            }
            break;
        }

        default: break;
    }
}

void UI::handleEncPress() {
    _dirty = true;
    switch(_screen) {
        case Screen::MAIN:
            // Enter step edit for selected step
            _stepField   = StepField::NOTE;
            _stepEditing = false;
            pushScreen(Screen::STEP_EDIT);
            break;

        case Screen::STEP_EDIT:
            _stepEditing = !_stepEditing;
            break;

        case Screen::SYNTH_PARAMS:
            _synthEditing = !_synthEditing;
            break;

        case Screen::PATTERN_SEL:
            _seq->setPattern(_patSel);
            popScreen();
            break;

        case Screen::SCALE_SEL:
            _editMode = !_editMode;  // toggle between scale/root editing
            break;

        case Screen::BPM_EDIT:
            popScreen();
            break;

        case Screen::MAIN_MENU:
            // Activate selected menu item
            switch(_menuSel) {
                case MenuItem::SEQ_STEP_EDIT:
                    _stepField   = StepField::NOTE;
                    _stepEditing = false;
                    pushScreen(Screen::STEP_EDIT);
                    break;
                case MenuItem::SEQ_BPM:
                    _bpmTmp = _seq->getBPM();
                    pushScreen(Screen::BPM_EDIT);
                    break;
                case MenuItem::PAT_SELECT:
                    _patSel = _seq->currentPattern();
                    pushScreen(Screen::PATTERN_SEL);
                    break;
                case MenuItem::PAT_OPTIONS:
                    _patOptSel = 0;
                    pushScreen(Screen::PATTERN_OPTS);
                    break;
                case MenuItem::SCALE_SEL:
                    _scaleTmp = _seq->getCurrentPattern().scaleIdx;
                    _rootTmp  = _seq->getCurrentPattern().rootNote;
                    _editMode = false;
                    pushScreen(Screen::SCALE_SEL);
                    break;
                case MenuItem::SYNTH_MODE:
                    _modeTmp = _engine->getParams().mode;
                    pushScreen(Screen::SYNTH_MODE);
                    break;
                case MenuItem::SYNTH_PARAMS:
                    _synthSel     = SynthParamID::MODE;
                    _synthScroll  = 0;
                    _synthEditing = false;
                    pushScreen(Screen::SYNTH_PARAMS);
                    break;
                case MenuItem::PAT_CHAIN:
                    pushScreen(Screen::CHAIN_EDIT);
                    break;
                case MenuItem::MIDI_SETTINGS:
                    _midiChTmp  = _seq->midiChannel();
                    _midiClkTmp = _seq->midiClockOut();
                    pushScreen(Screen::MIDI_SETTINGS);
                    break;
                case MenuItem::SETTINGS:
                    pushScreen(Screen::SETTINGS);
                    break;
                default: break;
            }
            break;

        case Screen::PATTERN_OPTS:
            _patOptSel = (_patOptSel + 1) % 3;   // cycle LEN → MIDI CH → SWING
            break;

        case Screen::MIDI_SETTINGS:
            _midiClkTmp = !_midiClkTmp;
            break;

        case Screen::SYNTH_MODE:
            // Apply mode selection
            _engine->getParams().mode = _modeTmp;
            // Load defaults for selected mode
            switch(_modeTmp) {
                case SynthMode::JUNO: {
                    const JunoPatch& jp = JUNO_PATCHES[_engine->getParams().junoPatch];
                    SynthParams& p = _engine->getParams();
                    p.osc2Level  = jp.pwm;
                    p.osc1Level  = jp.sawLevel;
                    p.subLevel   = jp.subLevel;
                    p.noiseLevel = jp.noiseLevel;
                    p.hpfCutoff  = jp.hpfCutoff;
                    p.filterCutoff = jp.lpfCutoff;
                    p.filterRes  = jp.lpfRes;
                    p.lfoRate    = jp.lfoRate;
                    p.lfoDepth   = jp.lfoFiltDepth;
                    p.lfoPwmDepth= jp.lfoPwmDepth;
                    p.chorus     = jp.chorus;
                    p.attack     = jp.attack;
                    p.decay      = jp.decay;
                    p.sustain    = jp.sustain;
                    p.release    = jp.release;
                    break;
                }
                case SynthMode::FM: {
                    const FMPatch& fp = FM_PATCHES[_engine->getParams().fmPatch];
                    SynthParams& p = _engine->getParams();
                    for(int i=0;i<NUM_FM_OPS;i++) {
                        p.opRatio[i]  = fp.opRatio[i];
                        p.opLevel[i]  = fp.opLevel[i];
                        p.opDecay[i]  = fp.opDecay[i];
                        p.opSustain[i]= fp.opSustain[i];
                    }
                    p.fmAlgo       = fp.algo;
                    p.opFeedback   = fp.feedback;
                    p.filterCutoff = fp.filterCutoff;
                    p.filterRes    = fp.filterRes;
                    break;
                }
                case SynthMode::BASS:
                    _engine->getParams().filterCutoff = 800;
                    _engine->getParams().filterRes    = 3.5f;
                    _engine->getParams().fEnvDec      = 0.12f;
                    _engine->getParams().fEnvSus      = 0.0f;
                    _engine->getParams().attack       = 0.002f;
                    _engine->getParams().decay        = 0.3f;
                    _engine->getParams().sustain      = 0.0f;
                    _engine->getParams().portaTime    = 0.05f;
                    break;
                case SynthMode::PAD:
                    _engine->getParams().attack  = 0.5f;
                    _engine->getParams().release = 1.5f;
                    _engine->getParams().filterCutoff = 2000;
                    _engine->getParams().reverbAmt = 0.5f;
                    break;
                case SynthMode::KEYS:
                    _engine->getParams().attack  = 0.001f;
                    _engine->getParams().release = 0.1f;
                    break;
                default: break;
            }
            _seq->getCurrentPattern().synth = _engine->getParams();
            _engine->setParams(_engine->getParams());   // push to AMY
            popScreen();
            break;

        default: break;
    }
}

void UI::handleBack() {
    _dirty = true;
    switch(_screen) {
        case Screen::MAIN:
            // Show main menu on back from main
            pushScreen(Screen::MAIN_MENU);
            break;
        case Screen::STEP_EDIT:
            if (_stepEditing) { _stepEditing = false; }
            else              { popScreen(); }
            break;
        case Screen::SYNTH_PARAMS:
            if (_synthEditing) { _synthEditing = false; }
            else               { popScreen(); }
            break;
        case Screen::SCALE_SEL:
            if (_editMode) { _editMode = false; }
            else {
                // Apply scale selection
                _seq->getCurrentPattern().rootNote = _rootTmp;
                _seq->getCurrentPattern().scaleIdx = _scaleTmp;
                popScreen();
            }
            break;
        case Screen::MAIN_MENU:
            // Always exit to MAIN — _prevScreen may point back at the menu
            // after a sub-screen pop, which would trap the user here.
            _screen = Screen::MAIN;
            break;
        default:
            popScreen();
            break;
    }
}

void UI::handleConfirm() {
    _dirty = true;
    switch(_screen) {
        case Screen::MAIN:
            _seq->togglePlay();
            break;
        case Screen::STEP_EDIT:
            // Move to next step
            _selStep = (_selStep + 1) % _seq->getCurrentPattern().length;
            _stepEditing = false;
            break;
        case Screen::SYNTH_PARAMS:
            if (!_synthEditing) _synthEditing = true;
            break;
        case Screen::SCALE_SEL:
            // Confirm and quantize
            _seq->getCurrentPattern().rootNote = _rootTmp;
            _seq->getCurrentPattern().scaleIdx = _scaleTmp;
            _seq->quantizePattern(_seq->currentPattern());
            popScreen();
            break;
        case Screen::MIDI_SETTINGS:
            _seq->getCurrentPattern().midiChannel = _midiChTmp;
            _seq->setMidiClock(_midiClkTmp);
            popScreen();
            break;
        default:
            popScreen();
            break;
    }
}

void UI::handleShift() {
    _editMode = !_editMode;
    _dirty = true;
}

void UI::handleLongBack() {
    if (_screen == Screen::MAIN) {
        // Long-press back on main = save current pattern + global settings
        storage.savePattern(_seq->currentPattern(), _seq->getCurrentPattern());
        GlobalSettings gs;
        gs.bpm          = _seq->getBPM();
        gs.lastPattern  = _seq->currentPattern();
        gs.midiClockOut = _seq->midiClockOut();
        gs.midiChannel  = _seq->midiChannel();
        gs.masterVol    = _engine->getParams().masterVol;
        storage.saveSettings(gs);
        _dirty = true;
    }
}

void UI::handleLongConfirm() {
    if (_screen == Screen::MAIN) {
        // Long-press confirm = toggle step active (quick toggle without entering edit)
        _seq->getStep(_selStep).active = !_seq->getStep(_selStep).active;
        _dirty = true;
    }
}

// ─── Step field change ────────────────────────────────────────────────────────

void UI::changeStepField(int delta) {
    Step& s = _seq->getStep(_selStep);
    Pattern& pat = _seq->getCurrentPattern();
    switch(_stepField) {
        case StepField::NOTE:
            s.note = (uint8_t)constrain((int)s.note + delta, 0, 127);
            s.active = true;
            break;
        case StepField::VELOCITY:
            s.velocity = (uint8_t)constrain((int)s.velocity + delta * 5, 1, 127);
            break;
        case StepField::GATE:
            s.gate = (uint8_t)constrain((int)s.gate + delta * 5, 1, 100);
            break;
        case StepField::PROBABILITY:
            s.probability = (uint8_t)constrain((int)s.probability + delta * 5, 0, 100);
            break;
        case StepField::ACCENT:
            if (delta != 0) s.accent = !s.accent;
            break;
        case StepField::SLIDE:
            if (delta != 0) s.slide = !s.slide;
            break;
        case StepField::ACTIVE:
            if (delta != 0) s.active = !s.active;
            break;
        default: break;
    }
}

// ─── Synth param helpers ──────────────────────────────────────────────────────

bool UI::isSynthParamVisible(SynthParamID id) {
    SynthMode m = _engine->getParams().mode;
    // Always visible
    if (id == SynthParamID::MODE || id == SynthParamID::MASTER_VOL) return true;
    if (id >= SynthParamID::FILTER_CUTOFF && id <= SynthParamID::HPF_CUTOFF) return true;
    if (id >= SynthParamID::AMP_ATK      && id <= SynthParamID::AMP_REL)    return true;
    if (id >= SynthParamID::REVERB_AMT   && id <= SynthParamID::DELAY_FEEDBACK) return true;
    // Mode-specific
    switch(m) {
        case SynthMode::DUAL_OSC:
            return (id >= SynthParamID::OSC1_WAVE && id <= SynthParamID::PULSE_WIDTH) ||
                   (id >= SynthParamID::FENV_ATK  && id <= SynthParamID::LFO_PWM_DEPTH) ||
                   id == SynthParamID::PORTA_TIME;
        case SynthMode::JUNO:
            return id == SynthParamID::OSC2_LEVEL  ||
                   id == SynthParamID::OSC1_LEVEL  ||
                   id == SynthParamID::SUB_LEVEL   ||
                   id == SynthParamID::NOISE_LEVEL ||
                   id == SynthParamID::PULSE_WIDTH ||
                   (id >= SynthParamID::LFO_WAVE && id <= SynthParamID::LFO_PWM_DEPTH) ||
                   id == SynthParamID::JUNO_PATCH  ||
                   id == SynthParamID::CHORUS_MODE ||
                   (id >= SynthParamID::FENV_ATK && id <= SynthParamID::FENV_REL);
        case SynthMode::FM:
            return id == SynthParamID::FM_ALGO     ||
                   id == SynthParamID::FM_PATCH    ||
                   (id >= SynthParamID::FM_OP1_RATIO && id <= SynthParamID::FM_FEEDBACK) ||
                   (id >= SynthParamID::FENV_ATK && id <= SynthParamID::FENV_REL);
        case SynthMode::BASS:
            return id == SynthParamID::OSC1_WAVE   ||
                   id == SynthParamID::PULSE_WIDTH ||
                   id == SynthParamID::SUB_LEVEL   ||
                   id == SynthParamID::PORTA_TIME  ||
                   (id >= SynthParamID::FENV_ATK && id <= SynthParamID::FENV_REL) ||
                   (id >= SynthParamID::LFO_WAVE && id <= SynthParamID::LFO_DEST);
        case SynthMode::PAD:
            return id == SynthParamID::CHORUS_MODE ||
                   (id >= SynthParamID::LFO_WAVE && id <= SynthParamID::LFO_DEST) ||
                   (id >= SynthParamID::FENV_ATK && id <= SynthParamID::FENV_REL);
        case SynthMode::KEYS:
            return id == SynthParamID::NOISE_LEVEL;
        default: return false;
    }
}

const char* UI::synthParamLabel(SynthParamID id) {
    switch(id) {
        case SynthParamID::MODE:          return "MODE";
        case SynthParamID::MASTER_VOL:    return "VOL";
        case SynthParamID::OSC1_WAVE:     return "OSC1";
        case SynthParamID::OSC1_LEVEL:    return "OSC1 LVL";
        case SynthParamID::OSC2_WAVE:     return "OSC2";
        case SynthParamID::OSC2_LEVEL:    return "OSC2 LVL";
        case SynthParamID::OSC2_DETUNE:   return "DETUNE";
        case SynthParamID::OSC2_COARSE:   return "COARSE";
        case SynthParamID::OSC_BALANCE:   return "BALANCE";
        case SynthParamID::NOISE_LEVEL:   return "NOISE";
        case SynthParamID::PULSE_WIDTH:   return "PW";
        case SynthParamID::SUB_LEVEL:     return "SUB";
        case SynthParamID::FILTER_CUTOFF: return "CUTOFF";
        case SynthParamID::FILTER_RES:    return "RES";
        case SynthParamID::FILTER_MODE:   return "FILT";
        case SynthParamID::FILTER_ENV_DEPTH: return "FENV";
        case SynthParamID::FILTER_KEYTRACK:  return "KEYTRK";
        case SynthParamID::HPF_CUTOFF:    return "HPF";
        case SynthParamID::AMP_ATK:       return "ATK";
        case SynthParamID::AMP_DEC:       return "DEC";
        case SynthParamID::AMP_SUS:       return "SUS";
        case SynthParamID::AMP_REL:       return "REL";
        case SynthParamID::FENV_ATK:      return "FATK";
        case SynthParamID::FENV_DEC:      return "FDEC";
        case SynthParamID::FENV_SUS:      return "FSUS";
        case SynthParamID::FENV_REL:      return "FREL";
        case SynthParamID::LFO_WAVE:      return "LFOW";
        case SynthParamID::LFO_RATE:      return "LRATE";
        case SynthParamID::LFO_DEPTH:     return "LDEPTH";
        case SynthParamID::LFO_DEST:      return "LDEST";
        case SynthParamID::LFO_PWM_DEPTH: return "LPWM";
        case SynthParamID::PORTA_TIME:    return "PORTA";
        case SynthParamID::JUNO_PATCH:    return "J.PATCH";
        case SynthParamID::CHORUS_MODE:   return "CHORUS";
        case SynthParamID::CHORUS_DEPTH:  return "CH.DPTH";
        case SynthParamID::CHORUS_RATE:   return "CH.RATE";
        case SynthParamID::FM_ALGO:       return "ALGO";
        case SynthParamID::FM_PATCH:      return "FM.PTCH";
        case SynthParamID::FM_OP1_RATIO:  return "OP1 RAT";
        case SynthParamID::FM_OP1_LEVEL:  return "OP1 LVL";
        case SynthParamID::FM_OP1_DECAY:  return "OP1 DEC";
        case SynthParamID::FM_OP2_RATIO:  return "OP2 RAT";
        case SynthParamID::FM_OP2_LEVEL:  return "OP2 LVL";
        case SynthParamID::FM_OP2_DECAY:  return "OP2 DEC";
        case SynthParamID::FM_OP3_RATIO:  return "OP3 RAT";
        case SynthParamID::FM_OP3_LEVEL:  return "OP3 LVL";
        case SynthParamID::FM_OP3_DECAY:  return "OP3 DEC";
        case SynthParamID::FM_OP4_RATIO:  return "OP4 RAT";
        case SynthParamID::FM_OP4_LEVEL:  return "OP4 LVL";
        case SynthParamID::FM_OP4_DECAY:  return "OP4 DEC";
        case SynthParamID::FM_FEEDBACK:   return "FDBK";
        case SynthParamID::REVERB_AMT:    return "REVERB";
        case SynthParamID::DELAY_TIME:    return "DELAY";
        case SynthParamID::DELAY_FEEDBACK:return "DEL.FB";
        default: return "---";
    }
}

const char* UI::synthParamValue(SynthParamID id, char* buf) {
    SynthParams& p = _engine->getParams();
    switch(id) {
        case SynthParamID::MODE:
            return SYNTH_MODE_NAMES[(int)p.mode];
        case SynthParamID::OSC1_WAVE:
            return WAVEFORM_NAMES[(int)p.osc1Wave];
        case SynthParamID::OSC2_WAVE:
            return WAVEFORM_NAMES[(int)p.osc2Wave];
        case SynthParamID::LFO_WAVE:
            return WAVEFORM_NAMES[(int)p.lfoWave];
        case SynthParamID::LFO_DEST:
            return LFO_DEST_NAMES[(int)p.lfoDest];
        case SynthParamID::FILTER_MODE:
            return FILTER_MODE_NAMES[(int)p.filterMode];
        case SynthParamID::CHORUS_MODE:
            return CHORUS_NAMES[(int)p.chorus];
        case SynthParamID::FM_ALGO:
            snprintf(buf, 8, "ALG%d", p.fmAlgo + 1);
            return buf;
        case SynthParamID::JUNO_PATCH:
            return JUNO_PATCHES[p.junoPatch].name;
        case SynthParamID::FM_PATCH:
            return FM_PATCHES[p.fmPatch].name;
        case SynthParamID::FILTER_CUTOFF:
            snprintf(buf, 8, "%dHz", (int)p.filterCutoff);
            return buf;
        case SynthParamID::HPF_CUTOFF:
            snprintf(buf, 8, "%dHz", (int)p.hpfCutoff);
            return buf;
        case SynthParamID::OSC2_DETUNE:
            snprintf(buf, 8, "%.2f", p.osc2Detune);
            return buf;
        case SynthParamID::OSC2_COARSE:
            snprintf(buf, 8, "%+d", (int)p.osc2Coarse);
            return buf;
        default: {
            float v = getSynthParamF(id);
            if (v >= 10.0f) snprintf(buf, 8, "%.1f", v);
            else            snprintf(buf, 8, "%.2f", v);
            return buf;
        }
    }
}

float UI::getSynthParamF(SynthParamID id) {
    SynthParams& p = _engine->getParams();
    switch(id) {
        case SynthParamID::MASTER_VOL:       return p.masterVol;
        case SynthParamID::OSC1_LEVEL:       return p.osc1Level;
        case SynthParamID::OSC2_LEVEL:       return p.osc2Level;
        case SynthParamID::OSC2_DETUNE:      return p.osc2Detune;
        case SynthParamID::OSC2_COARSE:      return p.osc2Coarse;
        case SynthParamID::OSC_BALANCE:      return p.oscBalance;
        case SynthParamID::NOISE_LEVEL:      return p.noiseLevel;
        case SynthParamID::PULSE_WIDTH:      return p.pulseWidth;
        case SynthParamID::SUB_LEVEL:        return p.subLevel;
        case SynthParamID::FILTER_CUTOFF:    return p.filterCutoff;
        case SynthParamID::FILTER_RES:       return p.filterRes;
        case SynthParamID::FILTER_ENV_DEPTH: return p.filterEnvDepth;
        case SynthParamID::FILTER_KEYTRACK:  return p.filterKeyTrack;
        case SynthParamID::HPF_CUTOFF:       return p.hpfCutoff;
        case SynthParamID::AMP_ATK:          return p.attack;
        case SynthParamID::AMP_DEC:          return p.decay;
        case SynthParamID::AMP_SUS:          return p.sustain;
        case SynthParamID::AMP_REL:          return p.release;
        case SynthParamID::FENV_ATK:         return p.fEnvAtk;
        case SynthParamID::FENV_DEC:         return p.fEnvDec;
        case SynthParamID::FENV_SUS:         return p.fEnvSus;
        case SynthParamID::FENV_REL:         return p.fEnvRel;
        case SynthParamID::LFO_RATE:         return p.lfoRate;
        case SynthParamID::LFO_DEPTH:        return p.lfoDepth;
        case SynthParamID::LFO_PWM_DEPTH:    return p.lfoPwmDepth;
        case SynthParamID::PORTA_TIME:       return p.portaTime;
        case SynthParamID::CHORUS_DEPTH:     return p.chorusDepth;
        case SynthParamID::CHORUS_RATE:      return p.chorusRate;
        case SynthParamID::FM_OP1_RATIO:     return p.opRatio[0];
        case SynthParamID::FM_OP1_LEVEL:     return p.opLevel[0];
        case SynthParamID::FM_OP1_DECAY:     return p.opDecay[0];
        case SynthParamID::FM_OP2_RATIO:     return p.opRatio[1];
        case SynthParamID::FM_OP2_LEVEL:     return p.opLevel[1];
        case SynthParamID::FM_OP2_DECAY:     return p.opDecay[1];
        case SynthParamID::FM_OP3_RATIO:     return p.opRatio[2];
        case SynthParamID::FM_OP3_LEVEL:     return p.opLevel[2];
        case SynthParamID::FM_OP3_DECAY:     return p.opDecay[2];
        case SynthParamID::FM_OP4_RATIO:     return p.opRatio[3];
        case SynthParamID::FM_OP4_LEVEL:     return p.opLevel[3];
        case SynthParamID::FM_OP4_DECAY:     return p.opDecay[3];
        case SynthParamID::FM_FEEDBACK:      return p.opFeedback;
        case SynthParamID::REVERB_AMT:       return p.reverbAmt;
        case SynthParamID::DELAY_TIME:       return p.delayTime;
        case SynthParamID::DELAY_FEEDBACK:   return p.delayFeedback;
        default: return 0;
    }
}

float UI::synthParamMin(SynthParamID id) {
    switch(id) {
        case SynthParamID::FILTER_CUTOFF: return 50;
        case SynthParamID::HPF_CUTOFF:   return 20;
        case SynthParamID::FILTER_RES:   return 0.5f;
        case SynthParamID::OSC2_DETUNE:  return -12;
        case SynthParamID::OSC2_COARSE:  return -24;
        case SynthParamID::LFO_RATE:     return 0.05f;
        case SynthParamID::FM_OP1_RATIO: case SynthParamID::FM_OP2_RATIO:
        case SynthParamID::FM_OP3_RATIO: case SynthParamID::FM_OP4_RATIO: return 0.25f;
        default: return 0;
    }
}

float UI::synthParamMax(SynthParamID id) {
    switch(id) {
        case SynthParamID::FILTER_CUTOFF: return 8000;
        case SynthParamID::HPF_CUTOFF:   return 2000;
        case SynthParamID::FILTER_RES:   return 12.0f;
        case SynthParamID::OSC2_DETUNE:  return 12;
        case SynthParamID::OSC2_COARSE:  return 24;
        case SynthParamID::LFO_RATE:     return 20;
        case SynthParamID::AMP_ATK: case SynthParamID::AMP_DEC:
        case SynthParamID::AMP_REL: case SynthParamID::FENV_ATK:
        case SynthParamID::FENV_DEC: case SynthParamID::FENV_REL: return 5.0f;
        case SynthParamID::DELAY_TIME:   return 0.5f;
        case SynthParamID::FM_OP1_RATIO: case SynthParamID::FM_OP2_RATIO:
        case SynthParamID::FM_OP3_RATIO: case SynthParamID::FM_OP4_RATIO: return 20;
        case SynthParamID::REVERB_AMT:   return 1.0f;
        default: return 1.0f;
    }
}

float UI::synthParamStep(SynthParamID id) {
    switch(id) {
        case SynthParamID::FILTER_CUTOFF: return 100;
        case SynthParamID::HPF_CUTOFF:   return 20;
        case SynthParamID::FILTER_RES:   return 0.1f;
        case SynthParamID::AMP_ATK: case SynthParamID::AMP_DEC:
        case SynthParamID::AMP_REL: case SynthParamID::FENV_ATK:
        case SynthParamID::FENV_DEC: case SynthParamID::FENV_REL: return 0.01f;
        case SynthParamID::OSC2_DETUNE:  return 0.1f;
        case SynthParamID::OSC2_COARSE:  return 1.0f;
        case SynthParamID::LFO_RATE:     return 0.1f;
        case SynthParamID::FM_OP1_RATIO: case SynthParamID::FM_OP2_RATIO:
        case SynthParamID::FM_OP3_RATIO: case SynthParamID::FM_OP4_RATIO: return 0.25f;
        case SynthParamID::DELAY_TIME:   return 0.01f;
        default: return 0.01f;
    }
}

void UI::setSynthParamF(SynthParamID id, float v) {
    SynthParams& p = _engine->getParams();
    switch(id) {
        case SynthParamID::MASTER_VOL:       p.masterVol = v; break;
        case SynthParamID::OSC1_LEVEL:       p.osc1Level = v; break;
        case SynthParamID::OSC2_LEVEL:       p.osc2Level = v; break;
        case SynthParamID::OSC2_DETUNE:      p.osc2Detune = v; break;
        case SynthParamID::OSC2_COARSE:      p.osc2Coarse = v; break;
        case SynthParamID::OSC_BALANCE:      p.oscBalance = v; break;
        case SynthParamID::NOISE_LEVEL:      p.noiseLevel = v; break;
        case SynthParamID::PULSE_WIDTH:      p.pulseWidth = v; break;
        case SynthParamID::SUB_LEVEL:        p.subLevel   = v; break;
        case SynthParamID::FILTER_CUTOFF:    p.filterCutoff = v; break;
        case SynthParamID::FILTER_RES:       p.filterRes = v; break;
        case SynthParamID::FILTER_ENV_DEPTH: p.filterEnvDepth = v; break;
        case SynthParamID::FILTER_KEYTRACK:  p.filterKeyTrack = v; break;
        case SynthParamID::HPF_CUTOFF:       p.hpfCutoff = v; break;
        case SynthParamID::AMP_ATK:          p.attack  = v; break;
        case SynthParamID::AMP_DEC:          p.decay   = v; break;
        case SynthParamID::AMP_SUS:          p.sustain = v; break;
        case SynthParamID::AMP_REL:          p.release = v; break;
        case SynthParamID::FENV_ATK:         p.fEnvAtk = v; break;
        case SynthParamID::FENV_DEC:         p.fEnvDec = v; break;
        case SynthParamID::FENV_SUS:         p.fEnvSus = v; break;
        case SynthParamID::FENV_REL:         p.fEnvRel = v; break;
        case SynthParamID::LFO_RATE:         p.lfoRate = v; break;
        case SynthParamID::LFO_DEPTH:        p.lfoDepth = v; break;
        case SynthParamID::LFO_PWM_DEPTH:    p.lfoPwmDepth = v; break;
        case SynthParamID::PORTA_TIME:       p.portaTime = v; break;
        case SynthParamID::CHORUS_DEPTH:     p.chorusDepth = v; break;
        case SynthParamID::CHORUS_RATE:      p.chorusRate = v; break;
        case SynthParamID::FM_OP1_RATIO:     p.opRatio[0] = v; break;
        case SynthParamID::FM_OP1_LEVEL:     p.opLevel[0] = v; break;
        case SynthParamID::FM_OP1_DECAY:     p.opDecay[0] = v; break;
        case SynthParamID::FM_OP2_RATIO:     p.opRatio[1] = v; break;
        case SynthParamID::FM_OP2_LEVEL:     p.opLevel[1] = v; break;
        case SynthParamID::FM_OP2_DECAY:     p.opDecay[1] = v; break;
        case SynthParamID::FM_OP3_RATIO:     p.opRatio[2] = v; break;
        case SynthParamID::FM_OP3_LEVEL:     p.opLevel[2] = v; break;
        case SynthParamID::FM_OP3_DECAY:     p.opDecay[2] = v; break;
        case SynthParamID::FM_OP4_RATIO:     p.opRatio[3] = v; break;
        case SynthParamID::FM_OP4_LEVEL:     p.opLevel[3] = v; break;
        case SynthParamID::FM_OP4_DECAY:     p.opDecay[3] = v; break;
        case SynthParamID::FM_FEEDBACK:      p.opFeedback = v; break;
        case SynthParamID::REVERB_AMT:       p.reverbAmt = v; break;
        case SynthParamID::DELAY_TIME:       p.delayTime = v; break;
        case SynthParamID::DELAY_FEEDBACK:   p.delayFeedback = v; break;
        default: break;
    }
    // Propagate to current pattern
    _seq->getCurrentPattern().synth = p;
}

void UI::setSynthParamI(SynthParamID id, int v) {
    SynthParams& p = _engine->getParams();
    switch(id) {
        case SynthParamID::MODE:        p.mode       = (SynthMode)v; break;
        case SynthParamID::OSC1_WAVE:   p.osc1Wave   = (WaveType)v; break;
        case SynthParamID::OSC2_WAVE:   p.osc2Wave   = (WaveType)v; break;
        case SynthParamID::LFO_WAVE:    p.lfoWave    = (WaveType)v; break;
        case SynthParamID::LFO_DEST:    p.lfoDest    = (LFODest)v; break;
        case SynthParamID::FILTER_MODE: p.filterMode = (FilterMode)v; break;
        case SynthParamID::CHORUS_MODE: p.chorus     = (ChorusMode)v; break;
        case SynthParamID::FM_ALGO:     p.fmAlgo     = (uint8_t)v; break;
        case SynthParamID::JUNO_PATCH:  p.junoPatch  = (uint8_t)v; break;
        case SynthParamID::FM_PATCH:    p.fmPatch    = (uint8_t)v; break;
        default: break;
    }
    _seq->getCurrentPattern().synth = p;
}

void UI::changeSynthParam(int delta) {
    SynthParamID id = _synthSel;
    SynthParams& p = _engine->getParams();

    // Integer / enum params
    switch(id) {
        case SynthParamID::MODE: {
            int v = (int)p.mode + delta;
            setSynthParamI(id, constrain(v, 0, (int)SynthMode::NUM_MODES - 1));
            applyEngine(id);
            return;
        }
        case SynthParamID::OSC1_WAVE: {
            int v = (int)p.osc1Wave + delta;
            setSynthParamI(id, constrain(v, 0, (int)WaveType::NUM_WAVES - 1));
            applyEngine(id);
            return;
        }
        case SynthParamID::OSC2_WAVE: {
            int v = (int)p.osc2Wave + delta;
            setSynthParamI(id, constrain(v, 0, (int)WaveType::NUM_WAVES - 1));
            applyEngine(id);
            return;
        }
        case SynthParamID::LFO_WAVE: {
            int v = (int)p.lfoWave + delta;
            setSynthParamI(id, constrain(v, 0, (int)WaveType::NUM_WAVES - 1));
            applyEngine(id);
            return;
        }
        case SynthParamID::LFO_DEST: {
            int v = (int)p.lfoDest + delta;
            setSynthParamI(id, constrain(v, 0, (int)LFODest::NUM_DESTS - 1));
            applyEngine(id);
            return;
        }
        case SynthParamID::FILTER_MODE: {
            int v = (int)p.filterMode + delta;
            setSynthParamI(id, constrain(v, 0, 3));
            applyEngine(id);
            return;
        }
        case SynthParamID::CHORUS_MODE: {
            int v = (int)p.chorus + delta;
            setSynthParamI(id, constrain(v, 0, 2));
            applyEngine(id);
            return;
        }
        case SynthParamID::FM_ALGO: {
            int v = (int)p.fmAlgo + delta;
            setSynthParamI(id, constrain(v, 0, 7));
            applyEngine(id);
            return;
        }
        case SynthParamID::JUNO_PATCH: {
            int v = (int)p.junoPatch + delta;
            int next = constrain(v, 0, JUNO_PATCHES_COUNT - 1);
            setSynthParamI(id, next);
            // Load patch defaults
            const JunoPatch& jp = JUNO_PATCHES[next];
            p.osc2Level  = jp.pwm;
            p.osc1Level  = jp.sawLevel;
            p.subLevel   = jp.subLevel;
            p.noiseLevel = jp.noiseLevel;
            p.hpfCutoff  = jp.hpfCutoff;
            p.filterCutoff = jp.lpfCutoff;
            p.filterRes  = jp.lpfRes;
            p.lfoRate    = jp.lfoRate;
            p.chorus     = jp.chorus;
            applyEngine(id);
            return;
        }
        case SynthParamID::FM_PATCH: {
            int v = (int)p.fmPatch + delta;
            int next = constrain(v, 0, FM_PATCHES_COUNT - 1);
            setSynthParamI(id, next);
            const FMPatch& fp = FM_PATCHES[next];
            for(int i=0;i<NUM_FM_OPS;i++) {
                p.opRatio[i]  = fp.opRatio[i];
                p.opLevel[i]  = fp.opLevel[i];
                p.opDecay[i]  = fp.opDecay[i];
                p.opSustain[i]= fp.opSustain[i];
            }
            p.fmAlgo = fp.algo;
            p.opFeedback = fp.feedback;
            applyEngine(id);
            return;
        }
        default: break;
    }

    // Float params
    float step = synthParamStep(id);
    float mn   = synthParamMin(id);
    float mx   = synthParamMax(id);
    float cur  = getSynthParamF(id);
    setSynthParamF(id, constrain(cur + delta * step, mn, mx));
    applyEngine(id);
}

// ─── Engine apply ─────────────────────────────────────────────────────────────
// Structural changes (mode, patch, oscillator setup) need a full voice
// reconfig; everything else only needs the cheap ADSR/filter/FX refresh.

void UI::applyEngine(SynthParamID id) {
    switch(id) {
        case SynthParamID::MODE:
        case SynthParamID::JUNO_PATCH:
        case SynthParamID::FM_PATCH:
        case SynthParamID::FM_ALGO:
        case SynthParamID::OSC1_WAVE:
        case SynthParamID::OSC2_WAVE:
        case SynthParamID::OSC1_LEVEL:
        case SynthParamID::OSC2_LEVEL:
        case SynthParamID::OSC2_DETUNE:
        case SynthParamID::OSC2_COARSE:
        case SynthParamID::NOISE_LEVEL:
            _engine->setParams(_engine->getParams());
            break;
        default:
            _engine->refresh();
            break;
    }
}

// ─── Visible-param navigation ─────────────────────────────────────────────────

SynthParamID UI::stepVisibleParam(SynthParamID from, int dir) {
    int i = (int)from;
    while (true) {
        i += dir;
        if (i < 0 || i >= (int)SynthParamID::NUM_PARAMS) return from;
        if (isSynthParamVisible((SynthParamID)i)) return (SynthParamID)i;
    }
}

int UI::visibleIndexOf(SynthParamID id) {
    int v = 0;
    for (int i = 0; i < (int)id; i++) {
        if (isSynthParamVisible((SynthParamID)i)) v++;
    }
    return v;
}

// ─── Drawing ─────────────────────────────────────────────────────────────────

void UI::drawAll() {
    _u8g2.clearBuffer();
    switch(_screen) {
        case Screen::MAIN:          drawHeader(); drawMainScreen();   break;
        case Screen::STEP_EDIT:     drawHeader(); drawStepEdit();     break;
        case Screen::SYNTH_PARAMS:  drawHeader(); drawSynthParams();  break;
        case Screen::SYNTH_MODE:    drawHeader(); drawSynthMode();    break;
        case Screen::PATTERN_SEL:   drawHeader(); drawPatternSel();   break;
        case Screen::PATTERN_OPTS:  drawHeader(); drawPatternOpts();  break;
        case Screen::CHAIN_EDIT:    drawHeader(); drawChainEdit();    break;
        case Screen::SCALE_SEL:     drawHeader(); drawScaleSel();     break;
        case Screen::BPM_EDIT:      drawBPMEdit();                   break;
        case Screen::MIDI_SETTINGS: drawHeader(); drawMIDISettings(); break;
        case Screen::SETTINGS:      drawHeader(); drawSettings();     break;
        case Screen::MAIN_MENU:     drawHeader(); drawMainMenu();     break;
        default: drawHeader(); break;
    }
    _u8g2.sendBuffer();
}

// ─── Header bar ──────────────────────────────────────────────────────────────

void UI::drawHeader() {
    const SynthParams& sp = _engine->getParams();

    // Full-width inverted bar
    _u8g2.setDrawColor(1);
    _u8g2.drawBox(0, 0, DISP_W, HEADER_H);
    _u8g2.setDrawColor(0);   // everything inside = white on black

    bool playing = (_seq->getPlayState() == PlayState::PLAYING);

    // Play/stop symbol (hand-drawn, 5px wide)
    if (playing) {
        // Right-pointing triangle ▶ — columns shrink toward the tip
        _u8g2.drawVLine(1, 2, 7);   // left edge (tallest)
        _u8g2.drawVLine(2, 3, 5);
        _u8g2.drawVLine(3, 4, 3);
        _u8g2.drawVLine(4, 5, 1);   // tip
    } else {
        _u8g2.drawBox(1, 2, 6, 6);  // filled square ■ (stop)
    }

    // BPM number (5x7 — most prominent element in header)
    _u8g2.setFont(u8g2_font_5x7_tr);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", _seq->getBPM());
    _u8g2.drawStr(8, 8, buf);

    // Small labels and info (4x6)
    _u8g2.setFont(u8g2_font_4x6_tr);
    _u8g2.drawStr(29, 8, "BPM");

    char patBuf[4];
    snprintf(patBuf, sizeof(patBuf), "P%d", _seq->currentPattern() + 1);
    _u8g2.drawStr(48, 8, patBuf);

    _u8g2.drawStr(64, 8, SYNTH_MODE_NAMES[(int)sp.mode]);

    // Beat position indicator (4 squares, far right)
    // Each square: 5×6 px, 1px gap between
    uint8_t beat = (uint8_t)((_seq->currentStep() / 4) % 4);
    for (int b = 0; b < 4; b++) {
        uint8_t bx = (uint8_t)(104 + b * 6);
        if (playing && b == (int)beat) {
            _u8g2.drawBox(bx, 2, 5, 6);      // filled = current beat
        } else {
            _u8g2.drawFrame(bx, 2, 5, 6);    // hollow = other beats
        }
    }

    _u8g2.setDrawColor(1);
}

// ─── Step cell ───────────────────────────────────────────────────────────────
// Visual language:
//   Inactive   : four corner dots only — ghost placeholder
//   Active     : rounded outline + velocity bar filling from the bottom
//   Cursor/Play: inverted (white fill) + velocity bar as dark cutout
//   Accent     : 2×2 dot in top-right corner
//   Slide      : 3-pixel line at bottom-left

void UI::drawStepCell(uint8_t step, uint8_t x, uint8_t y, bool cursor, bool playing) {
    const Step& s    = _seq->getStep(step);
    uint8_t     w    = STEP_CELL_W - 1;   // 15 px drawn
    uint8_t     h    = STEP_CELL_H;        // 12 px

    // Beyond pattern length — ghost pixel only (no cell border)
    if (step >= _seq->getCurrentPattern().length) {
        _u8g2.setDrawColor(1);
        _u8g2.drawPixel(x + w / 2, y + h / 2);
        if (cursor) _u8g2.drawRFrame(x, y, w, h, 1);
        return;
    }

    bool highlight = cursor || playing;

    if (highlight) {
        // Inverted cell — white fill, content drawn in black
        _u8g2.setDrawColor(1);
        _u8g2.drawRBox(x, y, w, h, 1);
        _u8g2.setDrawColor(0);

        if (s.active) {
            // Velocity bar: black fill from bottom
            uint8_t barH = (uint8_t)((s.velocity * (uint16_t)(h - 4)) / 127);
            if (barH > 0)
                _u8g2.drawBox(x + 1, (uint8_t)(y + h - 1 - barH), (uint8_t)(w - 2), barH);
            if (s.accent) _u8g2.drawBox(x + w - 3, y + 1, 2, 2);
            if (s.slide)  _u8g2.drawHLine(x + 1, y + h - 2, 4);
        }

    } else if (s.active) {
        // Active, not selected: outline + white velocity fill
        _u8g2.setDrawColor(1);
        _u8g2.drawRFrame(x, y, w, h, 1);

        uint8_t barH = (uint8_t)((s.velocity * (uint16_t)(h - 4)) / 127);
        if (barH > 0)
            _u8g2.drawBox(x + 1, (uint8_t)(y + h - 1 - barH), (uint8_t)(w - 2), barH);
        if (s.accent) _u8g2.drawBox(x + w - 3, y + 1, 2, 2);
        if (s.slide)  _u8g2.drawHLine(x + 1, y + h - 2, 4);

    } else {
        // Inactive: four corner dots — minimal presence
        _u8g2.setDrawColor(1);
        _u8g2.drawPixel(x + 1,     y + 1);
        _u8g2.drawPixel(x + w - 2, y + 1);
        _u8g2.drawPixel(x + 1,     y + h - 2);
        _u8g2.drawPixel(x + w - 2, y + h - 2);
        if (cursor) _u8g2.drawRFrame(x, y, w, h, 1);
    }

    _u8g2.setDrawColor(1);
}

// ─── Main screen ─────────────────────────────────────────────────────────────

void UI::drawMainScreen() {
    uint8_t curStep = _seq->currentStep();
    bool    playing = (_seq->getPlayState() == PlayState::PLAYING);

    // ── Step grid ────────────────────────────────────────────────────────────
    for (uint8_t s = 0; s < 8; s++) {
        drawStepCell(s, (uint8_t)(s * STEP_CELL_W), STEP_ROW1_Y,
                     s == _selStep, playing && s == curStep);
    }
    for (uint8_t s = 8; s < 16; s++) {
        drawStepCell(s, (uint8_t)((s - 8) * STEP_CELL_W), STEP_ROW2_Y,
                     s == _selStep, playing && s == curStep);
    }

    // ── Progress bar (2 px, directly below row 2) ─────────────────────────
    const uint8_t barY = STEP_ROW2_Y + STEP_CELL_H + 1;  // y=37
    if (playing) {
        uint16_t prog = _seq->stepProgress();
        _u8g2.drawHLine(0, barY, DISP_W);
        _u8g2.drawBox(0, barY, (uint8_t)(prog * DISP_W / 1000), 2);
    }

    // ── Separator ─────────────────────────────────────────────────────────
    _u8g2.drawHLine(0, (uint8_t)(barY + 3), DISP_W);   // y=40

    // ── Info area ─────────────────────────────────────────────────────────
    const Step& sel  = _seq->getStep(_selStep);
    const uint8_t iy = barY + 5;   // info area top = y=42

    // Line 1: Note  V:vel  G:gate%
    char noteBuf[6];
    if (sel.active) noteName(sel.note, noteBuf);
    else            strcpy(noteBuf, "---");

    char line1[28];
    snprintf(line1, sizeof(line1), "%s  V:%d  G:%d%%",
             noteBuf, sel.velocity, sel.gate);
    _u8g2.setFont(u8g2_font_5x7_tr);
    _u8g2.drawStr(0, (uint8_t)(iy + 8), line1);          // baseline y=50

    // Line 2: P:prob%  then badge pills for ACC / SLD
    char probBuf[10];
    snprintf(probBuf, sizeof(probBuf), "P:%d%%", sel.probability);
    _u8g2.drawStr(0, (uint8_t)(iy + 19), probBuf);       // baseline y=61

    // Badge helper: filled pill when active, lowercase plain text when not.
    // Badge box top = iy+11 (y=53), height=9, text baseline = iy+19 (y=61)
    auto badge = [&](uint8_t bx, const char* labelOn, const char* labelOff, bool on) {
        if (on) {
            _u8g2.drawBox(bx, (uint8_t)(iy + 11), 19, 9);
            _u8g2.setDrawColor(0);
            _u8g2.drawStr((uint8_t)(bx + 2), (uint8_t)(iy + 19), labelOn);
            _u8g2.setDrawColor(1);
        } else {
            _u8g2.drawStr((uint8_t)(bx + 2), (uint8_t)(iy + 19), labelOff);
        }
    };

    badge(42, "ACC", "acc", sel.accent);
    badge(65, "SLD", "sld", sel.slide);

    // Right-edge indicator: BPM-edit badge when shift held, step number otherwise
    _u8g2.setFont(u8g2_font_4x6_tr);
    if (_editMode) {
        _u8g2.drawBox(104, (uint8_t)(iy + 11), 24, 9);
        _u8g2.setDrawColor(0);
        _u8g2.drawStr(107, (uint8_t)(iy + 19), "BPM");
        _u8g2.setDrawColor(1);
    } else {
        char stepBuf[6];
        snprintf(stepBuf, sizeof(stepBuf), "S%02d", _selStep + 1);
        _u8g2.drawStr(110, (uint8_t)(iy + 19), stepBuf);
    }
}

// ─── Step edit screen ────────────────────────────────────────────────────────

void UI::drawStepEdit() {
    Step& s = _seq->getStep(_selStep);

    _u8g2.setFont(u8g2_font_5x7_tr);
    char title[16];
    snprintf(title, sizeof(title), "STEP %d EDIT", _selStep + 1);
    _u8g2.drawStr(0, 20, title);

    _u8g2.drawHLine(0, 22, DISP_W);

    // 7 fields don't fit below the title — show a 4-row window that
    // scrolls to keep the selected field visible.
    const int ROW_H   = 10;
    const int VISIBLE = 4;
    int first = constrain((int)_stepField - (VISIBLE - 1),
                          0, (int)StepField::NUM_FIELDS - VISIBLE);
    int y   = 32;
    int idx = 0;

    auto row = [&](StepField field, const char* label, const char* val) {
        int i = idx++;
        if (i < first || i >= first + VISIBLE) return;
        bool sel = (field == _stepField);
        bool edit= sel && _stepEditing;
        if (sel) {
            _u8g2.drawBox(0, y - 8, DISP_W, ROW_H);
            _u8g2.setDrawColor(0);
        }
        _u8g2.setFont(u8g2_font_5x7_tr);
        _u8g2.drawStr(2, y, label);
        if (edit) {
            _u8g2.drawStr(60, y, "[");
            _u8g2.drawStr(67, y, val);
            _u8g2.drawStr(67 + _u8g2.getStrWidth(val), y, "]");
        } else {
            _u8g2.drawStr(60, y, val);
        }
        _u8g2.setDrawColor(1);
        y += ROW_H;
    };

    char buf[16];
    char noteBuf[6];

    noteName(s.note, noteBuf);
    if (!s.active) strcpy(noteBuf, "---");
    row(StepField::NOTE,        "NOTE",  noteBuf);

    snprintf(buf, sizeof(buf), "%d", s.velocity);
    row(StepField::VELOCITY,    "VEL",   buf);

    snprintf(buf, sizeof(buf), "%d%%", s.gate);
    row(StepField::GATE,        "GATE",  buf);

    snprintf(buf, sizeof(buf), "%d%%", s.probability);
    row(StepField::PROBABILITY, "PROB",  buf);

    row(StepField::ACCENT, "ACCENT", s.accent ? "ON" : "OFF");
    row(StepField::SLIDE,  "SLIDE",  s.slide  ? "ON" : "OFF");
    row(StepField::ACTIVE, "ACTIVE", s.active ? "ON" : "OFF");
}

// ─── Synth mode selection ─────────────────────────────────────────────────────

void UI::drawSynthMode() {
    _u8g2.setFont(u8g2_font_5x7_tr);
    _u8g2.drawStr(0, 20, "SYNTH MODE");
    _u8g2.drawHLine(0, 22, DISP_W);

    for (int i = 0; i < (int)SynthMode::NUM_MODES; i++) {
        int y = 31 + i * 9;
        if ((SynthMode)i == _modeTmp) {
            _u8g2.drawBox(0, y - 7, DISP_W, 9);
            _u8g2.setDrawColor(0);
        }
        _u8g2.drawStr(4, y, SYNTH_MODE_NAMES[i]);
        _u8g2.setDrawColor(1);
    }
}

// ─── Synth params screen ──────────────────────────────────────────────────────

void UI::drawSynthParams() {
    _u8g2.setFont(u8g2_font_4x6_tr);

    const int ROWS_VISIBLE = 6;
    const int ROW_H = 9;
    int y = HEADER_H + 1;

    int row = 0;
    int visRow = 0;
    for (int i = 0; i < (int)SynthParamID::NUM_PARAMS && visRow < ROWS_VISIBLE; i++) {
        SynthParamID id = (SynthParamID)i;
        if (!isSynthParamVisible(id)) continue;
        if (row < _synthScroll) { row++; continue; }

        bool sel  = ((SynthParamID)i == _synthSel);
        bool edit = sel && _synthEditing;

        if (sel) {
            _u8g2.drawBox(0, y, DISP_W, ROW_H);
            _u8g2.setDrawColor(0);
        }

        // Label (left)
        _u8g2.setFont(u8g2_font_4x6_tr);
        _u8g2.drawStr(1, y + 7, synthParamLabel(id));

        // Value (right)
        char valBuf[12];
        const char* valStr = synthParamValue(id, valBuf);
        int vw = _u8g2.getStrWidth(valStr);
        if (edit) {
            _u8g2.drawStr(DISP_W - vw - 8, y + 7, "<");
            _u8g2.drawStr(DISP_W - vw - 4, y + 7, valStr);
            _u8g2.drawStr(DISP_W - 4, y + 7, ">");
        } else {
            _u8g2.drawStr(DISP_W - vw - 1, y + 7, valStr);
        }

        _u8g2.setDrawColor(1);
        y += ROW_H;
        row++;
        visRow++;
    }

    // Scroll indicator
    int totalVis = 0;
    for (int i = 0; i < (int)SynthParamID::NUM_PARAMS; i++) {
        if (isSynthParamVisible((SynthParamID)i)) totalVis++;
    }
    if (totalVis > ROWS_VISIBLE) {
        int barH = DISP_H * ROWS_VISIBLE / totalVis;
        int barY = HEADER_H + (_synthScroll * (DISP_H - HEADER_H) / totalVis);
        _u8g2.drawBox(DISP_W - 2, barY, 2, barH);
    }
}

// ─── Pattern select ───────────────────────────────────────────────────────────

void UI::drawPatternSel() {
    _u8g2.setFont(u8g2_font_5x7_tr);
    _u8g2.drawStr(0, 20, "SELECT PATTERN");
    _u8g2.drawHLine(0, 22, DISP_W);

    // 2x4 grid of patterns
    for (int i = 0; i < NUM_PATTERNS; i++) {
        int col = i % 4;
        int row = i / 4;
        int x = col * 32;
        int y = 24 + row * 18;

        if (i == _patSel) {
            _u8g2.drawBox(x, y, 30, 16);
            _u8g2.setDrawColor(0);
        } else {
            _u8g2.drawFrame(x, y, 30, 16);
        }

        char label[4];
        snprintf(label, sizeof(label), "P%d", i + 1);
        _u8g2.setFont(u8g2_font_6x10_tr);
        _u8g2.drawStr(x + 7, y + 12, label);
        _u8g2.setDrawColor(1);
    }
    _u8g2.setFont(u8g2_font_4x6_tr);
    _u8g2.drawStr(0, 61, "[ENC]=select [PUSH]=apply");
}

// ─── Pattern options ──────────────────────────────────────────────────────────

void UI::drawPatternOpts() {
    Pattern& pat = _seq->getCurrentPattern();
    _u8g2.setFont(u8g2_font_5x7_tr);
    _u8g2.drawStr(0, 20, "PATTERN OPTIONS");
    _u8g2.drawHLine(0, 22, DISP_W);

    char buf[24];
    auto row = [&](uint8_t idx, const char* text, int y) {
        if (idx == _patOptSel) {
            _u8g2.drawBox(0, y - 8, DISP_W, 10);
            _u8g2.setDrawColor(0);
        }
        _u8g2.drawStr(2, y, text);
        _u8g2.setDrawColor(1);
    };

    snprintf(buf, sizeof(buf), "LEN : %d STEPS", pat.length);
    row(0, buf, 33);
    snprintf(buf, sizeof(buf), "MIDI: CH %d", pat.midiChannel);
    row(1, buf, 43);
    snprintf(buf, sizeof(buf), "SWING: %d%%", pat.swing);
    row(2, buf, 53);

    snprintf(buf, sizeof(buf), "ROOT: %s  SCALE: %s",
             NOTE_NAMES[pat.rootNote], SCALES[pat.scaleIdx].name);
    _u8g2.setFont(u8g2_font_4x6_tr);
    _u8g2.drawStr(0, 62, buf);
}

// ─── Chain edit ──────────────────────────────────────────────────────────────

void UI::drawChainEdit() {
    _u8g2.setFont(u8g2_font_5x7_tr);
    _u8g2.drawStr(0, 20, "CHAIN EDIT");
    _u8g2.drawHLine(0, 22, DISP_W);
    _u8g2.setFont(u8g2_font_4x6_tr);
    _u8g2.drawStr(0, 35, "Chain editing: use SYNTH");
    _u8g2.drawStr(0, 44, "PARAMS screen to set up");
    _u8g2.drawStr(0, 53, "pattern sequence.");
}

// ─── Scale select ─────────────────────────────────────────────────────────────

void UI::drawScaleSel() {
    _u8g2.setFont(u8g2_font_5x7_tr);
    char title[20];
    snprintf(title, sizeof(title), "ROOT:%s  SCALE", NOTE_NAMES[_rootTmp]);
    _u8g2.drawStr(0, 20, title);
    _u8g2.drawHLine(0, 22, DISP_W);

    for (int i = 0; i < (int)NUM_SCALES && i < 5; i++) {
        int visIdx = (int)_scaleTmp - 2 + i;
        if (visIdx < 0 || visIdx >= (int)NUM_SCALES) continue;
        int y = 32 + i * 9;
        if (visIdx == _scaleTmp) {
            if (!_editMode) {
                _u8g2.drawBox(0, y - 7, DISP_W, 9);
                _u8g2.setDrawColor(0);
            }
        }
        _u8g2.drawStr(4, y, SCALES[visIdx].name);
        _u8g2.setDrawColor(1);
    }

    // Root note selector
    if (_editMode) {
        _u8g2.drawBox(0, 55, DISP_W, 9);
        _u8g2.setDrawColor(0);
        _u8g2.drawStr(4, 62, NOTE_NAMES[_rootTmp]);
        _u8g2.setDrawColor(1);
    }
    _u8g2.setFont(u8g2_font_4x6_tr);
    _u8g2.drawStr(80, 62, _editMode ? "[ROOT]" : "[SCALE]");
}

// ─── BPM edit ────────────────────────────────────────────────────────────────

void UI::drawBPMEdit() {
    // Inverted top label bar
    _u8g2.drawBox(0, 0, DISP_W, 11);
    _u8g2.setDrawColor(0);
    _u8g2.setFont(u8g2_font_5x7_tr);
    _u8g2.drawStr(50, 8, "BPM");
    _u8g2.setDrawColor(1);

    // Big BPM number
    _u8g2.setFont(u8g2_font_9x18B_tr);
    char bpmBuf[8];
    snprintf(bpmBuf, sizeof(bpmBuf), "%d", _bpmTmp);
    int tw = _u8g2.getStrWidth(bpmBuf);
    _u8g2.drawStr((DISP_W - tw) / 2, 40, bpmBuf);

    // Animated metronome dot — sweeps across a thin bar
    _u8g2.drawHLine(0, 46, DISP_W);
    unsigned long now     = millis();
    unsigned long beatMs  = 60000UL / (unsigned long)_bpmTmp;
    uint8_t dotX = (uint8_t)((now % beatMs) * DISP_W / beatMs);
    _u8g2.drawBox(dotX, 47, 4, 4);

    _u8g2.setFont(u8g2_font_4x6_tr);
    _u8g2.drawStr(14, 60, "ENC:change   PUSH:back");
}

// ─── MIDI settings ────────────────────────────────────────────────────────────

void UI::drawMIDISettings() {
    _u8g2.setFont(u8g2_font_5x7_tr);
    _u8g2.drawStr(0, 20, "MIDI SETTINGS");
    _u8g2.drawHLine(0, 22, DISP_W);

    char buf[24];
    snprintf(buf, sizeof(buf), "CHANNEL: %d", _midiChTmp);
    _u8g2.drawStr(0, 35, buf);
    _u8g2.drawStr(0, 46, _midiClkTmp ? "CLOCK OUT: ON" : "CLOCK OUT: OFF");
    _u8g2.setFont(u8g2_font_4x6_tr);
    _u8g2.drawStr(0, 60, "[ENC]=ch  [PUSH]=clk  [CFM]=ok");
}

// ─── Main menu overlay ────────────────────────────────────────────────────────

void UI::drawMainMenu() {
    static const char* MENU_LABELS[(int)MenuItem::NUM_ITEMS] = {
        "STEP EDIT", "BPM", "PATTERN", "PAT OPTS", "SCALE",
        "CHAIN", "SYNTH MODE", "SYNTH PARAMS", "MIDI", "SETTINGS"
    };

    // Solid black overlay panel with white inner border
    _u8g2.drawBox(6, HEADER_H + 1, DISP_W - 12, DISP_H - HEADER_H - 4);
    _u8g2.setDrawColor(0);
    _u8g2.drawFrame(7, HEADER_H + 2, DISP_W - 14, DISP_H - HEADER_H - 6);

    int startItem = max(0, (int)_menuSel - 2);
    int y = HEADER_H + 12;
    for (int i = startItem; i < (int)MenuItem::NUM_ITEMS && i < startItem + 5; i++) {
        bool sel = (i == (int)_menuSel);
        if (sel) {
            // Selected row: white bar + black text
            _u8g2.setDrawColor(1);
            _u8g2.drawBox(8, y - 7, DISP_W - 16, 9);
            _u8g2.setDrawColor(0);
        }
        _u8g2.setFont(u8g2_font_5x7_tr);
        _u8g2.drawStr(12, y, MENU_LABELS[i]);
        _u8g2.setDrawColor(1);
        y += 9;
    }

    // Scroll hint arrows
    _u8g2.setFont(u8g2_font_4x6_tr);
    _u8g2.setDrawColor(0);
    if (startItem > 0)                                 _u8g2.drawStr(DISP_W - 14, HEADER_H + 8,  "^");
    if (startItem + 5 < (int)MenuItem::NUM_ITEMS)      _u8g2.drawStr(DISP_W - 14, DISP_H - 5,   "v");
    _u8g2.setDrawColor(1);
}

// ─── Settings ────────────────────────────────────────────────────────────────

void UI::drawSettings() {
    _u8g2.setFont(u8g2_font_5x7_tr);
    _u8g2.drawStr(0, 20, "SETTINGS");
    _u8g2.drawHLine(0, 22, DISP_W);

    char buf[24];
    snprintf(buf, sizeof(buf), "MASTER VOL: %.0f%%",
             _engine->getParams().masterVol * 100);
    _u8g2.drawStr(0, 34, buf);
    _u8g2.drawStr(0, 44, "[BCK LONG]=SAVE");
    _u8g2.drawStr(0, 54, "[CFM LONG]=LOAD");
}
