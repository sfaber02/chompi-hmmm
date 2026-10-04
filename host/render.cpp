// Desktop harness: runs the exact engine the firmware runs and writes WAV
// files, so sounds can be checked by ear and FFT without flashing.
//
//   make -C host && host/render out_dir
//   host/render --factory card_dir wav_dir     factory patches as files + demos
//
// Each test is a patch (param overrides) plus a score of key presses.

#include "../code/src/hmmm/engine.h"
#include "../code/src/hmmm/factory.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <chrono>

using namespace hmmm;

static constexpr float  kSr     = 48000.f;
static constexpr size_t kBlock  = 24;
static constexpr size_t kFrames = 72000; // 1.5 s per delay line

static int16_t g_delay[2 * kFrames];
static Engine  g_engine;

struct Press
{
    float on_s, off_s;
    int   voice; // 0-7
};

struct Test
{
    std::string                          name;
    std::vector<std::pair<Param, float>> patch;
    std::vector<Press>                   presses;
    float                                seconds;
    bool                                 one_loop = true;
};

static void WriteWav(const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    FILE* f = fopen(path.c_str(), "wb");
    if(!f)
    {
        perror(path.c_str());
        exit(1);
    }
    const uint32_t n = l.size(), data = n * 4, sr = kSr, br = kSr * 4, riff = 36 + data;
    const uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
    const uint32_t fmt_len = 16;
    fwrite("RIFF", 1, 4, f);
    fwrite(&riff, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmt_len, 4, 1, f);
    fwrite(&pcm, 2, 1, f);
    fwrite(&ch, 2, 1, f);
    fwrite(&sr, 4, 1, f);
    fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data, 4, 1, f);
    for(uint32_t i = 0; i < n; i++)
    {
        int16_t s[2] = {static_cast<int16_t>(Clamp(l[i], -1.f, 1.f) * 32767.f),
                        static_cast<int16_t>(Clamp(r[i], -1.f, 1.f) * 32767.f)};
        fwrite(s, 2, 2, f);
    }
    fclose(f);
}

/** Peak of the last `tail_s` seconds: does the sound keep itself going? */
static float TailPeak(const std::vector<float>& l, float tail_s)
{
    float  p = 0.f;
    size_t from = l.size() - static_cast<size_t>(tail_s * kSr);
    for(size_t i = from; i < l.size(); i++)
        p = std::max(p, fabsf(l[i]));
    return p;
}

static void Run(const Test& t, const std::string& dir)
{
    Engine& e = g_engine;
    e.Init(kSr, g_delay, kFrames);
    e.volume = 0.7f;
    for(auto& kv : t.patch)
        e.params[kv.first] = kv.second;
    e.SetOneLoop(t.one_loop);

    const size_t       total = static_cast<size_t>(t.seconds * kSr / kBlock);
    std::vector<float> L, R;
    L.reserve(total * kBlock);
    R.reserve(total * kBlock);
    float  l[kBlock], r[kBlock];
    float  peak = 0.f;
    double cpu  = 0.0;

    for(size_t b = 0; b < total; b++)
    {
        const float now   = b * kBlock / kSr;
        uint8_t     gates = 0;
        for(auto& p : t.presses)
            if(now >= p.on_s && now < p.off_s)
                gates |= 1u << p.voice;
        e.SetKeyGates(gates);

        auto a = std::chrono::steady_clock::now();
        e.Process(l, r, kBlock);
        cpu += std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
        for(size_t i = 0; i < kBlock; i++)
        {
            L.push_back(l[i]);
            R.push_back(r[i]);
            peak = std::max(peak, std::max(fabsf(l[i]), fabsf(r[i])));
            if(!std::isfinite(l[i]) || !std::isfinite(r[i]))
            {
                printf("%s: NaN/inf at %.3fs\n", t.name.c_str(), (b * kBlock + i) / kSr);
                exit(1);
            }
        }
    }
    WriteWav(dir + "/" + t.name + ".wav", L, R);
    printf("%-18s peak %.2f  last-1s %.3f  render %.0fx realtime\n", t.name.c_str(), peak,
           TailPeak(L, 1.f), t.seconds / cpu);
}

/** Keys pressed one after another, then all let go together. */
static std::vector<Press> Roll(std::initializer_list<int> voices, float start, float step, float off)
{
    std::vector<Press> v;
    float              t = start;
    for(int x : voices)
    {
        v.push_back({t, off, x});
        t += step;
    }
    return v;
}

// ---------------------------------------------------------------------------
// Factory patches (code/src/hmmm/factory.h): written out as P01.txt.. for
// reference (docs/factory-patches) and rendered as demos.

static void WriteFactory(const std::string& card_dir, const std::string& wav_dir)
{
    for(int i = 0; i < kNumFactoryPatches; i++)
    {
        const FactoryPatch& f = kFactoryPatches[i];
        float               p[NUM_PARAMS];
        for(int k = 0; k < NUM_PARAMS; k++)
            p[k] = kParams[k].def;
        ApplyFactory(f, p);

        char name[64];
        snprintf(name, sizeof name, "%s/P%02d.txt", card_dir.c_str(), f.slot);
        FILE* fp = fopen(name, "w");
        if(!fp)
        {
            perror(name);
            exit(1);
        }
        for(int k = 0; k < NUM_PARAMS; k++)
            if(!kParams[k].global) // instrument settings are not part of a patch
                fprintf(fp, "%s %.4f\n", kParams[k].name, p[k]);
        fclose(fp);

        Test t;
        t.name = std::string("P") + std::to_string(f.slot) + "_" + f.name;
        for(int k = 0; k < f.count; k++)
            t.patch.push_back({static_cast<Param>(f.values[k].id), f.values[k].v});
        // Roll up through the voices, hold, let go and listen to the tail.
        t.presses = Roll({0, 1, 2, 3, 4, 5, 6, 7}, 0.2f, 0.5f, 6.f);
        t.seconds = 10.f;
        Run(t, wav_dir);
    }
}

int main(int argc, char** argv)
{
    if(argc > 3 && std::string(argv[1]) == "--factory")
    {
        WriteFactory(argv[2], argv[3]);
        return 0;
    }

    const std::string dir = argc > 1 ? argv[1] : ".";
    std::vector<Test> tests;

    // The default patch, one voice at a time, then a chord.
    tests.push_back({"organ_default", {}, Roll({0, 2, 4, 6}, 0.1f, 0.8f, 5.f), 8.f});

    // SHARP at full on the top pair, swept up two octaves with group PITCH
    // isn't possible mid-test, so: voices 7-8 tuned high, full square.
    // Listen / FFT for aliasing (fold-back below the fundamental).
    tests.push_back({"sharp_high",
                     {{SHARP4, 1.f}, {TUNE7, TuneSt(12)}, {TUNE8, TuneSt(12)}, {PITCH_B, 1.f},
                      {DLY_MIX, 0.f}, {CHARACTER, 0.f}, {ATTACK, 0.f}},
                     {{0.1f, 2.f, 6}}, 2.5f});

    // One pair FM'd by another at full depth: must stay finite.
    tests.push_back({"fm_max",
                     {{MOD1, 1.f}, {MOD2, 1.f}, {MOD3, 1.f}, {MOD4, 1.f}, {SHARP1, .5f},
                      {DLY_MIX, 0.f}},
                     Roll({0, 1, 2, 3, 4, 5, 6, 7}, 0.1f, 0.2f, 4.f), 6.f});

    // The same with two separate loops.
    Test two = tests.back();
    two.name     = "fm_max_two_loops";
    two.one_loop = false;
    tests.push_back(two);

    // Delay feedback past unity: one short touch, then it must keep singing
    // (last second well above the noise floor) without running away.
    tests.push_back({"delay_selfosc",
                     {{DLY_FB, .65f}, {DLY_MIX, .6f}, {DLY_MOD1, .2f}, {DLY_MOD2, .2f}},
                     {{0.1f, 0.6f, 2}}, 10.f});

    // Same with feedback below unity: the tail must die out.
    tests.push_back({"delay_decay",
                     {{DLY_FB, .4f}, {DLY_MIX, .6f}}, {{0.1f, 0.6f, 2}}, 10.f});

    // SELF modulation of the delay, hard.
    tests.push_back({"delay_self_mod",
                     {{DLY_FB, .7f}, {DLY_MIX, .7f}, {DLY_SRC, 0.f}, {DLY_MOD1, 1.f}, {DLY_MOD2, 1.f}},
                     Roll({0, 3, 5}, 0.1f, 0.5f, 3.f), 6.f});

    // Hyper LFO into audio rate on two pairs.
    tests.push_back({"lfo_audio",
                     {{SRC1, 1.f}, {SRC2, 1.f}, {MOD1, .5f}, {MOD2, .5f}, {LFO_A, .85f},
                      {LFO_B, .8f}, {LFO_LINK, 1.f}},
                     Roll({0, 1, 2, 3}, 0.1f, 0.3f, 4.f), 5.f});

    // HOLD: nothing pressed at all, groups droning on their own.
    tests.push_back({"hold", {{HOLD_A, .6f}, {HOLD_B, .4f}}, {}, 4.f});

    // FAST pairs need more HOLD: at HOLD .3, pair 12 FAST must be silent
    // while 34 drones.
    tests.push_back({"hold_fast",
                     {{HOLD_A, .3f}, {FAST1, 1.f}, {DLY_MIX, 0.f}}, {}, 3.f});

    // Everything maxed at once: must stay finite and under the limiter.
    {
        Test s{"stress", {}, Roll({0, 1, 2, 3, 4, 5, 6, 7}, 0.1f, 0.05f, 5.f), 6.f};
        for(int q = 0; q < kNumPairs; q++)
        {
            s.patch.push_back({static_cast<Param>(SHARP1 + q), 1.f});
            s.patch.push_back({static_cast<Param>(MOD1 + q), 1.f});
            s.patch.push_back({static_cast<Param>(SRC1 + q), 1.f});
        }
        for(auto kv : std::initializer_list<std::pair<Param, float>>{
                {TOTAL_FB, 1.f}, {HOLD_A, 1.f}, {HOLD_B, 1.f}, {VIBRATO, 1.f}, {LFO_A, 1.f},
                {LFO_B, 1.f}, {LFO_LINK, 1.f}, {DLY_FB, 1.f}, {DLY_MIX, 1.f}, {DLY_MOD1, 1.f},
                {DLY_MOD2, 1.f}, {DLY_SRC, 0.f}, {DIST_DRIVE, 1.f}, {DIST_MIX, 1.f},
                {CHARACTER, 1.f}, {PITCH_A, 1.f}, {PITCH_B, 1.f}})
            s.patch.push_back(kv);
        tests.push_back(s);
    }

    for(auto& t : tests)
        Run(t, dir);
    return 0;
}
