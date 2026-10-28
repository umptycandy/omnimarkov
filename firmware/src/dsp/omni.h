/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument: an OM-84 style chord harp. Three sections, as on the Omnichord:
 *
 *   HARP    16 strings, one per white key. Each plays a tone of the current chord: the tones
 *           folded into an F#..F window, root / third / fifth, five windows up, a root on top
 *           (the OM-84 / OM-108 strumplate, extended down one octave to fill 16 keys).
 *           Two voices, as the OM-84's: VOICE 1 shimmers (its tremolo), VOICE 2 is plain.
 *   CHORD   the chord itself (3 notes, F#3..F4) and its bass (C2..B2), held while a chord pad
 *           is down, or played in time by the rhythm (AUTO BASS SYNC).
 *   RHYTHM  ten OM-84 rhythms: bass drum, claves, hi-hat, cymbal, snare.
 *
 * Each section plays one of six voices (dsp/models.h): the OM-84's own, or Rings' and Plaits' string,
 * modal resonator, virtual analog, FM and wavefolder. The harp's are a pool of up to twelve voices
 * (eight to start with; the device's governor, main_fm1.c, sets how many the CPU affords).
 *
 * Two tracks share the clock: the DRUMS (the rhythm's drums) and the SEQUENCE (what plays the chord
 * and the bass in time: the rhythm's own patterns, a recorded loop, or the Markov generators, and the
 * Markov arp on the harp). Either starts and stops on its own; the clock runs while either does.
 *
 *   LOOP    REC records the chord changes, the chord's pattern and the bass, a step at a time, for 1..8
 *           bars, and the sequence then plays them back. A chord held over a playing loop moves the loop's
 *           bass to it.
 *   MARKOV  chord changes chosen from Markov chains over the chord buttons: a prior of the usual root
 *           motions (down a fifth, up a fifth, to the relative minor...) plus every change played by
 *           hand, learnt; a bass line (root, fifth, third, octave, the leading tone, the sixth / seventh)
 *           over the rhythm's bass; an arp that walks the strings. CHAOS flattens every chain.
 *
 * The waves, envelopes and rhythm patterns follow Chordian (Jan125/pb.chordian, CC0), an OM-84
 * emulator; dsp/om_data.h and dsp/om_drums.h are generated from it (tools/import_chordian.py).
 *
 * Threads: om_render() runs in the audio interrupt. Everything else posts commands (om_cmd), which
 * the render drains at the start of a block; the state below marked (UI) is written by the render
 * and only read elsewhere. Float only, no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define OM_SR 44100.0f
#define OM_NSTR 16                     /* harp strings: the white keys */
#define OM_NCHORD 3                    /* the OM's chords are triads */
#define OM_NPADS 11                    /* the chord buttons (the black keys) */
#define OM_LOOP_MAX 128                /* a loop's steps: 8 bars of 16 */
#define OM_HV_MAX 12                   /* harp voices (the Rings / Plaits models), at most */

/* chord types: the OM-108's nine (all triads: a 7th drops its fifth, as on the Omnichord) */
enum { CH_MAJ, CH_MIN, CH_7, CH_MAJ7, CH_M7, CH_DIM, CH_AUG, CH_SUS4, CH_ADD9, CH_NTYPES };
extern const char *const OM_TYPE_NAME[CH_NTYPES];      /* "", "m", "7", "M7", "m7", "dim", "aug", "sus4", "add9" */
extern const char *const OM_NOTE_NAME[12];             /* "C", "C#"... */
extern const int8_t OM_TYPE_TONES[CH_NTYPES][3];

/* parameters: value ranges in om_param_info() */
enum {
    P_VOICE1, P_VOICE2, P_SUSTAIN, P_CHORD,          /* harp voice levels, harp sustain, chord + bass level */
    P_RHYTHM, P_TEMPO, P_RHYVOL, P_BSRC,              /* rhythm, BPM, drums level, the bass's source (SRC_*;
                                                       * this slot was "Auto bass" before format 3) */
    P_REVERB, P_SPACE, P_TUNE, P_CREV,                /* the strings' reverb send, reverb size, tune (cents),
                                                       * the chord and bass's reverb send (the drums are dry) */
    P_TRANSPOSE, P_OCTAVE, P_WIDTH, P_MIDI,           /* semitones, harp octave, stereo width, MIDI out */
    P_HMODEL, P_HTIMBRE, P_HCOLOR, P_HLEVEL,          /* the harp's voice (M_*), its two knobs, its level */
    P_CMODEL, P_CTIMBRE, P_BMODEL, P_BTIMBRE,         /* the chord's and the bass's voices and tones */
    P_BLEVEL, P_BARS,                                 /* the bass's level; a recorded loop's length (1, 2, 4, 8 bars) */
    P_ARP, P_CHANGES, P_BVAR, P_CHAOS,                /* Markov: arp density, chord changes (off, every 1, 2, 4
                                                       * bars), the bass's variation, chaos (the chains' temperature) */
    P_NPARAMS
};
/* P_BSRC: what the sequence plays the bass and chord from */
enum { SRC_RHYTHM, SRC_LOOP, SRC_MARKOV, SRC_N };
/* the voices (dsp/models.h): their names, and the names of their two knobs */
enum { M_OM84, M_STRING, M_MODAL, M_ANALOG, M_FM, M_FOLD, OM_NMODELS };
extern const char *const OM_MODEL_NAME[OM_NMODELS];   /* "OM-84", "String", "Modal", "Analog", "FM", "Fold" */
const char *om_model_knob(int model, int which);        /* which 0: TIMBRE, 1: COLOR ("Bright", "Structure"...) */
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} om_param_t;
const om_param_t *om_param_info(int i);
void om_param_text(int i, int v, char *buf);   /* the value as the screen shows it (10 bytes) */

/* the rhythms (dsp/om_data.h): per step, bass and chord commands (T trigger, R release, O one-shot,
 * N cut, '-' none), the bass note (R root, 5 fifth, 3 third) and the drums (bit: BD CL HH CY SD) */
typedef struct {
    const char *name;
    uint8_t len, alt;
    const char *bass, *chord, *note;
    uint8_t drum[32];
} om_rhythm_t;
const om_rhythm_t *om_rhythm(int i);

/* commands (main loop -> render) */
void om_init(void);
/* the project's chord buttons, learnt changes ([from][to] counts) and loop (3 bytes a step, *loop_len steps),
 * which the engine reads (and, recording, writes) in place. Before the audio starts; unlinked, its own */
void om_link(const uint8_t *pad_root, const uint8_t *pad_type, const uint8_t *learn, uint8_t *loop, uint8_t *loop_len);
void om_set(int param, int value);
void om_chord(int root, int type);             /* the chord the harp, chord and bass play from now on */
void om_pad(int pad);                          /* ... and the button it came from (-1: not a button), for Markov */
void om_gate(int on);                          /* a chord pad held (1) / released (0): the chord section */
void om_strum(int string);                     /* pluck string 0..15 */
void om_strum_v(int string, int vel);          /* ... with a velocity, 1..127 */
void om_play(int on);                          /* the drums track: start / stop (the clock from the top if it was stopped) */
void om_seq(int on);                           /* the sequence track: start / stop, the same way */
enum { OM_REC_START, OM_REC_STOP, OM_REC_CLEAR };
void om_rec(int cmd);                          /* START: record a loop (now if stopped, else from the next bar);
                                                * STOP: keep what is recorded; CLEAR: no loop */
void om_bass_note(int note, int on);           /* a bass note by hand (MIDI in, channel 3) */
void om_poly(int voices);                      /* the harp's voices, 2..OM_HV_MAX (the CPU governor) */
void om_panic(void);                           /* everything quiet, now */
/* the probability of each chord button coming next after button `from` (the chord changes' chain) */
void om_markov_probs(int from, int chaos, float p[OM_NPADS]);
/* MIDI clock in (the main loop passes it on): OM_CLK_TICK (F8), START, CONTINUE, STOP. While ticks keep
 * coming (the last within ~0.6 s) they drive the rhythm, 6 a step, and its own tempo stands aside;
 * otherwise the rhythm sends clock (24 a quarter note) and start / stop, with MIDI out on. */
enum { OM_CLK_TICK, OM_CLK_START, OM_CLK_CONTINUE, OM_CLK_STOP };
void om_clock(int msg);

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void om_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* MIDI out, from the render: the app sends it (USB); status, data 1, data 2 */
void om_midi_out(uint32_t st, uint32_t d1, uint32_t d2);

/* state for the screen (UI) */
extern volatile float om_str_level[OM_NSTR];   /* each string's envelope, 0..1 */
extern volatile uint8_t om_str_note[OM_NSTR];  /* the note each string plucks, for the chord now (MIDI) */
extern volatile float om_chord_level, om_bass_level;
extern volatile uint8_t om_playing, om_step, om_steps;
extern volatile uint8_t om_drum_hit;           /* bits: drums hit since the UI last cleared it */
extern volatile uint8_t om_ext;                /* 1: following an external MIDI clock */
extern volatile uint8_t om_drums_on, om_seq_on; /* the tracks */
extern volatile uint8_t om_rec_state;          /* 0 no, 1 armed (waiting for the bar), 2 recording */
extern volatile uint8_t om_loop_pos;           /* the loop's step playing (or recording) */
extern volatile uint8_t om_loop_done;          /* counts loops recorded or cleared: the project changed */
extern volatile uint8_t om_bsrc;               /* P_BSRC as the engine has it (recording sets it to SRC_LOOP) */
extern volatile uint8_t om_root, om_type;      /* the chord sounding: a button's, the loop's or Markov's */
extern volatile int8_t om_gen_pad;             /* the button Markov chose, -1 none */
extern volatile uint8_t om_gen;                /* 1: the chord comes from the loop or Markov (no button held) */
extern volatile uint8_t om_poly_n;             /* the harp's voices now */
extern volatile uint8_t om_hv_active;          /* ... and how many of them sound */
/* the notes a chord gives: harp strings (16, with transpose and octave), chord (3) and bass root */
void om_voicing(int root, int type, int transpose, int octave, uint8_t harp[OM_NSTR], uint8_t chord[OM_NCHORD],
                uint8_t *bass);
