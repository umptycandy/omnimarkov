# FoMni for the M-VAVE FM-1

FoMni turns the M-VAVE FM-1 into a chord harp inspired by the Suzuki Omnichord. The 16 white
keys are the strum plate: run a finger across them and they ring out the chord. The 11 black keys
are the chord buttons. Behind them sit an organ-like chord with its bass, and the OM-84's ten
rhythms, which can play the bass and chord in time (auto bass sync).

It is meant to be simple: one screen, four knobs a page, no patterns to program.

This version adds:

- **New voices.** Five voices ported from Mutable Instruments' Rings and Plaits (MIT-licensed): String, Modal, Analog, FM and Fold.
- **A voice per section.** The harp, the chord and the bass each choose their own voice; the original OM-84 voice is still one of the choices.
- **Two tracks.** The drums and the sequence (bass and chord in time) start and stop separately.
- **Loops.** A bass and chord-progression loop recorder.
- **Markov generators.** Chord changes (learned from what you play), a bass line and an arp.

[![FoMni running on the FM-1 (watch on YouTube)](https://img.youtube.com/vi/pahaM6nzucE/maxresdefault.jpg)](https://www.youtube.com/watch?v=pahaM6nzucE)

![FoMni's screen: a C chord strummed](docs/img/screen-strum.png) ![A minor held](docs/img/screen-chord.png) ![The rhythm page](docs/img/screen-rhythm.png)

- **Try it in your browser** (no FM-1 needed): <https://charlesvestal.github.io/fm1-fomni/emu/>
- **Install it** (Chrome or Edge, with the FM-1 plugged in): <https://charlesvestal.github.io/fm1-fomni/install/>
- **Download the firmware file**: [releases](https://github.com/charlesvestal/fm1-fomni/releases)

Installing is at your own risk, though it's hard to get stuck: the web installer can put M-VAVE's
firmware back, and an FM-1 that keeps crashing starts in a safe mode.

## Playing

| | |
|---|---|
| White keys | The strum plate. Each key plucks one string of the chord. As on the Omnichord, each group of three keys is root, third and fifth, folded into an F#–F window, five groups up, with a root on top. |
| Black keys | Chord buttons (F C G, Dm Am, Em G7 E7, D7 Bb, A7 by default). Hold **LFO** for the 7th. |
| ENV | **Shift.** Hold it and turn KNOB 1–4 for the page's second layer (see below). ENV + PLAY: both tracks at once. ENV + SEQ: back to the top of the bar. ENV + REC: overdub the loop. ENV + a black key: pick that button on the Chords page without playing it. |
| PLAY | The **drums** track, on and off. |
| SEQ | The **sequence** track, on and off: the bass and chord in time (the rhythm's patterns, your loop, or Markov), the Markov chord changes and the arp. The clock runs while either track does; a track started while the other plays joins in time. |
| REC | Record a loop (see Loops below). Press again to stop early. Hold for a second to clear the loop. |
| ARP | CHORD HOLD: the chord keeps playing after you let go (press the same button again to stop it). |
| OCT− / OCT+ | Move the strum plate an octave. |
| SELECT | Tempo. |
| ALGORITHM | Rhythm. |
| PRESETS | Transpose (−6 to +6). |
| KNOB 1–4 | The four values on the screen (see the pages below). |
| SAVE | Saves. FoMni also saves by itself a few seconds after a change, once it's quiet. |

### The pages

Each page puts four values on KNOB 1–4. Each page button has two pages: press it again for the second.
EDIT steps through all eight.

- **HOME: Play.** The harp's voice (Model), its two knobs (named for the voice), Sustain.
  **HOME again: Mix.** Harp, Chord, Bass and Drums levels.
- **GLO: Rhythm.** Rhythm, Tempo, Bass (where the sequence gets its bass and chord: Rhythm, Loop or Markov), Bars (a loop's length: 1, 2, 4 or 8).
  **GLO again: Setup.** Tune, MIDI out, Lights (On: the key lights, and the unlit buttons glow dimly so they can be read; Keys: the key lights only; Off), Sync start (PLAY and SEQ wait for your first chord).
- **FX: Sound.** Reverb (the strings' send), Space (the plate's size), Width, Chord rev (the chord and bass's send). The drums stay dry.
  **FX again: Voices.** The chord's voice and its tone, the bass's voice and its tone.
- **SEL: Chords.** Change the chord on any black key: press the key, then turn Root (KNOB 1) and Type (KNOB 2). KNOB 3 and 4 are Transpose and Octave.
  **SEL again: Markov.** Arp, Changes, Bass var, Chaos (see below). Bars over the chord buttons show how likely each is to come next.

### Shift (ENV held)

| Page | KNOB 1–4 while ENV is held |
|---|---|
| Play | Velocity (the strum's strength), Position (where a Modal string is struck), LPG color (Analog, FM, Fold) |
| Rhythm | Swing |
| Markov | Arp range (how many strings the arp walks over), Arp rate (1/16, 1/8, 1/4), Learn (how much the changes you play count) |
| Voices | Chord color, Chord rel(ease), Bass color, Bass rel(ease) |

### The voices

| Voice | From | KNOB 2 / KNOB 3 on the Play page |
|---|---|---|
| OM-84 | the original: the Omnichord's harp, chord organ and bass | Voice 1 (shimmering) / Voice 2 (plain) |
| String | Plaits' and Rings' string: a plucked, Karplus-Strong-style string | Bright / Structure (below the middle: a sitar's buzzing bridge; above: a piano's stiffness) |
| Modal | Plaits' and Rings' modal resonator, struck | Bright / Structure (a string's harmonics to a bell's and a plate's partials) |
| Analog | Plaits' virtual analog, through its low-pass gate | Shape (saw, square, thin pulse) / Detune (a second saw) |
| FM | Plaits' two-operator FM | Index / Ratio (Plaits' ratios) |
| Fold | Plaits' wavefolder | Fold / Slope |

Sustain sets how long the strings ring, whatever the voice. Held chords and bass notes on String and
Modal are bowed (Plaits' continuous excitation) while held, and damped when let go.

The harp shares a pool of 8 to 12 voices (a strum keeps the newest strings ringing). On the FM-1, FoMni
watches how hard the audio is working and plays fewer harp voices when it nears its limit.

### Loops

REC records what the sequence plays: the chord changes (from the buttons, MIDI or Markov), the chord's
pattern and the bass. If nothing is playing, recording starts at once, with the sequence; otherwise it
starts on the next bar. After Bars bars (Rhythm page) it stops by itself, sets Bass to Loop, and the
sequence plays it back. While it plays, your hands are free for the strum plate: the harp follows the
loop's chords. Hold a chord button over a playing loop and the loop's bass moves to that chord.
The loop is saved with the rest. Hold REC for a second to clear it.

**Overdub** (ENV + REC, while a loop plays): one pass round the loop, recording over it. Hold a chord
button while it runs and that part of the loop takes the new chord, its bass moved with it. MIDI channel 3
notes are recorded too.

### Markov

- **Changes** (Off, every 1, 2 or 4 bars): with the sequence on and no chord button held, the next chord
  is chosen from a Markov chain over the chord buttons. It starts from the usual root moves (down a fifth,
  up a fifth, to the relative minor, a 7th resolving) and learns from you: every change you play by
  hand counts, and is saved. Press a button to take over; let go and Markov goes on from there.
- **Bass var:** with Bass set to Markov, the bass walks a chain of root, fifth, third, octave, leading
  tone and sixth (seventh) over the rhythm's bass, adding notes between them as you turn it up.
- **Arp:** with the sequence on, the harp plays itself: a walk over the strings, mostly a string or two at
  a time and mostly on in the direction it was going, on the beat more than off it.
- **Chaos:** flattens every chain, from the likeliest moves to anything goes.

### MIDI

MIDI comes in over USB and the TRS jack (the FM-1's jack is an input), and goes out over USB.

- **In, channel 1:** notes pluck the nearest string, at their velocity.
- **In, channel 2:** play a chord on a keyboard and it becomes the chord, as if you'd pressed a black key.
- **In, channel 3:** notes play the bass (and a loop being recorded records them).
- **In, clock:** the tracks follow MIDI clock; start starts both, stop stops both. The screen shows EXT for the tempo.
- **Out** (on by default, Setup page): the strings on channel 1, the chord on 2, the bass on 3, the drums
  on 10, and clock with start / stop when the rhythm runs on its own tempo.

The drums on channel 10:

| Sound | Note |
|---|---|
| Bass drum | 36 |
| Snare | 38 |
| Closed hat | 42 |
| Ride | 51 |
| Claves | 75 |

### Screen reader

FoMni-1 tells the [FM-1 Screen Reader](https://charlesvestal.github.io/fm1-reader/) what its screen shows,
over USB: the page and its knobs, a knob's value as it turns, the chord you play, play and stop, and what
each button did. Open the page in Chrome with the FM-1 plugged in; it speaks with its own voice, or hands
the text to your computer's screen reader.

## Building and testing

The toolchain is the same as X0X's (see [BUILDING.md](BUILDING.md)):

```
tests/run_tests.sh      # the instrument, the platform pieces, the app in a simulator
./build.sh              # build/omni.fwsc
python3 tools/fm1_install.py build/omni.fwsc     # install it over USB
```

`build/host/omni_host SCRIPT OUTDIR` runs the whole app on a computer from a script
(`tests/scenarios/*.omni`), with audio and screenshots coming out. `web/emu/build.sh` builds the
same code for the browser.

## Credits

The String, Modal, Analog, FM and Fold voices are C ports of [Mutable Instruments](https://github.com/pichenettes/eurorack)'
Plaits and Rings by Emilie Gillet (MIT). The rhythms, waves, drum sounds and envelope shapes come from
[Chordian](https://github.com/Jan125/pb.chordian), Jan125's OM-84 emulator (CC0). The platform
(hardware layer, USB, update loader, storage, installer) is
[Felucca](https://github.com/hugelton/Felucca)'s by Leo Kuroshita (Hügelton Instruments), by way of
[X0X](https://github.com/charlesvestal/fm1-x0x). GPL-3.0-only; see [LICENSING.md](LICENSING.md).

Omnichord is a trademark of Suzuki. FoMni isn't affiliated with or endorsed by Suzuki, M-VAVE or
Hügelton Instruments.
