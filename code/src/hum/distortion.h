/** @file distortion.h
 *  @brief The last stage before the volume, as on the Lyra-8: a fuzz that
 *  goes from warm to broken. Asymmetric (a little bias before the tanh) so it
 *  adds even harmonics too, then blended with the clean signal by MIX.
 */
#pragma once
#include "dsp.h"

namespace hum
{

class Distortion
{
  public:
    /** drive, mix: 0..1 knobs */
    void Set(float drive, float mix)
    {
        gain_   = 1.f + 60.f * drive * drive;
        bias_   = 0.3f * drive;
        offset_ = FastTanh(bias_);
        // Driven hard, the fuzz is a full-scale square: bring it back down
        // to sit near the clean level.
        makeup_ = 1.f - 0.55f * drive;
        mix_    = mix;
    }

    inline float Process(float x) const
    {
        const float y = (FastTanh(x * gain_ + bias_) - offset_) * makeup_;
        return x + (y - x) * mix_;
    }

  private:
    float gain_ = 1.f, bias_ = 0.f, offset_ = 0.f, makeup_ = 1.f, mix_ = 1.f;
};

} // namespace hum
