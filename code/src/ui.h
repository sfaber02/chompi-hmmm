/** @file ui.h
 *  @brief Keys, knobs and LEDs.
 *
 *  Poll() runs in the audio interrupt, once per block, right after the
 *  hardware is scanned, so key presses reach the engine within a block.
 *  Draw() runs from the main loop about 60 times a second and only reads.
 *  Preset loads and saves are requested here and carried out by the main loop
 *  (they touch the SD card).
 *
 *  Controls (the stock CHOMPI idiom: CHOMPI is shift):
 *    white keys 1-8          the eight voices: hold to sound
 *    LOOP                    latch: tap a voice on, tap it off. Turning it
 *                            on keeps whatever you're holding; off lets go
 *    PLAY                    TOTAL FB on/off
 *    toggle switch           FM structure: up one big loop, down two
 *    knobs 1-4               the current page's four parameters
 *    CHOMPI + knob 1-4       the page's second layer (voice TUNE pages:
 *                            the same knob in whole semitones)
 *    CHOMPI + click knob     reset it to default
 *    big purple knob         delay feedback on every page (CHOMPI: delay mix)
 *    volume knob             master volume (CHOMPI: distortion drive);
 *                            click: everything off
 *    CHOMPI + black key      choose page
 *    CHOMPI + white key      load preset; hold 1 s to save
 *    CHOMPI + PLAY / LOOP    octave down / up (the TUNE page's octave)
 */
#pragma once
#include "hardware.h"
#include "temp_led_stuff.h"
#include "hmmm/engine.h"
#include "diag.h"

namespace chompi
{

using namespace hmmm;
using Sw = Hardware::SwId;

// Key LED (SMT chain index) for each switch id.
static const uint8_t kKeyLed[40] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    23, 22, 21, 20, 1, 2, 3, 24,
    19, 18, 17, 16, 15, 4, 5, 6,
    14, 13, 12, 11, 10, 7, 8, 9,
    0, 0, 0, 0, 0, 0, 0, 0,
};

static const Sw kWhiteKeys[15] = {Sw::KEY_1, Sw::KEY_2, Sw::KEY_3, Sw::KEY_4, Sw::KEY_5,
                                  Sw::KEY_6, Sw::KEY_7, Sw::KEY_8, Sw::KEY_9, Sw::KEY_10,
                                  Sw::KEY_11, Sw::KEY_12, Sw::KEY_13, Sw::KEY_14, Sw::KEY_15};
static const Sw kBlackKeys[10] = {Sw::KEY_16, Sw::KEY_17, Sw::KEY_18, Sw::KEY_19, Sw::KEY_20,
                                  Sw::KEY_21, Sw::KEY_22, Sw::KEY_23, Sw::KEY_24, Sw::KEY_25};

// Physical encoder -> knob number (left to right), and each knob's LED(s).
static const uint8_t kEncoderKnob[6] = {1, 2, 3, 0, 4, 5};
static const uint8_t kKnobLed[6]     = {1, 2, 3, 4, 5, 9};
static const uint8_t kKnobLed2[6]    = {1, 2, 3, 4, 6, 9}; // knob 5 has two LEDs
static const Sw      kKnobClick[6]   = {Sw::ENC_4_SW, Sw::ENC_1_SW, Sw::ENC_2_SW,
                                        Sw::ENC_3_SW, Sw::NC_6, Sw::ENC_6_SW};
// (knob 5's click is not on the shift register; Poll() reads it from the encoder)

constexpr int      kLedChompi    = 0;
constexpr int      kLedPlay      = 7;
constexpr int      kLedLoop      = 8;
constexpr uint32_t kShowValueMs  = 1200;
constexpr uint32_t kSaveHoldMs   = 1000;
constexpr uint32_t kIgnoreBootMs = 1000;

// Voice TUNE knobs: a click is this many cents (fast turns: the second).
constexpr float kTuneCents     = 2.f;
constexpr float kTuneCentsFast = 10.f;

class Ui
{
  public:
    // Requests for the main loop (it clears them once done).
    volatile int  load_slot  = -1;
    volatile int  save_slot  = -1;
    volatile bool dirty      = false; // something changed since the last autosave
    volatile uint32_t last_change = 0;
    volatile uint16_t slots_used  = 0; // bit n = /HMMM/P(n+1).txt exists; main keeps it

    void Init(Hardware* hw, Engine* engine)
    {
        hw_     = hw;
        engine_ = engine;
        start_  = System::GetNow();
    }

    // ------------------------------------------------------------- input

    void Poll()
    {
        const uint32_t now = System::GetNow();
        auto&          sr  = hw_->button_sr;

        // Switch down = two FM loops (12<->34, 56<->78), up = one big loop.
        const bool tog = hw_->GetToggleState();
        if(tog != last_tog_)
        {
            diag.Add(now, D_TOGGLE, tog);
            last_tog_ = tog;
        }
        engine_->SetOneLoop(!tog);

        if(now - start_ < kIgnoreBootMs)
        {
            if(!early_logged_)
            {
                diag.Add(now, D_EARLY);
                early_logged_ = true;
            }
            return;
        }

        shift_ = sr.State(static_cast<int>(Sw::KEY_26));
        if(shift_ != last_shift_)
        {
            diag.Add(now, D_SHIFT, shift_);
            last_shift_ = shift_;
        }

        // Keys
        for(int s = 0; s < 15; s++)
        {
            const int i = static_cast<int>(kWhiteKeys[s]);
            if(sr.RisingEdge(i))
            {
                diag.Add(now, D_KEYDOWN, i, shift_);
                WhiteDown(s, now);
            }
            else if(sr.FallingEdge(i))
            {
                diag.Add(now, D_KEYUP, i);
                WhiteUp(s, now);
            }
        }
        for(int p = 0; p < NUM_PAGES; p++)
        {
            if(Pressed(kBlackKeys[p]) && shift_)
            {
                page_          = static_cast<Page>(p);
                shown_at_      = 0;
                page_flash_at_ = now;
                diag.Add(now, D_PAGE, p);
            }
        }
        engine_->SetKeyGates(held_ | latched_);

        // A preset key held long enough saves, while you're still holding it.
        if(preset_key_ >= 0 && !preset_saved_ && now - preset_down_ >= kSaveHoldMs)
        {
            save_slot     = preset_key_;
            preset_saved_ = true;
            Flash(preset_key_, true, now);
        }

        // PLAY and LOOP
        if(Pressed(Sw::KEY_27))
        {
            if(shift_)
                StepParam(OCTAVE, -1, now); // shortcut for the TUNE page's octave
            else
                StepParam(TOTAL_FB, StepIndex(engine_->params[TOTAL_FB], 2) ? -1 : 1, now);
        }
        if(Pressed(Sw::KEY_28))
        {
            if(shift_)
                StepParam(OCTAVE, +1, now);
            else
            {
                // Turning latch on keeps what you're holding; turning it
                // off lets go of everything that isn't held down.
                latch_   = !latch_;
                latched_ = latch_ ? held_ : 0;
            }
        }

        // Knob clicks
        for(int k = 0; k < 6; k++)
        {
            bool clicked;
            if(k == 4)
                clicked = hw_->enc[4].RisingEdge();
            else
                clicked = Pressed(kKnobClick[k]);
            if(clicked)
            {
                diag.Add(now, D_CLICK, k, shift_);
                KnobClick(k, now);
            }
        }

        // Knob turns
        for(int e = 0; e < 6; e++)
        {
            const int inc = hw_->enc[e].Increment();
            if(inc != 0)
                KnobTurn(kEncoderKnob[e], inc, now);
        }
    }

    // ------------------------------------------------------------- MIDI

    void MidiCc(int cc, int value)
    {
        const float v = value / 127.f;
        if(cc == 7)
            engine_->volume = v;
        else if(cc == 120 || cc == 123)
            engine_->AllNotesOff();
        else
        {
            for(int i = 0; i < NUM_PARAMS; i++)
            {
                if(kParams[i].cc == cc)
                {
                    const int steps    = kParams[i].steps;
                    engine_->params[i] = steps ? StepValue(StepIndex(v, steps), steps) : v;
                    Changed(i, System::GetNow());
                    break;
                }
            }
        }
    }

    // ------------------------------------------------------------- LEDs

    void Draw(float cpu_load)
    {
        const uint32_t now     = System::GetNow();
        const float*   page_c  = kPageColour[page_];
        const bool     showing = now - shown_at_ < kShowValueMs && shown_param_ >= 0;

        // Knob LEDs: page colour, brightness follows the value. Holding
        // CHOMPI shows the second layer, and knobs without one go dark.
        for(int k = 0; k < kPageKnobs; k++)
        {
            const int id = ParamAt(k, shift_);
            float     b  = id < 0 ? 0.f : 0.08f + 0.92f * engine_->params[id];
            if(id >= 0 && kParams[id].bipolar) // centre = dim, either end bright
                b = 0.15f + 0.85f * fabsf(engine_->params[id] - 0.5f) * 2.f;
            SetPthLedFloat(kKnobLed[k], page_c[0] * b, page_c[1] * b, page_c[2] * b);
        }

        // Big purple knob: delay feedback (or mix with CHOMPI), in the
        // delay's teal, turning white once the loop sings by itself.
        {
            const float* c  = kPageColour[PAGE_DELAY];
            const float  v  = engine_->params[shift_ ? DLY_MIX : DLY_FB];
            const float  b  = 0.1f + 0.9f * v;
            const float  wh = !shift_ && v > 0.555f ? 0.8f : 0.f;
            const float  r = (c[0] + (1.f - c[0]) * wh) * b, g = (c[1] + (1.f - c[1]) * wh) * b,
                        bl = (c[2] + (1.f - c[2]) * wh) * b;
            SetPthLedFloat(kKnobLed[4], r, g, bl);
            SetPthLedFloat(kKnobLed2[4], r, g, bl);
        }

        // Volume knob: a level meter, or the CPU load while CHOMPI is held.
        {
            const float m = shift_ ? cpu_load : Clamp(engine_->Meter(), 0.f, 1.f);
            const float b = shift_ ? 1.f : 0.15f + 0.85f * engine_->volume;
            SetPthLedFloat(kKnobLed[5], color_triple_xfade(0.f, 1.f, 1.f, m) * b,
                           color_triple_xfade(1.f, .9f, 0.f, m) * b, 0.f);
        }

        // CHOMPI, PLAY (TOTAL FB), LOOP (latch)
        SetPthLedFloat(kLedChompi, shift_ ? 1.f : 0.f, shift_ ? 1.f : 0.f, shift_ ? 1.f : 0.f);
        const bool tfb = StepIndex(engine_->params[TOTAL_FB], 2) == 1;
        SetPthLedFloat(kLedPlay, tfb ? 1.f : 0.f, tfb ? 0.25f : 0.f, 0.f);
        SetPthLedFloat(kLedLoop, latch_ ? 1.f : 0.f, 0.f, 0.f);

        // Keybed
        for(int i = 0; i < 25; i++)
            SetSmtLed(i, 0, 0, 0);

        if(now - page_flash_at_ < 350)
        {
            // New page: the whole keybed blinks its colour once.
            const float b = 1.f - (now - page_flash_at_) / 350.f;
            for(int s = 0; s < 15; s++)
                SetSmtLedFloat(WhiteLed(s), page_c[0] * b, page_c[1] * b, page_c[2] * b);
        }
        else if(showing)
            DrawValueBar(shown_colour_);
        else if(shift_)
            DrawShiftMenu(now);
        else
            DrawVoices();

        // Black keys. Holding CHOMPI shows the whole page map (that's when
        // you pick a page). While a knob's value is showing, only the
        // current page's key lights, so the keybed reads as one thing.
        const int tune_led = BlackLed(PAGE_TUNE);
        if(shift_)
        {
            for(int p = 0; p < NUM_PAGES; p++)
            {
                const float  b = p == page_ ? 1.f : 0.12f;
                const float* c = kPageColour[p];
                SetSmtLedFloat(BlackLed(p), c[0] * b, c[1] * b, c[2] * b);
            }
        }
        else if(showing)
            SetSmtLedFloat(BlackLed(page_), page_c[0], page_c[1], page_c[2]);

        // Shifted? The TUNE page's black key glows amber whenever octave,
        // transpose or fine tune is off centre, so you can't forget. Turning
        // a TUNE knob it switches live between amber (shifted) and white
        // (centred), so you can see the exact moment you reach zero.
        if(!shift_)
        {
            if(showing && page_ == PAGE_TUNE)
            {
                if(TuneShifted())
                    SetSmtLedFloat(tune_led, 1.f, 0.55f, 0.f);
                else
                    SetSmtLedFloat(tune_led, 1.f, 1.f, 1.f);
            }
            else if(TuneShifted())
            {
                const float b = 0.35f + 0.15f * sinf(now * 0.004f);
                SetSmtLedFloat(tune_led, b, b * 0.55f, 0.f);
            }
        }

        // Preset load / save confirmation.
        if(flash_slot_ >= 0 && now - flash_at_ < 600)
        {
            const bool on  = ((now - flash_at_) / 100) % 2 == 0;
            const int  led = WhiteLed(flash_slot_);
            if(flash_save_)
                SetSmtLedFloat(led, on ? 1.f : 0.f, 0.f, 0.f);
            else
                SetSmtLedFloat(led, 0.f, on ? 1.f : 0.f, 0.f);
        }

        fill_led_data();
    }

    int  CurrentSlot() const { return current_slot_; }
    void SetCurrentSlot(int s) { current_slot_ = s; }

    /** Everything off: latched voices and held ones (until pressed again). */
    void AllOff()
    {
        latched_ = 0;
        held_    = 0;
        engine_->AllNotesOff();
    }

  private:
    static int WhiteLed(int s) { return kKeyLed[static_cast<int>(kWhiteKeys[s])]; }
    static int BlackLed(int p) { return kKeyLed[static_cast<int>(kBlackKeys[p])]; }

    bool TuneShifted() const
    {
        const float* p = engine_->params;
        return StepIndex(p[OCTAVE], 5) != 2 || StepIndex(p[TRANSPOSE], 25) != 12
               || StepIndex(p[FINE_TUNE], 101) != 50;
    }

    void StepParam(int id, int dir, uint32_t now)
    {
        const int steps     = kParams[id].steps;
        int       i         = StepIndex(engine_->params[id], steps) + dir;
        i                   = i < 0 ? 0 : (i >= steps ? steps - 1 : i);
        engine_->params[id] = StepValue(i, steps);
        Changed(id, now);
    }

    /** A fresh press of a button that isn't a key.
     *  libDaisy's 4021 driver only reports a new rising edge after the
     *  falling edge has been read, so the release must be consumed too. */
    bool Pressed(Sw sw)
    {
        auto&     sr = hw_->button_sr;
        const int i  = static_cast<int>(sw);
        if(sr.RisingEdge(i))
            return true;
        sr.FallingEdge(i);
        return false;
    }

    /** Parameter under knob 0-3, on the shift layer if alt. -1 if none. */
    int ParamAt(int knob, bool alt) const
    {
        const uint8_t id = kPageParams[page_][knob + (alt ? kPageKnobs : 0)];
        return id == kNone ? -1 : id;
    }

    void Changed(int param, uint32_t now)
    {
        // The bar takes the colour of the page the parameter lives on.
        shown_colour_ = kPageColour[page_];
        if(param == OCTAVE)
            shown_colour_ = kPageColour[PAGE_TUNE];
        else if(param == DLY_FB || param == DLY_MIX)
            shown_colour_ = kPageColour[PAGE_DELAY];
        else if(param == DIST_DRIVE)
            shown_colour_ = kPageColour[PAGE_DIST];
        else if(param == TOTAL_FB)
            shown_colour_ = kPageColour[PAGE_MOD];
        shown_param_ = param;
        shown_at_    = now;
        dirty        = true;
        last_change  = now;
    }

    void Flash(int slot, bool save, uint32_t now)
    {
        flash_slot_ = slot;
        flash_save_ = save;
        flash_at_   = now;
    }

    void WhiteDown(int s, uint32_t now)
    {
        if(shift_)
        {
            preset_key_   = s;
            preset_down_  = now;
            preset_saved_ = false;
            return;
        }
        if(s >= kNumVoices)
            return;
        const uint8_t bit = 1u << s;
        held_ |= bit;
        if(latch_)
            latched_ ^= bit; // tap on, tap off
    }

    void WhiteUp(int s, uint32_t now)
    {
        if(s < kNumVoices)
            held_ &= ~(1u << s);

        if(preset_key_ == s)
        {
            const bool used = slots_used & (1u << s);
            if(!preset_saved_ && used)
            {
                load_slot = s;
                Flash(s, false, now);
            }
            if(used || preset_saved_)
                current_slot_ = s;
            preset_key_ = -1;
        }
    }

    void KnobClick(int knob, uint32_t now)
    {
        if(knob == 5)
        {
            AllOff();
            return;
        }
        if(knob == 4 || !shift_)
            return;
        const int id = ParamAt(knob, false);
        if(id >= 0)
        {
            engine_->params[id] = kParams[id].def;
            Changed(id, now);
        }
    }

    void KnobTurn(int knob, int inc, uint32_t now)
    {
        const bool fast = now - last_turn_[knob] < 25;
        last_turn_[knob] = now;

        if(knob == 5 && !shift_)
        {
            engine_->volume = Clamp(engine_->volume + inc * 0.01f, 0.f, 1.f);
            dirty           = true;
            last_change     = now;
            return;
        }

        int id;
        if(knob == 5)
            id = DIST_DRIVE; // CHOMPI + volume
        else if(knob == 4)
            id = shift_ ? DLY_MIX : DLY_FB;
        else
            id = ParamAt(knob, shift_);
        diag.Add(now, D_KNOB, knob, id < 0 ? 255 : id);
        if(id < 0)
            return;

        float&    v     = engine_->params[id];
        const int steps = kParams[id].steps;
        if(IsVoiceTune(id))
        {
            if(shift_)
            {
                // Whole semitones: snap to the nearest, then step.
                const float st = roundf((v - 0.5f) * kTuneRange) + (inc > 0 ? 1.f : -1.f);
                v              = 0.5f + st / kTuneRange;
            }
            else
                v += inc * (fast ? kTuneCentsFast : kTuneCents) / (100.f * kTuneRange);
        }
        else if(steps)
            v = StepValue(StepIndex(v, steps) + (inc > 0 ? 1 : -1), steps);
        else
            v += inc * (fast ? 0.024f : 0.008f);
        v = Clamp(v, 0.f, 1.f);
        Changed(id, now);
    }

    // Shows the last-touched value across the 15 white keys.
    void DrawValueBar(const float* c)
    {
        const ParamInfo& info = kParams[shown_param_];
        const float      v    = engine_->params[shown_param_];

        if(info.steps && info.steps <= 15) // more steps than keys: drawn as a bar below
        {
            const int idx = StepIndex(v, info.steps);
            for(int s = 0; s < info.steps && s < 15; s++)
            {
                const float b = s == idx ? 1.f : 0.1f;
                SetSmtLedFloat(WhiteLed(s), c[0] * b, c[1] * b, c[2] * b);
            }
            return;
        }

        for(int s = 0; s < 15; s++)
        {
            float b;
            if(info.bipolar)
            {
                // Grows outward from the middle key.
                const float pos = (s - 7) / 7.f;            // -1..1
                const float val = (v - 0.5f) * 2.f;         // -1..1
                if(s == 7)
                    b = 1.f;
                else if((pos > 0) == (val > 0) && fabsf(pos) <= fabsf(val) + 0.07f)
                    b = Clamp((fabsf(val) - fabsf(pos)) * 7.f + 1.f, 0.f, 1.f);
                else
                    b = 0.f;
            }
            else
                b = Clamp(v * 15.f - s, 0.f, 1.f);
            SetSmtLedFloat(WhiteLed(s), c[0] * b, c[1] * b, c[2] * b);
        }
    }

    // CHOMPI held: white keys are the patch slots. The one you're on is
    // blue, slots with a patch glow dim warm white, empty ones stay dark. A
    // key being held to save brightens towards the moment it saves.
    void DrawShiftMenu(uint32_t now)
    {
        for(int s = 0; s < 15; s++)
        {
            const int led = WhiteLed(s);
            if(s == preset_key_)
            {
                const float b = 0.3f + 0.7f * Clamp((now - preset_down_) / static_cast<float>(kSaveHoldMs), 0.f, 1.f);
                SetSmtLedFloat(led, b, b * 0.8f, b * 0.5f);
            }
            else if(s == current_slot_)
                SetSmtLedFloat(led, 0.f, 0.35f, 1.f);
            else if(slots_used & (1u << s))
                SetSmtLedFloat(led, 0.15f, 0.12f, 0.075f);
        }
    }

    // Playing: white keys 1-8 breathe with their voices, 1-4 warm and 5-8
    // cool. A latched voice keeps a faint marker even while it's quiet, so
    // you can see what's on.
    void DrawVoices()
    {
        for(int v = 0; v < kNumVoices; v++)
        {
            const float* c   = kGroupColour[v / 4];
            const float  lvl = engine_->VoiceLevel(v);
            float        b   = sqrtf(lvl); // perceived brightness
            if((latched_ & (1u << v)) && b < 0.12f)
                b = 0.12f;
            SetSmtLedFloat(WhiteLed(v), c[0] * b, c[1] * b, c[2] * b);
        }
    }

    Hardware* hw_     = nullptr;
    Engine*   engine_ = nullptr;
    uint32_t  start_  = 0;

    Page    page_    = PAGE_TUNE_A;
    bool    shift_   = false;
    bool    latch_   = false;
    uint8_t held_    = 0; // voice keys physically down
    uint8_t latched_ = 0; // voices latched on
    bool    last_shift_   = false;
    bool    last_tog_     = false;
    bool    early_logged_ = false;
    int     current_slot_ = -1;

    uint32_t     last_turn_[6]  = {};
    int          shown_param_   = -1;
    uint32_t     shown_at_      = 0;
    uint32_t     page_flash_at_ = 0;
    const float* shown_colour_  = kPageColour[PAGE_TUNE_A];

    int      preset_key_   = -1;
    uint32_t preset_down_  = 0;
    bool     preset_saved_ = false;

    int      flash_slot_ = -1;
    bool     flash_save_ = false;
    uint32_t flash_at_   = 0;
};

} // namespace chompi
