// Boot-silence check: fill the engine with junk (as uninitialised RAM is
// after another firmware ran), start it, touch no keys, report the peak.
#include "../code/src/hum/engine.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
using namespace hum;
static int16_t g_delay[2 * 72000];
static Engine  g_engine;
int main()
{
    memset((void*)&g_engine, 0x5A, sizeof g_engine);
    memset(g_delay, 0x5A, sizeof g_delay);
    g_engine.Init(48000.f, g_delay, 72000);
    g_engine.params[DLY_MIX] = 0.6f;
    float l[24], r[24], peak = 0.f;
    for(int b = 0; b < 2000 * 3; b++)
    {
        g_engine.Process(l, r, 24);
        for(int i = 0; i < 24; i++)
            peak = std::max(peak, std::max(fabsf(l[i]), fabsf(r[i])));
    }
    printf("boot peak with no keys: %.6f\n", peak);
    return peak > 0.0001f;
}
