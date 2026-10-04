/** @file voice.h
 *  @brief One HMMM voice: a triangle oscillator bent towards a square by
 *  SHARP, an envelope that swells in when its key is held and fades slowly
 *  when let go, and a HOLD floor under it that keeps it droning.
 *
 *  The oscillator runs at twice the sample rate (the engine decimates the
 *  mix), so SHARP's hard edges stay clean. FM is linear, in Hz, as on the
 *  Lyra-8: a fixed amount of modulation bends low voices much further than
 *  high ones, and the frequency may go through zero (the phase runs
 *  backwards), which is what lets an FM loop howl without blowing up.
 */
#pragma once
#include "dsp.h"

namespace hmmm
{

class Voice
{
  public:
    void Init(float sample_rate, uint32_t seed)
    {
        sr_       = sample_rate;
        phase_    = (seed % 1000) / 1000.f; // voices start out of phase
        out_      = 0.f;
        env_      = 0.f;
        level_    = 0.f;
        drift_    = drift_target_ = 0.f;
        drift_timer_ = 0.f;
        vib_phase_ = (seed % 777) / 777.f;
        // Each voice's vibrato runs at its own speed, 4.5 - 6.5 Hz, so eight
        // of them never line up: the Lyra's shimmer.
        vib_hz_ = 4.5f + (seed % 97) / 97.f * 2.f;
        noise_.s = 0x9E3779B9u ^ (seed * 2654435761u);
        gate_    = false;
    }

    void SetGate(bool on) { gate_ = on; }
    bool Gate() const { return gate_; }

    /** Envelope (0..1) with HOLD under it: what the LEDs show. */
    float Level() const { return level_; }
    /** The last output sample: this voice as an FM source. */
    float Out() const { return out_; }

    /** Once per block: the slow stuff. Returns the voice's pitch in
     *  semitones (MIDI note numbers) including vibrato and drift.
     *  @param character 0..1: drift and the envelope's pitch nudge */
    float BlockPitch(float note, float vibrato_st, float character, float dt)
    {
        vib_phase_ += vib_hz_ * dt;
        if(vib_phase_ >= 1.f)
            vib_phase_ -= 1.f;
        const float vib = sinf(2.f * kPi * vib_phase_) * vibrato_st;

        // Drift: wanders to a new random offset every second or two.
        drift_timer_ -= dt;
        if(drift_timer_ <= 0.f)
        {
            drift_target_ = noise_.Process();
            drift_timer_  = 0.8f + 1.5f * (noise_.Process() * 0.5f + 0.5f);
        }
        drift_ += (drift_target_ - drift_) * dt * 0.7f;

        // Up to +/- 8 cents of drift, and up to 6 cents sharp at full
        // envelope: the pitch leans as the voice swells, like the Lyra's.
        return note + vib + character * (0.08f * drift_ + 0.06f * env_);
    }

    /** Once per sample (base rate): the envelope. */
    inline void TickEnv(float attack_coef, float release_coef, float hold)
    {
        if(gate_)
            env_ += (1.f - env_) * attack_coef;
        else
            env_ += (0.f - env_) * release_coef;
        // The louder of the two, with a soft corner so HOLD and the
        // envelope blend instead of switching.
        const float d = env_ - hold;
        level_        = 0.5f * (env_ + hold + sqrtf(d * d + 0.0004f)) - 0.01f;
        if(level_ < 0.f)
            level_ = 0.f;
    }

    /** One oscillator sample at twice the sample rate.
     *  @param inc   phase increment (frequency / oversampled rate), may be < 0
     *  @param g     SHARP drive: 1 = triangle .. ~30 = square
     *  @param norm  1 / tanh(g), so every shape peaks at 1 */
    inline float Tick(float inc, float g, float norm)
    {
        phase_ += inc;
        phase_ -= floorf(phase_);
        const float tri = 4.f * fabsf(phase_ - 0.5f) - 1.f;
        out_            = FastTanh(g * tri) * norm * level_;
        return out_;
    }

  private:
    float    sr_ = 48000.f;
    float    phase_ = 0.f;
    float    out_   = 0.f;
    float    env_   = 0.f;
    float    level_ = 0.f;
    float    drift_ = 0.f, drift_target_ = 0.f, drift_timer_ = 0.f;
    float    vib_phase_ = 0.f, vib_hz_ = 5.f;
    Noise    noise_;
    bool     gate_ = false;
};

} // namespace hmmm
