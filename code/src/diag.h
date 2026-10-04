/** @file diag.h
 *  @brief Debug event log (build with DIAG=1). The audio interrupt records
 *  events into a RAM ring; the main loop appends them to /HMMM/diag.txt
 *  every couple of seconds.
 */
#pragma once
#include <cstdint>

#ifndef DIAG
#define DIAG 0
#endif

namespace chompi
{

enum DiagCode : uint8_t
{
    D_BOOT,      // a=0
    D_SHIFT,     // a=new shift state
    D_KEYDOWN,   // a=switch id, b=shift
    D_KEYUP,     // a=switch id
    D_PAGE,      // a=page
    D_TOGGLE,    // a=toggle state
    D_KNOB,      // a=knob, b=param id (255 = none)
    D_CLICK,     // a=knob, b=shift
    D_LOAD,      // a=slot, b=ok
    D_SAVE,      // a=slot, b=ok
    D_STALL,     // a=main loop gap ms (capped 255), b=what
    D_EARLY,     // Poll returned early (boot ignore window)
    D_LEVEL,     // a=voices peak %, b=delay input peak % (bends from 150)
    D_LEVEL2,    // a=distortion output peak %, b=limiter gain % (100 = untouched)
    D_CPU,       // a=average audio CPU %, b=worst single block %
};

struct DiagEvent
{
    uint32_t ms;
    uint8_t  code, a, b, pad;
};

struct DiagLog
{
    static constexpr uint32_t kSize = 512;
    DiagEvent        ev[kSize];
    volatile uint32_t head = 0; // written by the ISR (and main for load/save)
    uint32_t         tail = 0;  // main loop only
    volatile uint32_t dropped = 0;

    inline void Add(uint32_t ms, uint8_t code, uint8_t a = 0, uint8_t b = 0)
    {
#if DIAG
        const uint32_t h = head;
        if(h - tail >= kSize)
        {
            dropped = dropped + 1;
            return;
        }
        ev[h % kSize] = {ms, code, a, b, 0};
        head          = h + 1;
#else
        (void)ms, (void)code, (void)a, (void)b;
#endif
    }
};

extern DiagLog diag;

} // namespace chompi
