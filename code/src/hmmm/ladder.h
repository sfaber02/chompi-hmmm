/** @file ladder.h
 *  @brief 4-pole (24 dB/oct) transistor-ladder style low-pass.
 *
 *  Zero-delay-feedback (topology-preserving transform) ladder after Vadim
 *  Zavalishin, "The Art of VA Filter Design", ch. 5: four TPT one-poles with
 *  the global feedback loop solved instantaneously, so the cutoff stays in tune
 *  and resonance reaches self-oscillation at k = 4. A tanh on the loop input
 *  gives the drive and keeps high resonance from blowing up.
 *
 *  The cutoff coefficient is set once per block and ramped per sample, so
 *  envelopes sweep without zipper noise and without a tan() per sample.
 */
#pragma once
#include "dsp.h"

namespace hmmm
{

class Ladder
{
  public:
    void Init(float sample_rate)
    {
        sr_ = sample_rate;
        Reset();
        g_ = g_target_ = CutoffToG(1000.f);
    }

    void Reset()
    {
        s_[0] = s_[1] = s_[2] = s_[3] = 0.f;
    }

    /** Start a new block heading for this cutoff. */
    void SetCutoffBlock(float hz, size_t block_size)
    {
        g_ = g_target_;
        g_target_ = CutoffToG(hz);
        g_inc_    = (g_target_ - g_) / static_cast<float>(block_size);
    }

    /** 0..1. Self-oscillation starts around 0.9 (k = 4). */
    void SetResonance(float r) { k_ = Clamp(r, 0.f, 1.f) * 4.4f; }

    /** Input gain into the loop's saturator, 1 = clean-ish. */
    void SetDrive(float d) { drive_ = d; }

    inline float Process(float x)
    {
        g_ += g_inc_;
        const float g  = g_;
        const float G  = g / (1.f + g);
        const float G2 = G * G;

        // Each stage's output is y = G*x + S, with S = s / (1 + g).
        const float inv = 1.f / (1.f + g);
        const float S1  = s_[0] * inv;
        const float S2  = s_[1] * inv;
        const float S3  = s_[2] * inv;
        const float S4  = s_[3] * inv;
        const float S   = G2 * G * S1 + G2 * S2 + G * S3 + S4;

        // Some of the low end lost at high resonance comes back as input gain
        // (less than POLY: here the filter is the last stage before the
        // limiter, so a resonant peak hits it directly).
        const float in = x * drive_ * (1.f + 0.25f * k_);
        float u        = (in - k_ * S) / (1.f + k_ * G2 * G2);
        u              = FastTanh(u);

        float y = u;
        for(int i = 0; i < 4; i++)
        {
            const float v = (y - s_[i]) * G;
            const float o = v + s_[i];
            s_[i]         = o + v;
            y             = o;
        }

        // Undo the input drive so the drive knob mostly changes colour, not level.
        return y / (0.6f + 0.4f * drive_);
    }

  private:
    float CutoffToG(float hz) const
    {
        hz = Clamp(hz, 10.f, sr_ * 0.45f);
        return tanf(kPi * hz / sr_);
    }

    float sr_       = 48000.f;
    float s_[4]     = {};
    float g_        = 0.f;
    float g_target_ = 0.f;
    float g_inc_    = 0.f;
    float k_        = 0.f;
    float drive_    = 1.f;
};

} // namespace hmmm
