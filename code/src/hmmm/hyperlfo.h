/** @file hyperlfo.h
 *  @brief The Hyper LFO: two square LFOs, A and B, combined.
 *
 *    OR   (A + B) / 2: three levels, a stepped "gradient" wave
 *    AND  A * B: a square that flips whenever either one does
 *    LINK A gently frequency-modulates B, so the pattern never quite repeats
 *
 *  It reaches into audio rates, where it stops being a wobble and starts
 *  being a tone. A triangle version of each (summed) drives the delay's TRI
 *  mode. Pure C++, no allocation.
 */
#pragma once
#include "dsp.h"

namespace hmmm
{

class HyperLfo
{
  public:
    void Init(float sample_rate)
    {
        sr_  = sample_rate;
        pa_  = 0.f;
        pb_  = 0.25f;
        out_ = 0.f;
        tri_ = 0.f;
        sq_  = 0.f;
        // About 2 ms: rounds the corners off the steps, as the analogue
        // circuit does, without turning them into slopes.
        smooth_ = 1.f - expf(-1.f / (0.002f * sr_));
    }

    void SetFreqs(float hz_a, float hz_b)
    {
        inc_a_ = hz_a / sr_;
        inc_b_ = hz_b / sr_;
    }
    void SetAnd(bool on) { and_ = on; }
    void SetLink(bool on) { link_ = on; }

    inline void Process()
    {
        pa_ += inc_a_;
        if(pa_ >= 1.f)
            pa_ -= 1.f;
        const float tri_a = 4.f * fabsf(pa_ - 0.5f) - 1.f;
        // LINK: A sweeps B's speed over about an octave either way.
        pb_ += link_ ? inc_b_ * FastExp2(tri_a) : inc_b_;
        if(pb_ >= 1.f)
            pb_ -= floorf(pb_);
        const float tri_b = 4.f * fabsf(pb_ - 0.5f) - 1.f;

        const float a = pa_ < 0.5f ? 1.f : -1.f;
        const float b = pb_ < 0.5f ? 1.f : -1.f;
        const float target = and_ ? a * b : 0.5f * (a + b);
        out_ += (target - out_) * smooth_;
        sq_ = out_;
        tri_ = 0.5f * (tri_a + tri_b);
    }

    /** The LFO as the voices hear it: -1..1, stepped. */
    float Out() const { return out_; }
    /** Delay modulation shapes. */
    float Tri() const { return tri_; }
    float Square() const { return sq_; }
    /** For the LED: is A (the main beat) high? */
    bool  APhase() const { return pa_ < 0.5f; }

  private:
    float sr_ = 48000.f;
    float pa_ = 0.f, pb_ = 0.f;
    float inc_a_ = 0.f, inc_b_ = 0.f;
    float out_ = 0.f, tri_ = 0.f, sq_ = 0.f;
    float smooth_ = 1.f;
    bool  and_ = false, link_ = false;
};

} // namespace hmmm
