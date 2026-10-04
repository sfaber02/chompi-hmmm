HUM - a drone machine for the CHOMPI (beta)
===========================================

Eight voices inspired by the SOMA Lyra-8. They modulate each other,
run into a delay that can sing by itself, and end in a fuzz.

INSTALL
  On its own: copy HUM.bin to the ROOT of the card, as the only .bin
  there (remove CHOMPI.bin or any other). Power on. The rainbow shows
  while it installs.
  On the multi-firmware launcher: copy HUM.bin into /FIRMWARE. It
  goes on the first free key. 06_HUM.bin puts it on key 6.

  HUM makes a /HUM folder for your patches.

THE ONE RULE: the CHOMPI key (red, top left) is SHIFT.

PLAY
  White keys 1-8 .... the eight voices: hold to sound
                      (1-2 low, 3-6 middle, 7-8 high)
  LOOP .............. latch: tap a voice on, tap it off
  PLAY .............. TOTAL FB: the output modulates itself
                      (on pairs whose MOD source is LFO)
  Toggle switch ..... FM ring: up = one big loop, down = two
  Purple dial ....... delay feedback. Turns white when the delay
                      sings by itself. Shift: delay mix
  Volume knob ....... volume. Shift: distortion drive.
                      Click: everything off
  Shift + white key . load patch 1-15. Hold 1 s to save
  Shift + click knob  reset it

PAGES: shift + black key 1-10, then knobs 1-4 (shift + knob = 2nd row)
  1 TUNE 1-4 .. tune voices 1-4 (shift: by semitone)
  2 TUNE 5-8 .. tune voices 5-8
  3 SHARP ..... triangle -> square per pair 12/34/56/78 (shift: FAST)
  4 MOD ....... FM amount per pair (shift: source off / FM / LFO)
  5 GROUPS .... HOLD 1-4, HOLD 5-8, PITCH 1-4, PITCH 5-8 (shift+1: vibrato)
                HOLD makes a group drone by itself, no keys
  6 HYPER LFO . speed A, speed B, OR/AND, LINK
  7 DELAY ..... mix, time 1, time 2, feedback
                (shift: mod 1, mod 2, SELF/LFO, TRI/SQUARE)
  8 DISTORTION  drive, mix
  9 VOICE ..... attack, release, character, stereo spread
  10 TUNE ..... octave, transpose, fine, MIDI bend range

PATCHES (shift + white key)
  1 organ  2 fm loop  3 lfo pulse  4 delay drone  5 chaos
  6 hold drone (quiet, see below)  15 init

KNOWN ISSUES
  - HOLD is quiet: turn it most of the way up. Patch 6 drones on its own
    but is soft and low on the CHOMPI speaker.
  - MIDI in (notes C3-C4 play voices 1-8, CCs) is untested.

Beta, tested on one CHOMPI. Back up your card. By hiwatts (@sfaber02).
Built on CHOMPI Club's open-source firmware (MIT). An homage to the
Lyra-8, not affiliated with SOMA Laboratory.
