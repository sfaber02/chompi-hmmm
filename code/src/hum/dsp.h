/** @file dsp.h
 *  @brief Small math helpers shared by the synth DSP. Pure C++, no libDaisy, so
 *  the whole engine also builds on the desktop (see host/).
 */
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace hum
{

using std::size_t;

constexpr float kPi = 3.14159265358979f;

inline float Clamp(float x, float lo, float hi)
{
    return x < lo ? lo : (x > hi ? hi : x);
}

/** 2^x, accurate to ~0.01 % over the range we use. Cheap enough to run per sample. */
inline float FastExp2(float x)
{
    x = Clamp(x, -30.f, 30.f);
    const float fl = floorf(x);
    const float f  = x - fl;
    // 4th-order minimax fit of 2^f on [0,1)
    const float p = 1.f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * 0.0096181f)));
    return ldexpf(p, static_cast<int>(fl));
}

/** Clean below 0.75, then bends smoothly into 1.0: headroom for anything
 *  that has to fit a fixed range (the delay's and reverb's 16-bit memory). */
inline float SoftLimit(float x)
{
    const float a = fabsf(x);
    if(a <= 0.75f)
        return x;
    const float over = (a - 0.75f) * 4.f;            // 0.. at the knee
    const float y    = 0.75f + 0.25f * (over / (1.f + over));
    return x < 0.f ? -y : y;
}

/** Dry/wet law shared by the delay and reverb mix knobs: the bottom half
 *  brings the effect up to full under a full dry signal (the middle is a
 *  true 50/50), the top half fades the dry out to 100 % wet. */
inline void MixGains(float m, float* dry, float* wet)
{
    m    = m < 0.f ? 0.f : (m > 1.f ? 1.f : m);
    *wet = m < 0.5f ? 2.f * m : 1.f;
    *dry = m > 0.5f ? 2.f * (1.f - m) : 1.f;
}

/** Pade tanh, clamped where it would turn back. */
inline float FastTanh(float x)
{
    if(x > 3.f)
        return 1.f;
    if(x < -3.f)
        return -1.f;
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

inline float MidiToHz(float note)
{
    return 440.f * FastExp2((note - 69.f) * (1.f / 12.f));
}

/** Knob 0..1 to a time in seconds, exponential between lo and hi. */
inline float KnobToTime(float v, float lo, float hi)
{
    return lo * powf(hi / lo, v);
}

/** Per-sample coefficient for a one-pole that settles in about `seconds`. */
inline float TimeToCoef(float seconds, float sr)
{
    if(seconds <= 0.f)
        return 1.f;
    return 1.f - expf(-1.f / (seconds * sr));
}

/** xorshift32 white noise in [-1, 1). */
struct Noise
{
    uint32_t s = 0x9E3779B9u;
    inline float Process()
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return static_cast<int32_t>(s) * (1.f / 2147483648.f);
    }
};

} // namespace hum
