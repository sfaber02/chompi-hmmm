/** @file params.h
 *  @brief Every sound parameter, its default, its knob page, and its MIDI CC.
 *
 *  All values are stored normalised 0..1 in one float array, which is what the
 *  knobs move, what presets save, and what MIDI CCs set. The engine turns them
 *  into real units once per audio block. Stepped parameters (FAST, MOD source,
 *  ...) use `steps` positions spread evenly over 0..1.
 */
#pragma once
#include <cstdint>

namespace hmmm
{

constexpr int kNumVoices = 8;
constexpr int kNumPairs  = 4;

enum Param : uint8_t
{
    // Per voice. Keep each block contiguous: TUNE1 + v addresses voice v.
    TUNE1,
    TUNE2,
    TUNE3,
    TUNE4,
    TUNE5,
    TUNE6,
    TUNE7,
    TUNE8,

    // Per pair (12, 34, 56, 78): SHARP1 + pair.
    SHARP1,
    SHARP2,
    SHARP3,
    SHARP4,
    FAST1,
    FAST2,
    FAST3,
    FAST4,
    MOD1,
    MOD2,
    MOD3,
    MOD4,
    SRC1,
    SRC2,
    SRC3,
    SRC4,

    // Per group (1234, 5678)
    HOLD_A,
    HOLD_B,
    PITCH_A,
    PITCH_B,
    VIBRATO,
    TOTAL_FB,

    LFO_A,
    LFO_B,
    LFO_MODE,
    LFO_LINK,

    DLY_MIX,
    DLY_TIME1,
    DLY_TIME2,
    DLY_FB,
    DLY_MOD1,
    DLY_MOD2,
    DLY_SRC,
    DLY_SHAPE,

    DIST_DRIVE,
    DIST_MIX,

    ATTACK,
    RELEASE,
    CHARACTER,
    SPREAD,

    // Global: the instrument, not the sound. Never saved in a patch or
    // changed by loading one; remembered at power-on (in current.txt).
    OCTAVE,
    TRANSPOSE,
    FINE_TUNE,
    BEND_RANGE,

    NUM_PARAMS
};

/** A voice's TUNE knob covers +/- 12 semitones around its home note. */
constexpr float kTuneRange = 24.f;
/** Home notes: voices 1-2 low, 3-6 in the middle, 7-8 an octave up, as on
 *  the Lyra-8. */
constexpr float kHomeNote[kNumVoices] = {36, 36, 48, 48, 48, 48, 60, 60};

/** Normalised TUNE value for a number of semitones from the home note. */
constexpr float TuneSt(float st) { return 0.5f + st / kTuneRange; }

/** MOD source positions. */
enum ModSrc : uint8_t
{
    SRC_OFF,
    SRC_FM,  // the pair before it in the FM ring (see engine.h)
    SRC_LFO, // the Hyper LFO, or the whole output with TOTAL FB on
};

struct ParamInfo
{
    const char* name;    // key in preset files, keep stable
    float       def;     // normalised default
    uint8_t     steps;   // 0 = continuous, else number of positions
    bool        bipolar; // centre = zero; drives the key-LED bar display
    uint8_t     cc;      // MIDI CC in, 0 = none
    bool        global = false; // instrument setting: not part of a patch
};

// clang-format off
constexpr ParamInfo kParams[NUM_PARAMS] = {
    // Default tuning: a C minor 7 drone spread over three octaves.
    {"tune1",      TuneSt(0),  0, true,  20},  // C2
    {"tune2",      TuneSt(7),  0, true,  21},  // G2
    {"tune3",      TuneSt(0),  0, true,  22},  // C3
    {"tune4",      TuneSt(7),  0, true,  23},  // G3
    {"tune5",      TuneSt(3),  0, true,  24},  // Eb3
    {"tune6",      TuneSt(10), 0, true,  25},  // Bb3
    {"tune7",      TuneSt(0),  0, true,  26},  // C4
    {"tune8",      TuneSt(7),  0, true,  27},  // G4

    {"sharp12",    .15f,  0, false, 28},
    {"sharp34",    .15f,  0, false, 29},
    {"sharp56",    .15f,  0, false, 30},
    {"sharp78",    .15f,  0, false, 31},
    {"fast12",     0.f,   2, false, 0},
    {"fast34",     0.f,   2, false, 0},
    {"fast56",     0.f,   2, false, 0},
    {"fast78",     0.f,   2, false, 0},
    {"mod12",      0.f,   0, false, 102},
    {"mod34",      0.f,   0, false, 103},
    {"mod56",      0.f,   0, false, 104},
    {"mod78",      0.f,   0, false, 105},
    {"src12",      .5f,   3, false, 0},  // FM
    {"src34",      .5f,   3, false, 0},
    {"src56",      .5f,   3, false, 0},
    {"src78",      .5f,   3, false, 0},

    {"hold_a",     0.f,   0, false, 106},
    {"hold_b",     0.f,   0, false, 107},
    {"pitch_a",    .5f,   0, true,  108},
    {"pitch_b",    .5f,   0, true,  109},
    {"vibrato",    0.f,   0, false, 1},
    {"total_fb",   0.f,   2, false, 0},

    {"lfo_a",      .3f,   0, false, 110},
    {"lfo_b",      .36f,  0, false, 111},
    {"lfo_mode",   0.f,   2, false, 0},  // OR / AND
    {"lfo_link",   0.f,   2, false, 0},

    {"dly_mix",    .3f,   0, false, 91},
    {"dly_time1",  .75f,  0, false, 85},
    {"dly_time2",  .8f,   0, false, 86},
    {"dly_fb",     .3f,   0, false, 87},
    {"dly_mod1",   0.f,   0, false, 112},
    {"dly_mod2",   0.f,   0, false, 113},
    {"dly_src",    1.f,   2, false, 0},  // SELF / LFO
    {"dly_shape",  0.f,   2, false, 0},  // TRI / SQUARE

    {"dist_drive", 0.f,   0, false, 114},
    {"dist_mix",   1.f,   0, false, 115},

    {"attack",     .55f,  0, false, 73},
    {"release",    .75f,  0, false, 72},
    {"character",  .4f,   0, false, 116},
    {"spread",     .5f,   0, false, 10},

    {"octave",     .5f,   5, true,  0, true},     // -2..+2
    {"transpose",  .5f,  25, true,  0, true},     // -12..+12 semitones
    {"fine_tune",  .5f, 101, true,  117, true},   // +/-50 cents in exact 1-cent steps
    {"bend_range", 1/11.f, 12, false, 0, true},   // 1..12 semitones, default 2
};
// clang-format on

/** Index of a stepped parameter's position. */
constexpr int StepIndex(float v, int steps)
{
    int i = static_cast<int>(v * (steps - 1) + 0.5f);
    return i < 0 ? 0 : (i >= steps ? steps - 1 : i);
}

constexpr float StepValue(int idx, int steps)
{
    return steps > 1 ? static_cast<float>(idx) / (steps - 1) : 0.f;
}

inline bool IsVoiceTune(int id) { return id >= TUNE1 && id <= TUNE8; }

// ---------------------------------------------------------------------------
// Knob pages. CHOMPI + a black key picks one. Knobs 1-4 edit its four main
// parameters; CHOMPI + turn edits the second layer. On the two voice TUNE
// pages the second layer is the same knob in whole semitones.

enum Page : uint8_t
{
    PAGE_TUNE_A, // voices 1-4
    PAGE_TUNE_B, // voices 5-8
    PAGE_SHARP,
    PAGE_MOD,
    PAGE_GROUPS,
    PAGE_LFO,
    PAGE_DELAY,
    PAGE_DIST,
    PAGE_VOICE,
    PAGE_TUNE,
    NUM_PAGES
};

constexpr int     kPageKnobs = 4;
constexpr uint8_t kNone      = 0xFF;

// [page][0..3] = knobs 1-4, [page][4..7] = CHOMPI + knobs 1-4.
constexpr uint8_t kPageParams[NUM_PAGES][2 * kPageKnobs] = {
    {TUNE1, TUNE2, TUNE3, TUNE4, TUNE1, TUNE2, TUNE3, TUNE4},
    {TUNE5, TUNE6, TUNE7, TUNE8, TUNE5, TUNE6, TUNE7, TUNE8},
    {SHARP1, SHARP2, SHARP3, SHARP4, FAST1, FAST2, FAST3, FAST4},
    {MOD1, MOD2, MOD3, MOD4, SRC1, SRC2, SRC3, SRC4},
    {HOLD_A, HOLD_B, PITCH_A, PITCH_B, VIBRATO, kNone, kNone, kNone},
    {LFO_A, LFO_B, LFO_MODE, LFO_LINK, kNone, kNone, kNone, kNone},
    {DLY_MIX, DLY_TIME1, DLY_TIME2, DLY_FB, DLY_MOD1, DLY_MOD2, DLY_SRC, DLY_SHAPE},
    {DIST_DRIVE, DIST_MIX, kNone, kNone, kNone, kNone, kNone, kNone},
    {ATTACK, RELEASE, CHARACTER, SPREAD, kNone, kNone, kNone, kNone},
    {OCTAVE, TRANSPOSE, FINE_TUNE, BEND_RANGE, kNone, kNone, kNone, kNone},
};

// Page colours (RGB 0..1): the knob LEDs and the page's black key show these.
// Voices 1-4 are warm and 5-8 cool, everywhere they appear.
constexpr float kPageColour[NUM_PAGES][3] = {
    {1.f, .4f, 0.f},   // TUNE 1-4   orange
    {0.f, .7f, 1.f},   // TUNE 5-8   sky blue
    {1.f, 0.f, .25f},  // SHARP      hot pink
    {.6f, 0.f, 1.f},   // MOD        purple
    {0.f, 1.f, .3f},   // GROUPS     green
    {1.f, .85f, 0.f},  // HYPER LFO  yellow
    {0.f, 1.f, .85f},  // DELAY      teal
    {1.f, .06f, 0.f},  // DISTORTION red
    {.2f, .3f, 1.f},   // VOICE      indigo
    {1.f, 1.f, 1.f},   // TUNE       white
};

constexpr float kGroupColour[2][3] = {
    {1.f, .4f, 0.f}, // 1-4
    {0.f, .7f, 1.f}, // 5-8
};

} // namespace hmmm
