/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument: an OM-84 style chord harp. Three sections, as on the Omnichord:
 *
 *   HARP    16 strings, one per white key. Each plays a tone of the current chord: the tones
 *           folded into an F#..F window, root / third / fifth, five windows up, a root on top
 *           (the OM-84 / OM-108 strumplate, extended down one octave to fill 16 keys).
 *           Two voices, as the OM-84's: VOICE 1 shimmers (its tremolo), VOICE 2 is plain.
 *   CHORD   the chord itself (3 notes, F#3..F4), held: while a chord pad is down, or while the
 *           sequence holds it (a loop). No bass, no melody: the chord, held.
 *   RHYTHM  ten OM-84 rhythms' drums: bass drum, claves, hi-hat, cymbal, snare.
 *
 * The harp and the chord each play one of six voices (dsp/models.h): the OM-84's own, or Rings' and
 * Plaits' string, modal resonator, virtual analog, FM and wavefolder. The harp's are a pool of up to
 * twelve voices (eight to start with; the device's governor, main_fm1.c, sets how many the CPU affords).
 *
 * Two tracks share the clock: the DRUMS and the SEQUENCE (a recorded loop of chords, held in time).
 * Either starts and stops on its own; the clock runs while either does.
 *
 *   LOOP    REC counts in four beats (a click of its own), then records the chord changes, a step at a
 *           time, for 1..8 bars; the sequence then holds them in turn. A chord held over the loop sounds
 *           instead; overdubbing, it replaces the loop's.
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

/* parameters: value ranges in om_param_info(). 0..15 as in formats 1 and 2 (7 was Auto bass) */
enum {
    P_VOICE1, P_VOICE2, P_SUSTAIN, P_CHORD,          /* OM-84 harp voice levels, harp sustain, chord level */
    P_RHYTHM, P_TEMPO, P_RHYVOL, P_SWING,             /* rhythm, BPM, drums level, swing (%) */
    P_REVERB, P_SPACE, P_TUNE, P_CREV,                /* the strings' reverb send, reverb size, tune (cents),
                                                       * the chord's reverb send (the drums are dry) */
    P_TRANSPOSE, P_OCTAVE, P_WIDTH, P_MIDI,           /* semitones, harp octave, stereo width, MIDI out */
    P_HMODEL, P_HTIMBRE, P_HCOLOR, P_HLEVEL,          /* the harp's voice (M_*), its two knobs, its level */
    P_CMODEL, P_CTIMBRE, P_CCOLOR, P_CREL,            /* the chord's voice, its two knobs, its release */
    P_BARS,                                           /* a loop's length (1, 2, 4, 8 bars) */
    /* the SHIFT layer (ENV held): */
    P_HVEL, P_HPOS, P_LPGC,                           /* the harp's velocity, strike position (Modal), the LPG's colour */
    P_NPARAMS
};
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
/* the project's loop (a byte a step, *loop_len steps), which the engine plays (and, recording, writes) in
 * place. Before the audio starts; unlinked, its own */
void om_link(uint8_t *loop, uint8_t *loop_len);
void om_set(int param, int value);
void om_chord(int root, int type);             /* the chord the harp, chord and bass play from now on */
void om_gate(int on);                          /* a chord pad held (1) / released (0): the chord section */
void om_strum(int string);                     /* pluck string 0..15 */
void om_strum_v(int string, int vel);          /* ... with a velocity, 1..127 */
void om_play(int on);                          /* the drums track: start / stop (the clock from the top if it was stopped) */
void om_seq(int on);                           /* the sequence track: start / stop, the same way */
enum { OM_REC_START, OM_REC_STOP, OM_REC_CLEAR, OM_REC_OVERDUB };
void om_rec(int cmd);                          /* START: four beats counted in (a click), then record a loop (stopped:
                                                * the count from now, the loop from the top; playing: the count from
                                                * the next bar, the loop from the bar after it);
                                                * STOP: keep what is recorded; CLEAR: no loop; OVERDUB: once round
                                                * the playing loop, recording over it (a chord held rewrites it) */
void om_restart(void);                         /* the clock back to the top (the tracks keep playing) */
void om_poly(int voices);                      /* the harp's voices, 2..OM_HV_MAX (the CPU governor) */
void om_panic(void);                           /* everything quiet, now */
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
extern volatile float om_chord_level;
extern volatile uint8_t om_playing, om_step, om_steps;
extern volatile uint8_t om_drum_hit;           /* bits: drums hit since the UI last cleared it */
extern volatile uint8_t om_ext;                /* 1: following an external MIDI clock */
extern volatile uint8_t om_drums_on, om_seq_on; /* the tracks */
extern volatile uint8_t om_rec_state;          /* 0 no, 1 armed (waiting for the bar), 2 recording, 3 overdubbing,
                                                * 4 counting in */
extern volatile uint8_t om_count;              /* counting in: the beat, 1..4 */
extern volatile uint8_t om_loop_pos;           /* the loop's step playing (or recording) */
extern volatile uint8_t om_loop_done;          /* counts loops recorded or cleared: the project changed */
extern volatile uint8_t om_root, om_type;      /* the chord sounding: a button's, the loop's or Markov's */
extern volatile uint8_t om_gen;                /* 1: the chord comes from the loop (no button held) */
extern volatile uint8_t om_poly_n;             /* the harp's voices now */
extern volatile uint8_t om_hv_active;          /* ... and how many of them sound */
/* the notes a chord gives: harp strings (16, with transpose and octave), chord (3) and bass root */
void om_voicing(int root, int type, int transpose, int octave, uint8_t harp[OM_NSTR], uint8_t chord[OM_NCHORD],
                uint8_t *bass);
