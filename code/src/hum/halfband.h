/** @file halfband.h
 *  @brief Halves the sample rate: the voices run at 96 kHz and this brings
 *  their mix down to 48 kHz, filtering out everything above ~20 kHz first
 *  so SHARP's square edges don't fold back as aliasing.
 *
 *  A 31-tap windowed-sinc half-band FIR. Every other tap is zero, so it
 *  costs 16 multiplies per output sample.
 */
#pragma once
#include "dsp.h"

namespace hum
{

class Halfband
{
  public:
    static constexpr int kTaps   = 31;
    static constexpr int kCentre = kTaps / 2;

    void Init()
    {
        float sum = 0.f;
        for(int n = 0; n < kTaps; n++)
        {
            const float x = (n - kCentre) * 0.5f; // cutoff: a quarter of the high rate
            const float s = x == 0.f ? 1.f : sinf(kPi * x) / (kPi * x);
            // Blackman window: ~75 dB down in the stopband.
            const float w = 0.42f - 0.5f * cosf(2.f * kPi * n / (kTaps - 1))
                            + 0.08f * cosf(4.f * kPi * n / (kTaps - 1));
            h_[n] = s * w;
            sum += h_[n];
        }
        for(int n = 0; n < kTaps; n++)
            h_[n] /= sum;
        for(int i = 0; i < 2 * kTaps; i++)
            buf_[i] = 0.f;
        pos_ = 0;
    }

    /** Takes two samples at the high rate, returns one at the low rate. */
    inline float Process(float x0, float x1)
    {
        Push(x0);
        Push(x1);
        // buf_ holds the history twice over, so the newest kTaps samples
        // are always contiguous at buf_[pos_ .. pos_ + kTaps).
        const float* x   = &buf_[pos_];
        float        acc = h_[kCentre] * x[kCentre];
        for(int n = 0; n < kCentre; n += 2) // odd offsets from the centre
            acc += h_[n] * (x[n] + x[kTaps - 1 - n]);
        return acc;
    }

  private:
    inline void Push(float v)
    {
        pos_ = pos_ > 0 ? pos_ - 1 : kTaps - 1;
        buf_[pos_]         = v;
        buf_[pos_ + kTaps] = v;
    }

    float h_[kTaps];
    float buf_[2 * kTaps];
    int   pos_ = 0;
};

} // namespace hum
