/** @file presets.h
 *  @brief Patches on the SD card as small text files, one "name value" per
 *  line, values 0..1:
 *
 *    /HUM/P01.txt .. P15.txt   the 15 slots (CHOMPI + white key)
 *    /HUM/current.txt          the sound as you left it, restored at boot
 *
 *  Unknown names are ignored and missing ones keep their current value, so
 *  files survive parameters being added or renamed. Writes go to a temp file
 *  that is renamed over the old one, so a power cut never leaves half a patch.
 *
 *  Main loop only: these block on the card.
 */
#pragma once
#include "fatfs.h"
#include "hum/params.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace chompi
{

class PresetStore
{
  public:
    /** out must hold 16 chars. */
    static void SlotName(int slot, char* out) { snprintf(out, 16, "P%02d.txt", (slot + 1) % 100); }

    /** @return true if the file existed and was read */
    /** @param globals  also read the global (instrument) settings; only
     *                  current.txt carries them, never a patch slot. */
    bool Load(const char* fname, float* params, float* volume = nullptr, bool globals = true)
    {
        if(f_open(&fil_, fname, FA_READ) != FR_OK)
            return false;
        UINT br = 0;
        f_read(&fil_, buf_, kBufSize - 1, &br);
        f_close(&fil_);
        buf_[br < kBufSize ? br : kBufSize - 1] = '\0';

        char* line = buf_;
        while(line && *line)
        {
            char* next = strchr(line, '\n');
            if(next)
                *next++ = '\0';

            char* sp = strchr(line, ' ');
            float val;
            if(sp && ParseUnit(sp + 1, &val))
            {
                *sp = '\0';
                if(volume && strcmp(line, "volume") == 0)
                    *volume = val;
                for(int i = 0; i < hum::NUM_PARAMS; i++)
                {
                    if(strcmp(line, hum::kParams[i].name) == 0)
                    {
                        if(hum::kParams[i].global && !globals)
                            break;
                        params[i] = val;
                        break;
                    }
                }
            }
            line = next;
        }
        return true;
    }

    bool Save(const char* fname, const float* params, const float* volume = nullptr, bool globals = true)
    {
        size_t len = 0;
        for(int i = 0; i < hum::NUM_PARAMS; i++)
            if(globals || !hum::kParams[i].global)
                Append(&len, hum::kParams[i].name, params[i]);
        if(volume)
            Append(&len, "volume", *volume);

        if(f_open(&fil_, "preset.tmp", FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
            return false;
        UINT bw = 0;
        f_write(&fil_, buf_, len, &bw);
        f_close(&fil_);
        if(bw != len)
            return false;
        f_unlink(fname);
        return f_rename("preset.tmp", fname) == FR_OK;
    }

  private:
    // The firmware links newlib-nano, whose printf has no %f (and whose
    // snprintf then misbehaved badly enough to overrun this buffer and
    // trample the UI's memory). So values are written and read by hand as
    // fixed-point "0.1234", and every append is bounds-checked.
    void Append(size_t* len, const char* name, float v)
    {
        const size_t room = kBufSize - *len;
        if(room < 48)
            return;
        int ticks = static_cast<int>(v * 10000.f + 0.5f);
        ticks     = ticks < 0 ? 0 : (ticks > 10000 ? 10000 : ticks);
        const int n = snprintf(buf_ + *len, room, "%s %d.%04d\n", name, ticks / 10000, ticks % 10000);
        if(n > 0 && static_cast<size_t>(n) < room)
            *len += n;
    }

    /** "0.1234" / "1" / "1.0000" -> 0..1. Rejects anything else. */
    static bool ParseUnit(const char* p, float* out)
    {
        while(*p == ' ')
            p++;
        if(*p != '0' && *p != '1')
            return false;
        const int whole = *p++ - '0';
        float     frac = 0.f, scale = 0.1f;
        if(*p == '.')
        {
            p++;
            while(*p >= '0' && *p <= '9')
            {
                frac += (*p++ - '0') * scale;
                scale *= 0.1f;
            }
        }
        const float v = whole + frac;
        if(v > 1.f)
            return false;
        *out = v;
        return true;
    }

    static constexpr size_t kBufSize = 2048;
    // The SD driver reads straight into buf_ by DMA and then invalidates the
    // data cache over it in whole 32-byte lines. If buf_ shared a line with
    // anything else (fil_ used to sit right before it), that other data would
    // be thrown away: the cause of flaky preset loads. So buf_ owns its lines.
    // And the PresetStore must be a global, never a local: the stack is in
    // DTCM, which the SD card's DMA cannot reach.
    alignas(32) char buf_[kBufSize];
    alignas(32) FIL  fil_;
};

} // namespace chompi
