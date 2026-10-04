/** @file moddelay.h
 *  @brief The Lyra-8's MOD DELAY: two delay lines that feed each other.
 *
 *  Line 1 plays on the left and line 2 on the right. Each one's output is
 *  fed back into the other, so with FEEDBACK up the sound bounces between
 *  them and builds a resonance of its own. Just past the middle of the knob
 *  the loop has more gain than it loses and starts to sing by itself: the
 *  delay becomes a second instrument. A tanh in the loop keeps that bounded.
 *
 *  Each line's time can be modulated (MOD 1 / MOD 2) either by the Hyper LFO
 *  (a triangle or its square) or by the line's own output (SELF), which at
 *  audio rate turns into a gritty FM of the echoes.
 *
 *  Memory is supplied by the caller (SDRAM on the hardware), int16 to halve
 *  it, stored at half scale with POLY's soft knee so loud loops bend
 *  instead of clipping.
 */
#pragma once
#include "dsp.h"

namespace hmmm
{

class ModDelay
{
  public:
    /** @param mem  2 * frames samples: line 1 then line 2 */
    void Init(int16_t* mem, size_t frames, float sample_rate)
    {
        line_[0] = mem;
        line_[1] = mem + frames;
        size_    = frames;
        sr_      = sample_rate;
        write_   = 0;
        for(int k = 0; k < 2; k++)
        {
            time_[k] = target_[k] = 0.3f * sr_;
            out_[k] = lp_[k] = dc_[k] = 0.f;
        }
        for(size_t i = 0; i < 2 * frames; i++)
            mem[i] = 0;
        peak_ = 0.f;
    }

    void SetTimes(float s1, float s2)
    {
        target_[0] = Clamp(s1 * sr_, 32.f, size_ - 4.f);
        target_[1] = Clamp(s2 * sr_, 32.f, size_ - 4.f);
    }
    /** 0..1 knob. Unity loop gain a little past the middle (0.55). */
    void SetFeedback(float knob) { fb_ = knob * 1.8f; }
    /** 0..1 knobs, how far each line's time swings. */
    void SetMod(float m1, float m2)
    {
        mod_[0] = m1 * m1;
        mod_[1] = m2 * m2;
    }
    void SetSelf(bool self) { self_ = self; }
    void SetSquare(bool sq) { square_ = sq; }
    void SetMix(float mix) { MixGains(mix, &dry_, &wet_); }

    float TakePeak()
    {
        const float p = peak_;
        peak_         = 0.f;
        return p;
    }

    inline void Process(float* l, float* r, float lfo_tri, float lfo_sq)
    {
        const float lfo = square_ ? lfo_sq : lfo_tri;
        float       o[2];
        for(int k = 0; k < 2; k++)
        {
            // The base time is slewed (~80 ms) so turning TIME bends the
            // pitch of the echoes, tape style. The modulation is not.
            time_[k] += (target_[k] - time_[k]) * 0.0003f;
            const float m     = self_ ? out_[k] : lfo;
            // Swing up to 90 % of the time, or 250 ms, whichever is less.
            const float swing = mod_[k] * fminf(0.9f * time_[k], 0.25f * sr_);
            o[k] = Read(line_[k], Clamp(time_[k] + swing * m, 2.f, size_ - 4.f));
        }

        // Cross-feedback: each line hears the other. A gentle low-pass and a
        // DC blocker in the loop keep a self-oscillating loop musical.
        const float in[2] = {*l, *r};
        for(int k = 0; k < 2; k++)
        {
            float w = in[k] + fb_ * o[1 - k];
            lp_[k] += (w - lp_[k]) * 0.6f;
            dc_[k] += (lp_[k] - dc_[k]) * 0.0005f;
            // Saturates at half scale: a singing loop sits level with the
            // voices instead of a full-scale square.
            w = 0.5f * FastTanh(2.f * (lp_[k] - dc_[k]));
            peak_ = fmaxf(peak_, fabsf(w));
            line_[k][write_] = ToS16(SoftLimit(w * 0.5f));
            out_[k]          = o[k];
        }
        write_ = write_ + 1 < size_ ? write_ + 1 : 0;

        *l = *l * dry_ + o[0] * wet_;
        *r = *r * dry_ + o[1] * wet_;
    }

  private:
    static inline int16_t ToS16(float x)
    {
        x = Clamp(x, -1.f, 1.f);
        return static_cast<int16_t>(x * 32767.f);
    }

    inline float Read(const int16_t* line, float delay) const
    {
        float pos = static_cast<float>(write_) - delay;
        if(pos < 0.f)
            pos += static_cast<float>(size_);
        const size_t i0 = static_cast<size_t>(pos);
        const size_t i1 = i0 + 1 < size_ ? i0 + 1 : 0;
        const float  f  = pos - static_cast<float>(i0);
        return (line[i0] + (line[i1] - line[i0]) * f) * (2.f / 32768.f);
    }

    int16_t* line_[2] = {nullptr, nullptr};
    size_t   size_    = 0;
    size_t   write_   = 0;
    float    sr_      = 48000.f;
    float    time_[2] = {}, target_[2] = {};
    float    out_[2] = {}, lp_[2] = {}, dc_[2] = {};
    float    mod_[2] = {};
    float    fb_     = 0.f;
    float    dry_ = 1.f, wet_ = 0.f;
    bool     self_ = false, square_ = false;
    float    peak_ = 0.f;
};

} // namespace hmmm
