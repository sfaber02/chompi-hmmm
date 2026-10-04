# HMMM

A drone machine for the [CHOMPI](https://github.com/sfaber02/CHOMPI), after the SOMA Lyra-8.

It has eight voices. Each is a triangle that SHARP bends towards a square.
- **Pairs and groups:** the voices are tuned by ear, and arranged in four pairs (12 34 56 78) and two groups (1234 5678).
- **FM:** pairs can frequency-modulate each other in a ring, or be modulated by the Hyper LFO. With TOTAL FB on, the instrument's own output modulates them.
- **Signal chain:** everything runs into a two-line modulated delay that can sing by itself, then a fuzz.

> Beta. Download from [Releases](https://github.com/sfaber02/chompi-hmmm/releases/tag/beta).

## Controls

**CHOMPI is shift.**

| Do | Does |
|---|---|
| White keys 1–8 | The eight voices: hold to sound |
| LOOP | Latch: tap a voice on, tap it off |
| PLAY | TOTAL FB on/off |
| Toggle switch | FM ring: up = one big loop, down = two loops (12↔34, 56↔78) |
| Big purple knob | Delay feedback (CHOMPI: delay mix). It turns white once the delay sings by itself |
| Volume knob | Volume (CHOMPI: distortion drive). Click: everything off |
| CHOMPI + black key | Page |
| CHOMPI + white key | Load patch 1–15. Hold 1 s to save |
| CHOMPI + PLAY / LOOP | Octave down / up |
| CHOMPI + click a knob | Reset it |

### Pages

*Italics* = CHOMPI + turn.

| # | Page | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
|---|---|---|---|---|---|
| 1 | **TUNE 1–4** | voice 1 | voice 2 | voice 3 | voice 4 |
| 2 | **TUNE 5–8** | voice 5 | voice 6 | voice 7 | voice 8 |
| 3 | **SHARP** | pair 12<br>*FAST 12* | pair 34<br>*FAST 34* | pair 56<br>*FAST 56* | pair 78<br>*FAST 78* |
| 4 | **MOD** | depth 12<br>*source 12* | depth 34<br>*source 34* | depth 56<br>*source 56* | depth 78<br>*source 78* |
| 5 | **GROUPS** | HOLD 1234<br>*vibrato* | HOLD 5678 | PITCH 1234 | PITCH 5678 |
| 6 | **HYPER LFO** | freq A | freq B | OR / AND | LINK |
| 7 | **DELAY** | mix<br>*mod 1* | time 1<br>*mod 2* | time 2<br>*SELF / LFO* | feedback<br>*TRI / SQUARE* |
| 8 | **DISTORTION** | drive | mix | | |
| 9 | **VOICE** | attack | release | character | stereo spread |
| 10 | **TUNE** | octave | transpose | fine | MIDI bend range |

Notes on the pages:
- **Voice TUNE knobs:** a plain turn moves 2 cents a click, and fast turns move further. CHOMPI + turn moves in whole semitones. Each voice covers ±1 octave around its home note: voices 1–2 low, 3–6 middle, 7–8 high.
- **MOD source:** off / FM (the neighbouring pair in the ring) / LFO (the Hyper LFO, or the whole output when TOTAL FB is on).

## Building

```
cd code/src
PATH=/path/to/gcc-arm-none-eabi-10.3-2021.10/bin:$PATH make
```

`build/CHOMPI.bin` ships as `HMMM.bin`.

Desktop harness: `make -C host && host/render host/out`.

## Credits

By hiwatts ([@sfaber02](https://github.com/sfaber02)). The hardware layer comes from CHOMPI Club's open-source firmware (MIT); see `LICENSE.chompi-club`. Not affiliated with SOMA Laboratory: HMMM is an homage, not a clone of their circuit.
