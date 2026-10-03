/** @file engine.h
 *  @brief The whole instrument, modelled on the SOMA Lyra-8:
 *
 *    8 voices in 4 pairs (12 34 56 78) and 2 groups (1234 5678)
 *      -> mix at 96 kHz -> halve to 48 kHz -> MOD DELAY -> DISTORTION
 *      -> FILTER -> volume -> limiter
 *
 *  FM. Each pair's MOD source is off, FM, or LFO:
 *    FM   the pair is modulated by its neighbour in the FM ring. The
 *         structure switch decides the ring: one big loop
 *         12 -> 34 -> 56 -> 78 -> 12, or two small ones 12 <-> 34 and
 *         56 <-> 78. A voice modulates in proportion to how loud it is, so
 *         FM swells and fades with the envelopes.
 *    LFO  the pair is modulated by the Hyper LFO, or, with TOTAL FB on, by
 *         the instrument's own output after the distortion.
 *
 *  Threading: everything here runs in the audio interrupt. The UI and MIDI
 *  set gates from that same interrupt, and only write the param array from
 *  anywhere else (single float stores, harmless).
 */
#pragma once
#include "dsp.h"
#include "params.h"
#include "voice.h"
#include "hyperlfo.h"
#include "halfband.h"
#include "moddelay.h"
#include "distortion.h"
#include "ladder.h"

namespace hmmm
{

class Engine
{
  public:
    float params[NUM_PARAMS];
    float volume = 0.7f; // master, not saved in presets

    void Init(float sample_rate, int16_t* delay_mem, size_t delay_frames)
    {
        sr_  = sample_rate;
        sr2_ = 2.f * sample_rate;
        for(int i = 0; i < NUM_PARAMS; i++)
            params[i] = kParams[i].def;
        for(int v = 0; v < kNumVoices; v++)
            voices_[v].Init(sample_rate, 1234567u * (v + 1) + 89u);
        lfo_.Init(sample_rate);
        hb_l_.Init();
        hb_r_.Init();
        delay_.Init(delay_mem, delay_frames, sample_rate);
        filt_l_.Init(sample_rate);
        filt_r_.Init(sample_rate);
        // Every field, explicitly: on the hardware this object may sit in
        // RAM that startup doesn't clear (POLY's boot-noise lesson).
        volume     = 0.7f;
        key_gates_ = midi_gates_ = 0;
        one_loop_  = true;
        total_fb_  = false;
        bend_      = 0.f;
        fb_sig_    = 0.f;
        noise_.s   = 0x9E3779B9u;
        lv_voices_ = lv_dist_ = 0.f;
        lv_limit_  = 1.f;
        dc_l_ = dc_r_ = 0.f;
        vol_ = 0.f;
        lim_env_ = meter_ = 0.f;
    }

    // ---------------------------------------------------------------- voices

    /** Key gates for voices 0-7, from the keybed (bit v = voice v held or
     *  latched) and from MIDI. A voice sounds if either has it. */
    void SetKeyGates(uint8_t g) { key_gates_ = g; }
    void SetMidiGate(int v, bool on)
    {
        if(v < 0 || v >= kNumVoices)
            return;
        midi_gates_ = on ? (midi_gates_ | (1u << v)) : (midi_gates_ & ~(1u << v));
    }

    /** The structure switch: true = one big FM loop, false = two. */
    void SetOneLoop(bool one) { one_loop_ = one; }

    /** -1..1; scaled by the bend range on the TUNE page. */
    void SetPitchBend(float amount) { bend_ = amount; }

    /** Releases every MIDI voice (keys are the UI's to release). */
    void AllNotesOff() { midi_gates_ = 0; }

    /** 0..1: envelope (or HOLD) of each voice, for the key LEDs. */
    float VoiceLevel(int v) const { return voices_[v].Level(); }
    bool  VoiceGate(int v) const { return voices_[v].Gate(); }
    bool  LfoBeat() const { return lfo_.APhase(); }

    /** Peak levels at each stage since the last call, for the DIAG log. */
    struct Levels
    {
        float voices, delay_in, dist_out, limit_gain;
    };
    Levels TakeLevels()
    {
        Levels l{lv_voices_, delay_.TakePeak(), lv_dist_, lv_limit_};
        lv_voices_ = lv_dist_ = 0.f;
        lv_limit_             = 1.f;
        return l;
    }

    /** Output level 0..1 for the volume knob's meter. */
    float Meter() const { return meter_; }

    // ---------------------------------------------------------------- audio

    void Process(float* out_l, float* out_r, size_t size)
    {
        const float dt = static_cast<float>(size) / sr_;
        block_         = size;
        UpdateParams(dt);

        const uint8_t gates = key_gates_ | midi_gates_;
        for(int v = 0; v < kNumVoices; v++)
            voices_[v].SetGate(gates & (1u << v));

        // Which pair modulates each pair when its source is FM.
        const int ring[kNumPairs] = {one_loop_ ? 3 : 1, 0, one_loop_ ? 1 : 3, 2};

        float sounding = 0.f;
        for(int v = 0; v < kNumVoices; v++)
            sounding += voices_[v].Level();
        sounding = sounding > 1.f ? 1.f : sounding;

        float peak = 0.f;
        for(size_t i = 0; i < size; i++)
        {
            lfo_.Process();
            const float lfo_mod = total_fb_ ? fb_sig_ : lfo_.Out();

            for(int v = 0; v < kNumVoices; v++)
                voices_[v].TickEnv(att_[v / 2], rel_[v / 2], hold_[v]);

            float hl[2], hr[2];
            for(int s = 0; s < 2; s++)
            {
                // Modulation for each pair, from the last oversampled tick.
                float fm[kNumPairs];
                for(int p = 0; p < kNumPairs; p++)
                {
                    float m = 0.f;
                    if(src_[p] == SRC_FM)
                    {
                        const int q = ring[p];
                        m = 0.5f * (voices_[2 * q].Out() + voices_[2 * q + 1].Out());
                    }
                    else if(src_[p] == SRC_LFO)
                        m = lfo_mod;
                    fm[p] = m * depth_inc_[p];
                }

                float l = 0.f, r = 0.f;
                for(int v = 0; v < kNumVoices; v++)
                {
                    const float inc = Clamp(inc_[v] + fm[v / 2], -0.45f, 0.45f);
                    const float y   = voices_[v].Tick(inc, g_[v], norm_[v]);
                    l += y * pan_l_[v];
                    r += y * pan_r_[v];
                }
                hl[s] = l;
                hr[s] = r;
            }
            float l = hb_l_.Process(hl[0], hl[1]);
            float r = hb_r_.Process(hr[0], hr[1]);

            // A little hiss under the voices: the Lyra is never quite clean,
            // and it gives a self-oscillating delay something to grow from.
            // It follows how much is sounding, so nothing playing is silence.
            const float n = noise_.Process() * noise_amt_ * sounding;
            l += n;
            r += n;
            lv_voices_ = fmaxf(lv_voices_, fmaxf(fabsf(l), fabsf(r)));

            delay_.Process(&l, &r, lfo_.Tri(), lfo_.Square());

            l = dist_.Process(l);
            r = dist_.Process(r);
            lv_dist_ = fmaxf(lv_dist_, fmaxf(fabsf(l), fabsf(r)));

            // The global filter. Run at half level so the ladder's input
            // saturator stays out of the way when it's wide open.
            l = filt_l_.Process(l * 0.5f) * 2.f;
            r = filt_r_.Process(r * 0.5f) * 2.f;

            // TOTAL FB taps here, after the filter (so closing the filter
            // calms the feedback too), before the volume.
            fb_sig_ = Clamp(l + r, -1.f, 1.f);

            // DC blocker (the fuzz is asymmetric)
            dc_l_ += (l - dc_l_) * 0.0005f;
            dc_r_ += (r - dc_r_) * 0.0005f;
            l -= dc_l_;
            r -= dc_r_;

            // Smoothed master volume, then a fast peak limiter.
            vol_ += (volume - vol_) * 0.002f;
            l *= vol_ * 2.f;
            r *= vol_ * 2.f;
            const float p = fabsf(l) > fabsf(r) ? fabsf(l) : fabsf(r);
            lim_env_      = p > lim_env_ ? p : lim_env_ * 0.9998f;
            const float g = lim_env_ > 0.95f ? 0.95f / lim_env_ : 1.f;
            lv_limit_     = fminf(lv_limit_, g);
            out_l[i]      = l * g;
            out_r[i]      = r * g;
            if(p > peak)
                peak = p;
        }
        meter_ = peak > meter_ ? peak : meter_ * 0.97f;
    }

  private:
    // ------------------------------------------------- knobs -> units

    void UpdateParams(float dt)
    {
        const float* p = params;

        // Where the whole instrument sits: octave, transpose, fine, bend.
        const float global = 12.f * (StepIndex(p[OCTAVE], 5) - 2)
                             + (StepIndex(p[TRANSPOSE], 25) - 12) + (p[FINE_TUNE] - 0.5f)
                             + bend_ * (StepIndex(p[BEND_RANGE], 12) + 1);
        const float vib       = 0.6f * p[VIBRATO] * p[VIBRATO];
        const float character = p[CHARACTER];

        for(int v = 0; v < kNumVoices; v++)
        {
            const int   pair  = v / 2;
            const int   group = v / 4;
            const float note  = kHomeNote[v] + (p[TUNE1 + v] - 0.5f) * kTuneRange
                               + (p[PITCH_A + group] - 0.5f) * 24.f + global;
            const float hz = MidiToHz(voices_[v].BlockPitch(note, vib, character, dt));
            inc_[v]        = hz / sr2_;

            // SHARP: 1 = triangle .. 30 = square. High voices are held back
            // a little so their edges stay two samples wide (no aliasing).
            const float sh = p[SHARP1 + pair];
            float       g  = 1.f + 29.f * sh * sh;
            const float gmax = 24000.f / hz;
            if(g > gmax)
                g = gmax > 1.f ? gmax : 1.f;
            g_[v]    = g;
            norm_[v] = 1.f / FastTanh(g);

            // HOLD: a floor the voice never drops below. FAST voices need
            // more of it before they sound.
            const float hold = p[HOLD_A + group];
            const bool  fast = StepIndex(p[FAST1 + pair], 2) == 1;
            const float h    = fast ? Clamp((hold - 0.35f) / 0.65f, 0.f, 1.f) : hold;
            hold_[v]         = h * h;

            // Pan: odd voices lean left, even right, so each pair is a
            // stereo image and the four pairs interleave.
            const float spread = p[SPREAD] * (0.4f + 0.15f * pair);
            const float pan    = (v & 1) ? spread : -spread;
            pan_l_[v]          = kVoiceGain * (pan > 0.f ? 1.f - pan : 1.f);
            pan_r_[v]          = kVoiceGain * (pan < 0.f ? 1.f + pan : 1.f);
        }

        const float att = KnobToTime(p[ATTACK], 0.01f, 5.f);
        const float rel = KnobToTime(p[RELEASE], 0.05f, 10.f);
        for(int q = 0; q < kNumPairs; q++)
        {
            const bool fast = StepIndex(p[FAST1 + q], 2) == 1;
            // TimeToCoef settles in ~1 time constant; ~3 to be all the way.
            att_[q] = TimeToCoef((fast ? att * 0.15f : att) / 3.f, sr_);
            rel_[q] = TimeToCoef((fast ? 0.12f : rel) / 3.f, sr_);

            const float d  = p[MOD1 + q];
            // Up to 1500 Hz of swing: a low voice goes through zero long
            // before that, a high one just wobbles. As on the Lyra.
            depth_inc_[q] = d * d * 1500.f / sr2_;
            src_[q]       = static_cast<ModSrc>(StepIndex(p[SRC1 + q], 3));
        }

        total_fb_ = StepIndex(p[TOTAL_FB], 2) == 1;

        lfo_.SetFreqs(KnobToTime(p[LFO_A], 0.05f, 600.f), KnobToTime(p[LFO_B], 0.05f, 600.f));
        lfo_.SetAnd(StepIndex(p[LFO_MODE], 2) == 1);
        lfo_.SetLink(StepIndex(p[LFO_LINK], 2) == 1);

        delay_.SetTimes(KnobToTime(p[DLY_TIME1], 0.005f, 1.4f), KnobToTime(p[DLY_TIME2], 0.005f, 1.4f));
        delay_.SetFeedback(p[DLY_FB]);
        delay_.SetMod(p[DLY_MOD1], p[DLY_MOD2]);
        delay_.SetSelf(StepIndex(p[DLY_SRC], 2) == 0);
        delay_.SetSquare(StepIndex(p[DLY_SHAPE], 2) == 1);
        delay_.SetMix(p[DLY_MIX]);

        dist_.Set(p[DIST_DRIVE], p[DIST_MIX]);

        // Global filter: 20 Hz - 20 kHz, resonance up to self-oscillation.
        const float cutoff = KnobToTime(p[CUTOFF], 20.f, 20000.f);
        filt_l_.SetCutoffBlock(cutoff, block_);
        filt_r_.SetCutoffBlock(cutoff, block_);
        filt_l_.SetResonance(p[RESONANCE]);
        filt_r_.SetResonance(p[RESONANCE]);

        noise_amt_ = 0.004f * character * character;
    }

    // Eight voices at full level and in phase reach 0.8; in practice a
    // drone sits well under the limiter.
    static constexpr float kVoiceGain = 0.1f;

    float      sr_ = 48000.f, sr2_ = 96000.f;
    Voice      voices_[kNumVoices];
    HyperLfo   lfo_;
    Halfband   hb_l_, hb_r_;
    ModDelay   delay_;
    Distortion dist_;
    Ladder     filt_l_, filt_r_;
    size_t     block_ = 24;
    Noise      noise_;

    uint8_t key_gates_ = 0, midi_gates_ = 0;
    bool    one_loop_  = true;
    bool    total_fb_  = false;
    float   bend_      = 0.f;
    float   fb_sig_    = 0.f;

    float  inc_[kNumVoices] = {}, g_[kNumVoices] = {}, norm_[kNumVoices] = {};
    float  hold_[kNumVoices] = {};
    float  pan_l_[kNumVoices] = {}, pan_r_[kNumVoices] = {};
    float  att_[kNumPairs] = {}, rel_[kNumPairs] = {};
    float  depth_inc_[kNumPairs] = {};
    ModSrc src_[kNumPairs] = {};
    float  noise_amt_ = 0.f;

    float dc_l_ = 0.f, dc_r_ = 0.f;
    float vol_     = 0.f;
    float lim_env_ = 0.f;
    float meter_   = 0.f;
    float lv_voices_ = 0.f, lv_dist_ = 0.f, lv_limit_ = 1.f;
};

} // namespace hmmm
