/** @file chompi_main.cpp
 *  @brief HMMM: an eight-voice drone machine for the CHOMPI, after the
 *  SOMA Lyra-8.
 *
 *  Two places code runs:
 *   1. AudioCallback(): the audio interrupt, every 24 samples (0.5 ms). Scans
 *      the keys and knobs, reads MIDI, and renders audio. Everything that
 *      touches the engine's voices happens here, so no locking is needed.
 *   2. The main loop: LEDs, the SD card (presets, autosave) and the battery.
 *
 *  Hardware layer (hardware.h, encoder.*, temp_led_stuff.h) comes from
 *  CHOMPI Club's WAVE firmware; the rest of the plumbing from POLY.
 */
#include "hardware.h"
#include "temp_led_stuff.h"
#include "fatfs.h"
#include "OptionsManager.h"
#include "presets.h"
#include "hmmm/engine.h"
#include "hmmm/factory.h"
#include "ui.h"
#include "diag.h"

using namespace daisy;
using namespace chompi;

#define DSY_DTCMRAM_BSS __attribute__((section(".dtcmram_bss")))

static constexpr size_t kDelayFrames = 72000; // 1.5 s per line at 48 kHz

Hardware       hw;
hmmm::Engine    engine;
Ui             ui;
SdmmcHandler   sdmmc;
// The SD driver DMAs into the FATFS sector buffer inside this and then
// invalidates the data cache in whole 32-byte lines. Owning its lines keeps
// that from wiping variables the audio interrupt is writing (see presets.h).
struct alignas(32) AlignedFs
{
    FatFSInterface fsi;
};
AlignedFs      fs_holder;
FatFSInterface& fsi = fs_holder.fsi;
OptionsManager options;
PresetStore    presets;
MidiUartHandler midi_uart;
MidiUsbHandler  midi_usb;
CpuLoadMeter    cpu;

// The two delay lines, one after the other.
int16_t DSY_SDRAM_BSS delay_mem[2 * kDelayFrames];

bool          sd_ok   = false;
volatile bool running = false; // audio outputs silence until startup is done

DiagLog chompi::diag;

#if DIAG
// Same cache-line rules as presets.h.
struct alignas(32) DiagFile
{
    char buf[4096];
    FIL  fil;
};
DiagFile diag_file;

static void DiagMainAdd(uint8_t code, uint8_t a, uint8_t b)
{
    __disable_irq();
    diag.Add(System::GetNow(), code, a, b);
    __enable_irq();
}

/** Appends whatever the ring holds to diag.txt. */
static void DiagFlush()
{
    static const char* kNames[] = {"BOOT", "SHIFT", "KEYDOWN", "KEYUP", "PAGE", "TOGGLE",
                                   "KNOB", "CLICK", "LOAD", "SAVE", "STALL", "EARLY",
                                   "LEVEL", "LEVEL2", "CPU"};
    size_t len = 0;
    while(diag.tail != diag.head && len < sizeof(diag_file.buf) - 48)
    {
        const DiagEvent& e = diag.ev[diag.tail % DiagLog::kSize];
        len += snprintf(diag_file.buf + len, sizeof(diag_file.buf) - len, "%lu %s %u %u\n",
                        static_cast<unsigned long>(e.ms), e.code < 15 ? kNames[e.code] : "?",
                        e.a, e.b);
        diag.tail++;
    }
    if(diag.dropped)
    {
        len += snprintf(diag_file.buf + len, sizeof(diag_file.buf) - len, "DROPPED %lu\n",
                        static_cast<unsigned long>(diag.dropped));
        diag.dropped = 0;
    }
    if(len == 0)
        return;
    if(f_open(&diag_file.fil, "diag.txt", FA_OPEN_APPEND | FA_WRITE) == FR_OK)
    {
        // Whole sectors go to the card by DMA straight from our buffer, and
        // the SD DMA garbles data that doesn't start on a 4-byte boundary.
        // So top up the open sector first (that goes through FatFS's own
        // buffer), then slide the rest down to the aligned start of ours.
        size_t   off = 0;
        UINT     bw;
        const size_t fill = (512 - f_tell(&diag_file.fil) % 512) % 512;
        if(fill)
        {
            const size_t n = fill < len ? fill : len;
            f_write(&diag_file.fil, diag_file.buf, n, &bw);
            off = n;
        }
        if(off < len)
        {
            memmove(diag_file.buf, diag_file.buf + off, len - off);
            f_write(&diag_file.fil, diag_file.buf, len - off, &bw);
        }
        f_close(&diag_file.fil);
    }
}
#endif
uint8_t midi_ch_in = 0;

/** MIDI notes C3..C4 on the white keys (48 50 52 53 55 57 59 60) play
 *  voices 1-8, like the CHOMPI's own keys. Other notes are ignored. */
static int MidiVoice(int note)
{
    static const uint8_t kNotes[hmmm::kNumVoices] = {48, 50, 52, 53, 55, 57, 59, 60};
    for(int v = 0; v < hmmm::kNumVoices; v++)
        if(kNotes[v] == note)
            return v;
    return -1;
}

static void HandleMidi(const MidiEvent& ev)
{
    if(ev.channel != midi_ch_in)
        return;
    switch(ev.type)
    {
        case NoteOn: engine.SetMidiGate(MidiVoice(ev.data[0]), ev.data[1] > 0); break;
        case NoteOff: engine.SetMidiGate(MidiVoice(ev.data[0]), false); break;
        case ControlChange:
            if(options.midi_cc_in || ev.data[0] == 1)
                ui.MidiCc(ev.data[0], ev.data[1]);
            break;
        case PitchBend:
        {
            const int bend = (ev.data[1] << 7 | ev.data[0]) - 8192;
            engine.SetPitchBend(bend / 8192.f);
            break;
        }
        default: break;
    }
}

/** Loads slot 0-14: its file if there is one, else the built-in factory
 *  patch for that slot. A file starts from the defaults, so a patch saved by
 *  an older build with fewer parameters loads the same way every time.
 *  Main loop only (SD). */
static bool LoadSlot(int slot)
{
    char name[16];
    PresetStore::SlotName(slot, name);
    // Start from the defaults, keeping the global (instrument) settings.
    float p[hmmm::NUM_PARAMS];
    for(int i = 0; i < hmmm::NUM_PARAMS; i++)
        p[i] = hmmm::kParams[i].global ? engine.params[i] : hmmm::kParams[i].def;
    bool ok = sd_ok && presets.Load(name, p, nullptr, false);
    if(!ok)
    {
        const hmmm::FactoryPatch* f = hmmm::FactoryForSlot(slot + 1);
        if(!f)
            return false;
        hmmm::ApplyFactory(*f, p);
        ok = true;
    }
    for(int i = 0; i < hmmm::NUM_PARAMS; i++)
        engine.params[i] = p[i];
    return ok;
}

/** Channels: out[0..1] headphones, out[2..3] main out. */
void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size)
{
    // While booting: silence, and leave the controls to main(), which is
    // scanning them for the shipping-mode combo.
    if(!running)
    {
        for(size_t i = 0; i < size; i++)
            out[0][i] = out[1][i] = out[2][i] = out[3][i] = 0.f;
        return;
    }

    cpu.OnBlockStart();

    hw.ProcessAllControls();
    ui.Poll();

    midi_uart.Listen();
    while(midi_uart.HasEvents())
        HandleMidi(midi_uart.PopEvent());
    while(midi_usb.HasEvents())
        HandleMidi(midi_usb.PopEvent());

    engine.Process(out[0], out[1], size);
    for(size_t i = 0; i < size; i++)
    {
        out[2][i] = out[0][i];
        out[3][i] = out[1][i];
    }

    cpu.OnBlockEnd();
}

/** Startup: the eight voice keys swell in and fade out, 1-4 warm and 5-8
 *  cool, so you know which firmware you booted. */
static void BootAnimation()
{
    for(int step = 0; step < 48; step++)
    {
        const float t = step / 47.f;
        for(int v = 0; v < hmmm::kNumVoices; v++)
        {
            // Each key a little later than the one before, rise then fall.
            const float x = Clamp(t * 1.6f - v * 0.075f, 0.f, 1.f);
            const float b = sinf(x * hmmm::kPi);
            const float* c = hmmm::kGroupColour[v / 4];
            SetSmtLedFloat(kKeyLed[static_cast<int>(kWhiteKeys[v])], c[0] * b, c[1] * b, c[2] * b);
        }
        fill_led_data();
        System::Delay(12);
    }
    for(int i = 0; i < 25; i++)
        SetSmtLed(i, 0, 0, 0);
    fill_led_data();
}

int main(void)
{
    hw.Init();

    // Start the codecs' clocks straight away, playing silence, as the stock
    // firmwares do. A codec that is set up but left unclocked while we read
    // the card makes a loud noise. The engine is ready (and its delay
    // lines cleared) before the first block.
    engine.Init(hw.seed.AudioSampleRate(), delay_mem, kDelayFrames);
    hw.StartAudio(AudioCallback);

    hw.MpWrite(0x0c, 0B01010001); // BATT_LOW to 3 V
    hw.MpReadAll();
    for(size_t i = 0; i < 10; i++)
    {
        hw.LowBatteryLockoutCheck();
        System::Delay(10);
    }

    LedSetup();

    // SD card. Everything we keep lives in /HMMM, created if missing.
    System::Delay(100);
    SdmmcHandler::Config sd_cfg;
    sd_cfg.speed = SdmmcHandler::Speed::FAST;
    sd_cfg.width = SdmmcHandler::BusWidth::BITS_4;
    sdmmc.Init(sd_cfg);
    fsi.Init(FatFSInterface::Config::MEDIA_SD);
    sd_ok = f_mount(&fsi.GetSDFileSystem(), fsi.GetSDPath(), 1) == FR_OK;
    if(sd_ok)
    {
        if(f_chdir("/HMMM") != FR_OK)
        {
            // This firmware was called HUM through its first beta, and kept everything in
            // /HUM. Rename that folder rather than starting fresh, so a beta tester's saved
            // patches come across with the name. Only when /HMMM does not exist yet: never
            // clobber one that does.
            if(f_rename("/HUM", "/HMMM") != FR_OK)
                f_mkdir("/HMMM");
            f_chdir("/HMMM");
        }
        options.Init();
        midi_ch_in = options.midi_ch_in;
#if DIAG
        f_unlink("diag_prev.txt");
        f_rename("diag.txt", "diag_prev.txt");
#endif
    }

    // Which slots hold a patch, so CHOMPI can show them: any slot with a
    // file, plus every factory slot (built in, even with no file).
    uint16_t used = 0;
    for(int i = 0; i < hmmm::kNumFactoryPatches; i++)
        used |= 1u << (hmmm::kFactoryPatches[i].slot - 1);
    bool restored = false;
    if(sd_ok)
    {
        restored = presets.Load("current.txt", engine.params, &engine.volume, true);
        char name[16];
        for(int s = 0; s < 15; s++)
        {
            PresetStore::SlotName(s, name);
            if(f_stat(name, nullptr) == FR_OK)
                used |= 1u << s;
        }
    }
    ui.slots_used = used;

    // A brand-new card has no saved sound: start on the first factory
    // patch, which waits silently for a key.
    if(!restored)
    {
        if(!(sd_ok && LoadSlot(hmmm::kFirstBootSlot - 1)))
            hmmm::ApplyFactory(*hmmm::FactoryForSlot(hmmm::kFirstBootSlot), engine.params);
        ui.SetCurrentSlot(hmmm::kFirstBootSlot - 1);
    }
    ui.Init(&hw, &engine);

    MidiUartHandler::Config uart_cfg;
    midi_uart.Init(uart_cfg);
    midi_uart.StartReceive();
    MidiUsbHandler::Config usb_cfg;
    usb_cfg.transport_config.periph = MidiUsbTransport::Config::EXTERNAL;
    midi_usb.Init(usb_cfg);
    midi_usb.Listen();

    cpu.Init(hw.seed.AudioSampleRate(), hw.seed.AudioBlockSize());

    BootAnimation();

    // Boot combo from the stock firmwares: CHOMPI + PLAY + LOOP held at
    // power-on puts the battery chip in shipping mode (fully off).
    uint32_t ship = 0;
    for(int i = 0; i < 5000; i++)
    {
        hw.ProcessAllControls();
        ship += hw.button_sr.State(int(Hardware::SwId::KEY_26))
                && hw.button_sr.State(int(Hardware::SwId::KEY_27))
                && hw.button_sr.State(int(Hardware::SwId::KEY_28));
        System::DelayUs(100);
    }
    if(ship > 4000)
        hw.MpWrite(0x08, 0B10111111);

    ui.Init(&hw, &engine); // restarts its ignore-the-first-second timer
    running = true;

    hw.usb_sw.Write(false);       // give USB control
    System::Delay(1);
    hw.MpWrite(0x0a, 0B00100100); // AutoDPDM
    System::Delay(1);
    hw.usb_sw.Write(true);        // take USB control

    uint32_t last_draw = 0, last_batt = 0;
    char     fname[16];
#if DIAG
    DiagMainAdd(D_BOOT, 0, 0);
    uint32_t last_flush = System::GetNow(), last_iter = System::GetNow();
    uint32_t last_level = System::GetNow();
#endif

    while(1)
    {
        const uint32_t now = System::GetNow();
#if DIAG
        // A main loop that stops for long freezes the LEDs and the SD work.
        if(now - last_iter > 20)
            DiagMainAdd(D_STALL, now - last_iter > 255 ? 255 : now - last_iter, 0);
        if(sd_ok && now - last_flush > 2000)
        {
            DiagFlush();
            last_flush = System::GetNow();
        }
        // Once a second while something is sounding: the peak at each stage,
        // to find where clipping happens.
        if(now - last_level >= 1000)
        {
            last_level = now;
            const auto lv  = engine.TakeLevels();
            auto       pct = [](float v) { return static_cast<uint8_t>(v * 100.f > 255.f ? 255 : v * 100.f); };
            if(lv.voices > 0.02f)
            {
                DiagMainAdd(D_LEVEL, pct(lv.voices), pct(lv.delay_in));
                DiagMainAdd(D_LEVEL2, pct(lv.dist_out), pct(lv.limit_gain));
            }
            // The LED shows the average; dropouts come from the worst block.
            DiagMainAdd(D_CPU, pct(cpu.GetAvgCpuLoad()), pct(cpu.GetMaxCpuLoad()));
            cpu.Reset();
        }
        last_iter = System::GetNow();
#endif

        if(now - last_draw >= 16)
        {
            last_draw = now;
            ui.Draw(cpu.GetAvgCpuLoad());
        }

        // Loads work without a card too: factory slots are built in.
        const int load = ui.load_slot;
        if(load >= 0)
        {
            ui.load_slot = -1;
            const bool ok = LoadSlot(load);
            if(ok)
                ui.dirty = true; // new sound becomes current.txt too
#if DIAG
            DiagMainAdd(D_LOAD, load, ok);
#endif
        }

        if(sd_ok)
        {
            const int save = ui.save_slot;
            if(save >= 0)
            {
                ui.save_slot = -1;
                PresetStore::SlotName(save, fname);
                const bool ok = presets.Save(fname, engine.params, nullptr, false);
                if(ok)
                    ui.slots_used = ui.slots_used | (1u << save);
#if DIAG
                DiagMainAdd(D_SAVE, save, ok);
#else
                (void)ok;
#endif
            }

            // Remember the sound a few seconds after the last change.
            if(ui.dirty && now - ui.last_change > 3000)
            {
                ui.dirty = false;
                const bool ok = presets.Save("current.txt", engine.params, &engine.volume, true);
#if DIAG
                DiagMainAdd(D_SAVE, 99, ok);
#else
                (void)ok;
#endif
            }
        }

        if(now - last_batt > 20)
        {
            last_batt = now;
            hw.LowBatteryLockoutCheck();
        }
    }
}
