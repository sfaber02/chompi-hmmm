/** @file factory.h
 *  @brief The factory patches, built into the firmware.
 *
 *  A factory slot whose file (/HMMM/Pnn.txt) is missing loads its built-in
 *  copy, so a bare .bin with no HMMM folder still has every patch, and slot
 *  15 (init) always works. A patch you save to a slot writes the file, which
 *  then wins.
 *
 *  Each patch is the parameter defaults plus these overrides. Only the HOLD
 *  patch drones by itself; the rest wait for you to touch a key.
 */
#pragma once
#include "params.h"

namespace hmmm
{

struct ParamValue
{
    uint8_t id;
    float   v;
};

struct FactoryPatch
{
    const char*       name;
    uint8_t           slot; // 1-15
    const ParamValue* values;
    uint8_t           count;
};

// The default C minor 7 voicing, with a few cents between neighbours so
// the pairs beat slowly against each other.
constexpr ParamValue kOrgan[] = {
    {TUNE2, TuneSt(7.04f)}, {TUNE4, TuneSt(6.97f)}, {TUNE6, TuneSt(10.05f)}, {TUNE8, TuneSt(6.96f)},
    {SHARP1, .3f}, {SHARP2, .25f}, {SHARP3, .1f}, {SHARP4, .1f},
    {VIBRATO, .3f}, {CHARACTER, .5f}, {DLY_MIX, .3f}, {DLY_FB, .35f},
};

// All four pairs in one FM ring: each touch drags the others with it.
constexpr ParamValue kFmLoop[] = {
    {TUNE1, TuneSt(0)}, {TUNE2, TuneSt(.06f)}, {TUNE3, TuneSt(7)}, {TUNE4, TuneSt(-5)},
    {TUNE5, TuneSt(0)}, {TUNE6, TuneSt(7.05f)}, {TUNE7, TuneSt(-5)}, {TUNE8, TuneSt(0.04f)},
    {SHARP1, .4f}, {SHARP2, .35f}, {SHARP3, .3f}, {SHARP4, .2f},
    {MOD1, .35f}, {MOD2, .25f}, {MOD3, .22f}, {MOD4, .3f},
    {DLY_MIX, .25f}, {DLY_FB, .4f}, {CHARACTER, .5f},
};

// Pairs 12 and 56 stepped about by the Hyper LFO (AND), and the echoes
// jump with its square.
constexpr ParamValue kLfoPulse[] = {
    {SRC1, StepValue(SRC_LFO, 3)}, {SRC3, StepValue(SRC_LFO, 3)},
    {MOD1, .3f}, {MOD3, .22f},
    {LFO_A, .45f}, {LFO_B, .5f}, {LFO_MODE, 1.f},
    {SHARP1, .5f}, {SHARP3, .5f},
    {DLY_SRC, 1.f}, {DLY_SHAPE, 1.f}, {DLY_MOD1, .3f}, {DLY_MOD2, .3f},
    {DLY_MIX, .4f}, {DLY_FB, .4f},
};

// Delay feedback past the middle: the echoes keep themselves going and
// slowly bend with the LFO's triangle. Touch a key and let go.
constexpr ParamValue kDelayDrone[] = {
    {SHARP1, .05f}, {SHARP2, .05f}, {SHARP3, .05f}, {SHARP4, .05f},
    {DLY_FB, .6f}, {DLY_MIX, .55f}, {DLY_TIME1, .85f}, {DLY_TIME2, .9f},
    {DLY_MOD1, .22f}, {DLY_MOD2, .18f}, {LFO_A, .12f}, {LFO_B, .17f},
    {RELEASE, .85f}, {CHARACTER, .6f},
};

// TOTAL FB on: every pair is modulated by the whole output, distortion
// and all. It feeds on itself.
constexpr ParamValue kChaos[] = {
    {TOTAL_FB, 1.f},
    {SRC1, StepValue(SRC_LFO, 3)}, {SRC2, StepValue(SRC_LFO, 3)},
    {SRC3, StepValue(SRC_LFO, 3)}, {SRC4, StepValue(SRC_LFO, 3)},
    {MOD1, .4f}, {MOD2, .3f}, {MOD3, .25f}, {MOD4, .35f},
    {SHARP1, .6f}, {SHARP2, .5f}, {SHARP3, .5f}, {SHARP4, .4f},
    {DIST_DRIVE, .5f}, {DIST_MIX, .7f},
    {DLY_FB, .45f}, {DLY_SRC, 0.f}, {DLY_MOD1, .2f}, {DLY_MIX, .35f},
};

// HOLD up on voices 1-4: a low drone that plays as soon as it loads.
// Play 5-8 over it.
constexpr ParamValue kHoldDrone[] = {
    {TUNE1, TuneSt(0)}, {TUNE2, TuneSt(0.05f)}, {TUNE3, TuneSt(-5)}, {TUNE4, TuneSt(-4.96f)},
    {HOLD_A, .55f}, {SHARP1, .2f}, {SHARP2, .35f},
    {MOD1, .15f}, {VIBRATO, .25f}, {DLY_MIX, .35f}, {DLY_FB, .45f},
};

constexpr ParamValue kInit[] = {{TUNE1, TuneSt(0)}};

#define HMMM_FACTORY(name, slot, arr) \
    {name, slot, arr, static_cast<uint8_t>(sizeof(arr) / sizeof(arr[0]))}
constexpr FactoryPatch kFactoryPatches[] = {
    HMMM_FACTORY("organ", 1, kOrgan),
    HMMM_FACTORY("fm_loop", 2, kFmLoop),
    HMMM_FACTORY("lfo_pulse", 3, kLfoPulse),
    HMMM_FACTORY("delay_drone", 4, kDelayDrone),
    HMMM_FACTORY("chaos", 5, kChaos),
    HMMM_FACTORY("hold_drone", 6, kHoldDrone),
    HMMM_FACTORY("init", 15, kInit),
};
#undef HMMM_FACTORY
constexpr int kNumFactoryPatches = sizeof(kFactoryPatches) / sizeof(kFactoryPatches[0]);

/** The sound a brand-new card starts on. Silent until a key is touched. */
constexpr int kFirstBootSlot = 1;

/** Built-in patch for a slot (1-15), or nullptr. */
inline const FactoryPatch* FactoryForSlot(int slot)
{
    for(int i = 0; i < kNumFactoryPatches; i++)
        if(kFactoryPatches[i].slot == slot)
            return &kFactoryPatches[i];
    return nullptr;
}

/** Writes the whole patch (defaults + overrides) into params. Global
 *  (instrument) settings are left as they are. */
inline void ApplyFactory(const FactoryPatch& f, float* params)
{
    for(int i = 0; i < NUM_PARAMS; i++)
        if(!kParams[i].global)
            params[i] = kParams[i].def;
    for(int i = 0; i < f.count; i++)
        params[f.values[i].id] = f.values[i].v;
}

} // namespace hmmm
