/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument (omni.h). One compilation unit, -O2. Waves, envelope shapes, voicing and rhythms
 * after Chordian (Jan125/pb.chordian, CC0 1.0); its rates were written for 96 kHz and are converted
 * here where they are per sample (the filters), not where they are per second (the envelopes). The
 * other voices are Rings' and Plaits' (dsp/models.h). */
#ifdef OM_HOST
#define OM_POOL
#else
#define OM_POOL __attribute__((section(".pool")))
#endif
#define MV_POOL OM_POOL
#include "omni.h"
#include "fastmath.h"
#include "models.h"
#include "om_data.h"
#include "om_drums.h"
#define BARRIER() __asm__ volatile("" ::: "memory")

const char *const OM_TYPE_NAME[CH_NTYPES] = {"", "m", "7", "M7", "m7", "dim", "aug", "sus4", "add9"};
const char *const OM_NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
/* the three tones (semitones over the root); the third tone of a 7th is the 7th, as on the OM */
const int8_t OM_TYPE_TONES[CH_NTYPES][3] = {
    {0, 4, 7}, {0, 3, 7}, {0, 4, 10}, {0, 4, 11}, {0, 3, 10}, {0, 3, 6}, {0, 4, 8}, {0, 5, 7}, {0, 4, 2}};
const char *const OM_MODEL_NAME[OM_NMODELS] = {"OM-84", "String", "Modal", "Analog", "FM", "Fold"};

static const om_param_t PARAMS[P_NPARAMS] = {
    {"Voice 1", 0, 100, 80}, {"Voice 2", 0, 100, 45}, {"Sustain", 0, 100, 50}, {"Chord", 0, 100, 60},
    {"Rhythm", 0, OM_NRHYTHM - 1, 0}, {"Tempo", 60, 240, 116}, {"Drums", 0, 100, 70}, {"Bass", 0, SRC_N - 1, SRC_RHYTHM},
    {"Reverb", 0, 100, 30}, {"Space", 0, 100, 55}, {"Tune", -50, 50, 0}, {"Chord rev", 0, 100, 25},
    {"Transpose", -6, 6, 0}, {"Octave", -1, 1, 0}, {"Width", 0, 100, 60}, {"MIDI out", 0, 1, 1},
    {"Model", 0, OM_NMODELS - 1, M_STRING}, {"Timbre", 0, 100, 50}, {"Color", 0, 100, 40}, {"Harp", 0, 100, 80},
    {"Chord", 0, OM_NMODELS - 1, M_MODAL}, {"Tone", 0, 100, 50}, {"Bass", 0, OM_NMODELS - 1, M_ANALOG},
    {"Tone", 0, 100, 35}, {"Bass", 0, 100, 60}, {"Bars", 0, 3, 1},
    {"Arp", 0, 100, 0}, {"Changes", 0, 3, 0}, {"Bass var", 0, 100, 40}, {"Chaos", 0, 100, 30},
};

const om_rhythm_t *om_rhythm(int i) { return &OM_RHYTHM[(unsigned)i < OM_NRHYTHM ? i : 0]; }

const om_param_t *om_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }

const char *om_model_knob(int model, int which)
{
    static const char *const K[OM_NMODELS][2] = {{"Voice 1", "Voice 2"}, {"Bright", "Structure"}, {"Bright", "Structure"},
                                                 {"Shape", "Detune"},     {"Index", "Ratio"},       {"Fold", "Slope"}};
    return K[(unsigned)model < OM_NMODELS ? model : 0][which & 1];
}

static void itoa_s(int v, char *b)
{
    char t[8];
    int n = 0, neg = v < 0;
    if (neg)
        v = -v;
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 6);
    if (neg)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

void om_param_text(int i, int v, char *b)
{
    static const char *const ONOFF[2] = {"Off", "On"};
    static const char *const SRC[SRC_N] = {"Rhythm", "Loop", "Markov"};
    static const char *const CHG[4] = {"Off", "1 bar", "2 bars", "4 bars"};
    static const char *const BARS[4] = {"1 bar", "2 bars", "4 bars", "8 bars"};
    const char *s = 0;
    if (i == P_RHYTHM)
        s = OM_RHYTHM[v < 0 ? 0 : v % OM_NRHYTHM].name;
    else if (i == P_MIDI)
        s = ONOFF[v ? 1 : 0];
    else if (i == P_BSRC)
        s = SRC[(unsigned)v < SRC_N ? v : 0];
    else if (i == P_HMODEL || i == P_CMODEL || i == P_BMODEL)
        s = OM_MODEL_NAME[(unsigned)v < OM_NMODELS ? v : 0];
    else if (i == P_CHANGES)
        s = CHG[v & 3];
    else if (i == P_BARS)
        s = BARS[v & 3];
    else if (i == P_ARP && v == 0)
        s = "Off";
    if (s) {
        int k = 0;
        while (s[k] && k < 9) {
            b[k] = s[k];
            k++;
        }
        b[k] = 0;
        return;
    }
    if ((i == P_TRANSPOSE || i == P_OCTAVE || i == P_TUNE) && v > 0) {
        *b++ = '+';
    }
    itoa_s(v, b);
}

/* ------------------------------------------------------------- voicing --- */
static int fold(int pc, int lo) { return lo + (((pc - lo) % 12) + 12) % 12; }

void om_voicing(int root, int type, int transpose, int octave, uint8_t harp[OM_NSTR], uint8_t chord[OM_NCHORD],
                uint8_t *bass)
{
    const int8_t *t = OM_TYPE_TONES[(unsigned)type < CH_NTYPES ? type : 0];
    int r = root + transpose, k, i;
    for (k = 0; k < 5; k++)                       /* windows F#2..F3, F#3..F4, ...: root, 2nd, 3rd tone */
        for (i = 0; i < 3; i++)
            harp[3 * k + i] = (uint8_t)fold(r + t[i], 42 + 12 * k + 12 * octave);
    harp[15] = (uint8_t)fold(r, 42 + 60 + 12 * octave);
    for (i = 0; i < 3; i++)
        chord[i] = (uint8_t)fold(r + t[i], 54);  /* F#3..F4, the OM's chord register */
    *bass = (uint8_t)fold(r, 36);                 /* C2..B2 */
}

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_CHORD, C_GATE, C_STRUM, C_PLAY, C_PANIC, C_CLOCK, C_SEQ, C_REC, C_PAD, C_BNOTE, C_POLY };
#define QN 64u
static uint32_t q[QN];
static volatile uint32_t q_w, q_r;
static void post(uint32_t c, int a, int b)
{
    uint32_t w = q_w;
    if (w - q_r >= QN)
        return;                                   /* full: dropped (never, at the UI's rate) */
    q[w % QN] = c << 24 | ((uint32_t)a & 0xFFu) << 16 | ((uint32_t)b & 0xFFFFu);
    BARRIER();
    q_w = w + 1;
}
void om_set(int p, int v) { post(C_SET, p, v); }
void om_chord(int root, int type) { post(C_CHORD, root, type); }
void om_pad(int pad) { post(C_PAD, pad, 0); }
void om_gate(int on) { post(C_GATE, on, 0); }
void om_strum(int s) { post(C_STRUM, s, 0); }
void om_strum_v(int s, int vel) { post(C_STRUM, s, vel); }
void om_play(int on) { post(C_PLAY, on, 0); }
void om_seq(int on) { post(C_SEQ, on, 0); }
void om_rec(int cmd) { post(C_REC, cmd, 0); }
void om_bass_note(int note, int on) { post(C_BNOTE, note, on); }
void om_poly(int n) { post(C_POLY, n, 0); }
void om_panic(void) { post(C_PANIC, 0, 0); }
void om_clock(int msg) { post(C_CLOCK, msg, 0); }

/* -------------------------------------------------------------- state --- */
volatile float om_str_level[OM_NSTR];
volatile uint8_t om_str_note[OM_NSTR];        /* the note a pluck plays now */
volatile float om_chord_level, om_bass_level;
volatile uint8_t om_playing, om_step, om_steps = 32;
volatile uint8_t om_drum_hit;
volatile uint8_t om_ext;
volatile uint8_t om_drums_on, om_seq_on, om_rec_state, om_loop_pos, om_loop_done, om_bsrc;
volatile uint8_t om_root, om_type, om_gen, om_poly_n, om_hv_active;
volatile int8_t om_gen_pad = -1;

static int16_t par[P_NPARAMS];
static float tunefac = 1.0f, v1, v2, cvol, bvol, hvol, rvol, rsend, csend, sus_rate;
static float pan_l[OM_NSTR], pan_r[OM_NSTR];
static int root, type;                         /* the chord sounding */
static uint8_t harp_n[OM_NSTR], chord_n[OM_NCHORD], bass_n;
static int gate;                               /* a chord is held: by a button, by the loop or by Markov */

/* the chord by hand (a button, MIDI), the loop's, Markov's: chord_eval() picks the one that sounds */
static int live_root, live_type, live_gate, live_pad = 1;
static int lp_root = -1, lp_type, lp_gate;
static int gen_pad = -1;

/* the project's, linked (om_link); the engine's own until then */
static uint8_t own_root[OM_NPADS] = {5, 0, 7, 2, 9, 4, 7, 4, 2, 10, 9}, own_type[OM_NPADS] = {0, 0, 0, 1, 1, 1, 2, 2, 2, 0, 2};
static uint8_t own_learn[OM_NPADS * OM_NPADS], own_loop[OM_LOOP_MAX * 3], own_len;
static const uint8_t *pad_root = own_root, *pad_type = own_type, *learn = own_learn;
static uint8_t *loop = own_loop, *loop_len = &own_len;

void om_link(const uint8_t *pr, const uint8_t *pt, const uint8_t *lr, uint8_t *lp, uint8_t *ll)
{
    pad_root = pr;
    pad_type = pt;
    learn = lr;
    loop = lp;
    loop_len = ll;
}

static float sin3[360];                       /* Chordian's shimmer shape: |sin|^0.6 */
static float shim_ph, shim_rate;
/* the shimmer's rate follows the chord's root (Chordian, Hz, C..B) */
static const float SHIM_HZ[12] = {3.761f, 2.910f, 4.275f, 4.565f, 5.050f, 6.333f, 2.843f, 3.125f, 3.162f, 3.419f,
                                  3.667f, 3.953f};

static float note_hz(int n) { return 440.0f * tunefac * fm_exp2f((float)(n - 69) * (1.0f / 12.0f)); }
static float note_inc(int n) { return note_hz(n) * (100.0f / OM_SR); }   /* wave positions (100 per cycle) per sample */
static float note_f0(int n) { return fm_minf(note_hz(n) * (1.0f / OM_SR), 0.2f); }   /* cycles per sample */

static inline float wave(const float *w, float pos)
{
    int i = (int)pos;
    float f = pos - (float)i, a = w[i], b = w[i + 1 < 100 ? i + 1 : 0];
    return a + (b - a) * f;
}
static inline float adv(float pos, float inc)
{
    pos += inc;
    while (pos >= 100.0f)
        pos -= 100.0f;
    return pos;
}

static void midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (par[P_MIDI])
        om_midi_out(st, d1, d2);
}

/* ---- the Rings / Plaits voices: the harp's pool, then the three chord voices and the bass */
#define NV (OM_HV_MAX + OM_NCHORD + 1)
#define V_CHORD OM_HV_MAX
#define V_BASS (OM_HV_MAX + OM_NCHORD)
static mv_t mv[NV] OM_POOL;
static float mv_dl[NV][MV_DL] OM_POOL, mv_st[NV][MV_ST] OM_POOL;
static int8_t hv_str[OM_HV_MAX];               /* the string a harp voice plays (-1: let go, ringing out) */
static uint8_t hv_note[OM_HV_MAX], hv_pan[OM_HV_MAX];
static uint32_t hv_age[OM_HV_MAX], hv_clock;
static int hv_n = 8;                            /* the harp's polyphony (om_poly) */
static uint8_t ver_h, ver_c, ver_b;             /* the knobs' versions: voices update when they differ */
/* each model's level against the OM-84's (measured: a strum, a held chord) */
static const float G_HARP[OM_NMODELS] = {1.0f, 1.25f, 0.75f, 0.95f, 0.85f, 1.15f};
static const float G_HELD[OM_NMODELS] = {1.0f, 0.55f, 0.45f, 0.75f, 0.75f, 0.9f};
/* the chord's and the bass's COLOR (they have one knob): consonant settings */
static const float C_HELD[OM_NMODELS] = {0.0f, 0.27f, 0.26f, 0.3f, 0.18f, 0.5f};
static float h_timbre, h_color, h_decay;        /* the harp's knobs, 0..1 */

static void harp_par(mv_par_t *p)
{
    p->timbre = h_timbre;
    p->color = h_color;
    p->decay = h_decay;
}
static void held_par(mv_t *v, int bass, int released)
{
    const int m = v->model;
    v->p.timbre = (float)par[bass ? P_BTIMBRE : P_CTIMBRE] * 0.01f;
    v->p.color = C_HELD[m];
    /* the physical models ring while held and are damped when let go; the LPG's decay is its release */
    v->p.decay = (m == M_STRING || m == M_MODAL) ? (released ? (bass ? 0.18f : 0.3f) : (bass ? 0.5f : 0.62f)) : 0.32f;
}

/* ---- harp strings: the OM-84's (one per white key), or a voice from the pool */
typedef struct {
    float pos, inc, v, g, y1, y2, k, c, m, modamt;
    uint16_t shim_off;
    uint8_t on, note;
} str_t;
static str_t str[OM_NSTR];
static uint8_t str_midi[OM_NSTR];               /* the note on for a string (MIDI out), 0 none */

static void str_midi_on(int s, int n, int vel)
{
    if (str_midi[s])
        midi(0x80, str_midi[s], 0);
    midi(0x90, (uint32_t)n, (uint32_t)vel);
    str_midi[s] = (uint8_t)n;
}
static void str_midi_off(int s, int n)
{
    if (str_midi[s] == n) {
        midi(0x80, (uint32_t)n, 0);
        str_midi[s] = 0;
    }
}

static void strum_om(int s)
{
    str_t *v = &str[s];
    int n = harp_n[s];
    float u = fm_exp2f((float)(n - 69) * (1.0f / 12.0f)) * tunefac, base;
    if (!v->on) {
        v->y1 = v->y2 = 0.0f;
        v->g = 0.0f;
    }
    v->on = 1;
    v->note = (uint8_t)n;
    v->inc = note_inc(n);
    v->v = 1.0f;
    /* the voice filter: Chordian's per-note brightness, darker as the note decays */
    base = fm_maxf(((18.0f - u) / 18.0f) * 0.31f + 0.34f, 0.05f);
    v->k = fm_powf(base, 2.85f);
    {   /* the shimmer: less on the higher strings; each string its own phase (Chordian's offsets) */
        int i = s * 12 / 15;
        v->modamt = 1.0f - (float)i / 17.0f;
        v->shim_off = (uint16_t)(((i & 1) ? 27 * i : 14 * i) % 360);
    }
}

static void strum_mv(int s, float acc)
{
    int n = harp_n[s], i, pick = -1, model = par[P_HMODEL];
    float best = 9.0f;
    mv_t *v;
    for (i = 0; i < hv_n; i++)                      /* the string's own voice, ringing its note: struck again */
        if (hv_str[i] == s) {
            if (mv[i].on && hv_note[i] == n && mv[i].model == model)
                pick = i;
            else
                hv_str[i] = -1;                     /* another note: let it ring out, a new voice plays */
        }
    if (pick < 0)
        for (i = 0; i < hv_n; i++)
            if (!mv[i].on) {
                pick = i;
                break;
            }
    if (pick < 0)                                   /* all busy: the quietest goes (a let-go one first) */
        for (i = 0; i < hv_n; i++) {
            float l = mv[i].level + (hv_str[i] >= 0 ? 0.5f : 0.0f) + (float)(hv_clock - hv_age[i] < 2000u) * 0.25f;
            if (l < best) {
                best = l;
                pick = i;
            }
        }
    v = &mv[pick];
    if (!(v->on && hv_str[pick] == s)) {
        if (v->on && hv_str[pick] >= 0)
            str_midi_off(hv_str[pick], hv_note[pick]);
        mv_reset(v, model);
    }
    hv_str[pick] = (int8_t)s;
    hv_note[pick] = (uint8_t)n;
    hv_pan[pick] = (uint8_t)s;
    hv_age[pick] = hv_clock;
    v->p.f0 = note_f0(n);
    v->p.accent = acc;
    harp_par(&v->p);
    mv_coef(v, 0, 12);
    v->ver = ver_h;
    mv_strike(v);
}

static void strum(int s, int vel)
{
    int n = harp_n[s];
    float acc = vel > 0 ? (float)vel * (1.0f / 127.0f) : 0.8f;
    str_midi_on(s, n, vel > 0 ? vel : 100);
    if (par[P_HMODEL] == M_OM84)
        strum_om(s);
    else
        strum_mv(s, acc);
}

/* ---- chord and bass */
enum { E_OFF, E_DEC, E_SUS, E_REL, E_BDEC, E_CUT };
typedef struct {
    float pos, inc, v, g, cut;
    uint8_t st, note, model;
} cv_t;
static cv_t chv[OM_NCHORD], bsv;
static uint8_t bass_kind;                      /* 0 root, 1 fifth, 2 third */

/* a held voice on its note: the OM-84's wave, or a model's voice struck */
static void held_on(cv_t *v, mv_t *m, int bass, int note, float acc)
{
    int model = par[bass ? P_BMODEL : P_CMODEL];
    v->note = (uint8_t)note;
    v->inc = note_inc(note);
    v->v = 1.0f;
    v->cut = 1.0f;
    v->model = (uint8_t)model;
    if (model == M_OM84)
        return;
    if (m->model != model || !m->on)
        mv_reset(m, model);
    m->p.f0 = note_f0(note);
    m->p.accent = acc;
    held_par(m, bass, 0);
    mv_coef(m, !bass, bass ? 12 : 8);
    m->ver = bass ? ver_b : ver_c;
    mv_strike(m);
}
static void held_retune(cv_t *v, mv_t *m, int note)
{
    v->note = (uint8_t)note;
    v->inc = note_inc(note);
    if (v->model != M_OM84 && m->on) {
        m->p.f0 = note_f0(note);
        mv_coef(m, m != &mv[V_BASS], m != &mv[V_BASS] ? 8 : 12);
    }
}

static void chord_cmd(char c)
{
    int i;
    if (c == 'T' || c == 'O') {
        for (i = 0; i < OM_NCHORD; i++) {
            if (chv[i].st != E_OFF)
                midi(0x81, chv[i].note, 0);
            held_on(&chv[i], &mv[V_CHORD + i], 0, chord_n[i], 0.6f);
            chv[i].st = c == 'T' ? E_DEC : E_REL;
            midi(0x91, chord_n[i], 90);
        }
    } else if (c == 'R' || c == 'N') {
        for (i = 0; i < OM_NCHORD; i++)
            if (chv[i].st != E_OFF)
                chv[i].st = c == 'R' ? E_REL : E_CUT;
    }
}

/* the bass on a note: T held, O a one-shot, R released, N cut */
static void bass_play(char c, int note)
{
    if (c == 'T' || c == 'O') {
        if (bsv.st != E_OFF)
            midi(0x82, bsv.note, 0);
        held_on(&bsv, &mv[V_BASS], 1, note, 0.8f);
        bsv.st = c == 'T' ? E_SUS : E_BDEC;
        midi(0x92, bsv.note, 100);
    } else if ((c == 'R' || c == 'N') && bsv.st != E_OFF) {
        bsv.st = c == 'R' ? E_REL : E_CUT;
    }
}

static int bass_note(void)
{
    const int8_t *t = OM_TYPE_TONES[type];
    return bass_n + (bass_kind == 1 ? 7 : bass_kind == 2 ? t[1] : 0);
}

static void rec_put(int what, int code);
static void bass_cmd(char c, char which)
{
    if (c == 'T' || c == 'O')
        bass_kind = which == '5' ? 1 : which == '3' ? 2 : 0;
    bass_play(c, bass_note());
    rec_put(2, c == 'T' || c == 'O' ? bass_note() | (c == 'O' ? 0x80 : 0) : c == 'R' ? 1 : 2);
}
static void chord_rec(char c)
{
    chord_cmd(c);
    rec_put(1, c);
}

/* the chord changed: what sounds moves to it (the OM's chord and bass follow the buttons) */
static void retune(void)
{
    int i;
    om_voicing(root, type, par[P_TRANSPOSE], par[P_OCTAVE], harp_n, chord_n, &bass_n);
    for (i = 0; i < OM_NSTR; i++)                 /* what each string plays when plucked next */
        om_str_note[i] = harp_n[i];
    for (i = 0; i < OM_NCHORD; i++)
        if (chv[i].st != E_OFF && chv[i].note != chord_n[i]) {
            midi(0x81, chv[i].note, 0);
            held_retune(&chv[i], &mv[V_CHORD + i], chord_n[i]);
            midi(0x91, chord_n[i], 90);
        }
    if (bsv.st != E_OFF && bsv.note != bass_note() && bass_kind < 3) {
        midi(0x82, bsv.note, 0);
        held_retune(&bsv, &mv[V_BASS], bass_note());
        midi(0x92, bsv.note, 100);
    }
    shim_rate = SHIM_HZ[((root + par[P_TRANSPOSE]) % 12 + 12) % 12] * 360.0f / OM_SR;
    om_root = (uint8_t)root;
    om_type = (uint8_t)type;
}

/* ---- the sequence: the loop and Markov */
static int drums_on, seq_on;
static int rec_state, rec_pos, rec_len, rec_src;   /* recording: its step, its length, the source it records */
static uint32_t gstep, loop_origin;               /* steps since the clock started; the loop's first */
static int lp_valid;                              /* the loop's chord state known at its position */
static int bars_left;                             /* Markov: bars to the next chord change */
static int pulse;                                 /* MIDI clock, 0..5 within a step */

static int loop_on(void) { return seq_on && par[P_BSRC] == SRC_LOOP && *loop_len && rec_state != 2; }
static int gen_on(void) { return seq_on && par[P_CHANGES] && !loop_on(); }

static void chord_eval(void)
{
    int r = live_root, t = live_type, g = live_gate, gen = 0;
    if (!live_gate) {
        if (loop_on() && lp_root >= 0) {
            r = lp_root;
            t = lp_type;
            g = lp_gate;
            gen = 1;
        } else if (gen_on() && gen_pad >= 0) {
            r = pad_root[gen_pad] % 12;
            t = pad_type[gen_pad] < CH_NTYPES ? pad_type[gen_pad] : 0;
            g = 1;
            gen = 1;
        }
    }
    if (r != root || t != type) {
        root = r;
        type = t;
        retune();
        if (rec_state == 2)
            rec_put(0, g ? 0x80 + root * CH_NTYPES + type : 0xFF);
    } else if (g != gate && rec_state == 2) {
        rec_put(0, g ? 0x80 + root * CH_NTYPES + type : 0xFF);
    }
    gate = g;
    om_gen = (uint8_t)gen;
    om_gen_pad = (int8_t)(!live_gate && gen_on() ? gen_pad : -1);
}

/* the probabilities of each button after `from`: a prior of root motions, the changes played, learnt */
static void markov_w(int from, float chaos, float *w)
{
    static const float MOTION[12] = {0.6f, 0.5f, 2.2f, 2.6f, 1.4f, 5.0f, 0.3f, 4.0f, 1.4f, 3.0f, 2.2f, 0.5f};
    int j, fr = pad_root[from] % 12, ft = pad_type[from];
    float e = 2.2f - 2.0f * chaos, sum = 0.0f;
    for (j = 0; j < OM_NPADS; j++) {
        int d = ((pad_root[j] - fr) % 12 + 12) % 12, tt = pad_type[j];
        float x = MOTION[d];
        if (j == from)
            x = 0.25f;
        else if (d == 0 && tt == ft)
            x = 0.1f;                              /* the same chord on another button */
        if ((ft == CH_7 || ft == CH_M7) && d == 5)
            x *= 2.0f;                             /* a dominant resolves */
        if (tt == CH_MAJ || tt == CH_MIN)
            x *= 1.3f;
        x += 3.0f * (float)learn[from * OM_NPADS + j];
        w[j] = fm_powf(x, e);
        sum += w[j];
    }
    for (j = 0; j < OM_NPADS; j++)
        w[j] /= sum;
}

void om_markov_probs(int from, int chaos, float p[OM_NPADS])
{
    markov_w((unsigned)from < OM_NPADS ? from : 1, (float)chaos * 0.01f, p);
}

static int pick(const float *w, int n)          /* an index by weight (the weights sum to 1, or any > 0) */
{
    float sum = 0.0f, u;
    int i;
    for (i = 0; i < n; i++)
        sum += w[i];
    u = mv_randf() * sum;
    for (i = 0; i < n - 1; i++) {
        u -= w[i];
        if (u < 0.0f)
            return i;
    }
    return n - 1;
}

static void markov_change(void)
{
    float w[OM_NPADS];
    int from = gen_pad >= 0 ? gen_pad : (live_pad >= 0 ? live_pad : 1);
    markov_w(from, (float)par[P_CHAOS] * 0.01f, w);
    gen_pad = pick(w, OM_NPADS);
}

/* the bass's chain: root, fifth, third, octave, the leading tone, the sixth (seventh) */
enum { MB_ROOT, MB_FIFTH, MB_THIRD, MB_OCT, MB_LEAD, MB_SIXTH, MB_N };
static const float MB_T[MB_N][MB_N] = {
    {2.0f, 4.0f, 2.0f, 2.5f, 1.0f, 1.0f}, {4.0f, 1.0f, 1.5f, 2.0f, 1.5f, 1.5f}, {2.0f, 3.5f, 0.5f, 1.0f, 1.0f, 1.0f},
    {2.0f, 3.0f, 1.0f, 0.5f, 1.0f, 1.5f}, {6.0f, 1.0f, 0.5f, 0.5f, 0.2f, 0.3f}, {2.0f, 3.0f, 1.0f, 1.0f, 1.5f, 0.3f}};
static int mb_state;
static int mb_note(int s)
{
    const int8_t *t = OM_TYPE_TONES[type];
    static const int8_t OFF[MB_N] = {0, 7, 0, 12, -1, 9};
    int n = bass_n + OFF[s];
    if (s == MB_THIRD)
        n = bass_n + (t[1] < 7 ? t[1] : 4);
    if (s == MB_SIXTH && (type == CH_7 || type == CH_M7))
        n = bass_n + 10;
    return n;
}
static void markov_bass(const om_rhythm_t *r, int st)
{
    float var = (float)par[P_BVAR] * 0.01f, chaos = (float)par[P_CHAOS] * 0.01f, w[MB_N];
    char c = r->bass[st];
    int half = r->len / 2, k;
    if (c == 'R' || c == 'N') {
        bass_play(c, 0);
        rec_put(2, c == 'R' ? 1 : 2);
        return;
    }
    if (c != 'T' && c != 'O') {                     /* no note in the rhythm: one now and then */
        float p = (st & 1) ? var * 0.12f : var * 0.3f;
        if (mv_randf() >= p)
            return;
        c = 'O';
    }
    for (k = 0; k < MB_N; k++)
        w[k] = fm_powf(MB_T[mb_state][k] * (k == MB_OCT || k == MB_LEAD || k == MB_SIXTH ? 0.3f + var : 1.0f),
                       1.6f - 1.4f * chaos);
    mb_state = st % half == 0 && mv_randf() > chaos * 0.5f ? MB_ROOT : pick(w, MB_N);
    bass_kind = 3;                                  /* (not a pattern's: retune leaves it) */
    bass_play(c, mb_note(mb_state));
    rec_put(2, mb_note(mb_state) | (c == 'O' ? 0x80 : 0));
}

/* the arp: a walk over the strings, steps of one or two most likely, going on the way it went */
static int arp_s = 7, arp_dir = 1;
static void markov_arp(int st)
{
    static const float JUMP[8] = {0.3f, 4.0f, 2.2f, 1.2f, 0.6f, 0.3f, 0.15f, 0.1f};
    float dens = (float)par[P_ARP] * 0.01f, chaos = (float)par[P_CHAOS] * 0.01f, w[15], p;
    int d, beat = st % 4 == 0;
    p = dens * (beat ? 1.25f : (st & 1) ? 0.6f : 1.0f);
    if (mv_randf() >= p)
        return;
    for (d = -7; d <= 7; d++) {
        int s = arp_s + d, a = d < 0 ? -d : d;
        float x = (s < 0 || s >= OM_NSTR) ? 0.0f : JUMP[a] * ((d > 0) == (arp_dir > 0) && d ? 1.6f : 1.0f);
        w[d + 7] = x > 0.0f ? fm_powf(x, 1.0f - 0.85f * chaos) : 0.0f;
    }
    d = pick(w, 15) - 7;
    if (d)
        arp_dir = d > 0 ? 1 : -1;
    arp_s += d;
    if (arp_s <= 0 || arp_s >= OM_NSTR - 1)
        arp_dir = -arp_dir;
    arp_s = arp_s < 0 ? 0 : arp_s > OM_NSTR - 1 ? OM_NSTR - 1 : arp_s;
    strum(arp_s, beat ? 110 : 70 + (int)(mv_randf() * 30.0f));
}

/* ---- the loop: 3 bytes a step. [0] the chord: 0 none, 0x80 + root * 9 + type, 0xFF let go; [1] the
 * chord's pattern (T R O N, 0 none); [2] the bass: 0 none, a note (| 0x80 a one-shot), 1 release, 2 cut */
static void rec_put(int what, int code)
{
    int at;
    if (rec_state != 2)
        return;
    at = rec_pos + (pulse >= 3 ? 1 : 0);           /* between steps: the nearer one */
    if (at >= rec_len)
        return;
    loop[at * 3 + what] = (uint8_t)code;
}
static void rec_begin(void)
{
    int i, bars = 1 << (par[P_BARS] & 3);
    rec_len = bars * (om_steps / 2);
    if (rec_len > OM_LOOP_MAX)
        rec_len = OM_LOOP_MAX;
    for (i = 0; i < OM_LOOP_MAX * 3; i++)
        loop[i] = 0;
    rec_pos = 0;
    rec_state = 2;
    rec_src = par[P_BSRC] == SRC_LOOP ? SRC_RHYTHM : par[P_BSRC];
    loop_origin = gstep;
    loop[0] = gate ? (uint8_t)(0x80 + root * CH_NTYPES + type) : 0xFF;
    om_rec_state = 2;
}
static void rec_end(int len)
{
    int half = om_steps / 2;
    if (len > 0) {
        len = (len + half - 1) / half * half;      /* whole bars */
        *loop_len = (uint8_t)(len < rec_len ? len : rec_len);
        par[P_BSRC] = SRC_LOOP;
        om_bsrc = SRC_LOOP;
        lp_valid = 0;
    }
    rec_state = 0;
    om_rec_state = 0;
    om_loop_done++;
}

/* the loop's chord at a step: the last change at or before it (wrapping) */
static void loop_scan(int pos)
{
    int i, n = *loop_len;
    lp_root = -1;
    lp_gate = 0;
    for (i = 0; i < n; i++) {
        int k = ((pos - i) % n + n) % n, c = loop[k * 3];
        if (c >= 0x80 && c != 0xFF) {
            lp_root = (c - 0x80) / CH_NTYPES;
            lp_type = (c - 0x80) % CH_NTYPES;
            lp_gate = 1;
            return;
        }
        if (c == 0xFF) {
            /* let go here: the chord before it, not sounding */
            for (; i < n; i++) {
                k = ((pos - i) % n + n) % n;
                c = loop[k * 3];
                if (c >= 0x80 && c != 0xFF) {
                    lp_root = (c - 0x80) / CH_NTYPES;
                    lp_type = (c - 0x80) % CH_NTYPES;
                    break;
                }
            }
            return;
        }
    }
}

static void loop_step(int pos)
{
    const uint8_t *e = &loop[pos * 3];
    if (!lp_valid) {
        loop_scan(pos);
        lp_valid = 1;
    } else if (e[0] >= 0x80) {
        if (e[0] == 0xFF) {
            lp_gate = 0;
        } else {
            lp_root = (e[0] - 0x80) / CH_NTYPES;
            lp_type = (e[0] - 0x80) % CH_NTYPES;
            lp_gate = 1;
        }
    }
    chord_eval();
    if (e[1])
        chord_cmd((char)e[1]);
    if (e[2] == 1 || e[2] == 2) {
        bass_play(e[2] == 1 ? 'R' : 'N', 0);
    } else if (e[2]) {
        int n = e[2] & 0x7F;
        if (live_gate && lp_root >= 0) {           /* a chord held over the loop: the bass moves to it */
            int d = ((root - lp_root) % 12 + 12) % 12;
            n += d > 6 ? d - 12 : d;
        }
        bass_kind = 3;
        bass_play(e[2] & 0x80 ? 'O' : 'T', n);
    }
}

/* ---- rhythm */
typedef struct {
    uint32_t pos;
    uint8_t on;
} drum_t;
static drum_t drum[OM_NDRUM];
static int rh, rh_next, step;
static float pulse_left, step_len;
static uint32_t ext_age;                         /* render calls since the last clock tick */
static const uint8_t GM_DRUM[OM_NDRUM] = {36, 75, 42, 51, 38};   /* BD, claves, closed hat, ride, snare */

static void tempo_set(void) { step_len = OM_SR * 60.0f / ((float)par[P_TEMPO] * 4.0f); }

static void drums_hit(uint8_t d)
{
    int i;
    for (i = 0; i < OM_NDRUM; i++)
        if (d >> i & 1u) {
            drum[i].pos = 0;
            drum[i].on = 1;
            midi(0x99, GM_DRUM[i], 100);
            midi(0x89, GM_DRUM[i], 0);
        }
    om_drum_hit |= d;
}

static void do_step(void)
{
    const om_rhythm_t *r = &OM_RHYTHM[rh];
    int half = r->len / 2, src;
    if (step % half == 0) {                       /* a bar */
        if (rec_state == 1)
            rec_begin();
        if (gen_on() && !live_gate) {
            if (gen_pad < 0) {
                gen_pad = live_pad >= 0 ? live_pad : 1;   /* Markov takes over from the button played last */
                bars_left = 1 << (par[P_CHANGES] - 1);
            } else if (--bars_left <= 0) {
                markov_change();
                bars_left = 1 << (par[P_CHANGES] - 1);
            }
            chord_eval();
        }
    }
    src = rec_state == 2 ? rec_src : par[P_BSRC];
    if (drums_on)
        drums_hit(r->drum[step]);
    if (seq_on) {
        if (loop_on()) {
            int pos = (int)((gstep - loop_origin) % *loop_len);
            om_loop_pos = (uint8_t)pos;
            loop_step(pos);
        } else if (gate) {
            if (src == SRC_MARKOV)
                markov_bass(r, step);
            else if (r->bass[step] != '-')
                bass_cmd(r->bass[step], r->note[step]);
            if (r->chord[step] != '-')
                chord_rec(r->chord[step]);
        }
        if (par[P_ARP])
            markov_arp(step);
    }
    if (rec_state == 2) {
        om_loop_pos = (uint8_t)rec_pos;
        if (++rec_pos >= rec_len)
            rec_end(rec_len);                      /* the loop plays from the next step: its first */
    }
    om_step = (uint8_t)step;
    gstep++;
    if (++step >= r->len) {
        step = 0;
        if (rh_next != rh) {                     /* a new rhythm starts on the bar */
            rh = rh_next;
            om_steps = OM_RHYTHM[rh].len;
        }
    }
}

/* ---- reverb: Dattorro's plate (J. Dattorro, "Effect Design, Part 1", JAES 1997): a predelay, four
 * input diffusers, then a figure-eight tank of two halves, each a modulated allpass, a delay, a damping
 * lowpass, an allpass and a delay, feeding the other half. Stereo out from seven taps a side. The
 * paper's lengths are for 29761 Hz; DS() scales them to 44.1 kHz. */
#define DS(n) (((n) * 14818 + 5000) / 10000)          /* 44100 / 29761 */
#define PRE_N 442                                    /* 10 ms */
#define EXC DS(16)                                   /* the tank allpasses' modulation, samples */
enum { L_PRE, L_IN1, L_IN2, L_IN3, L_IN4, L_APL, L_D1L, L_AP2L, L_D2L, L_APR, L_D1R, L_AP2R, L_D2R, L_N };
static const int DL_LEN[L_N] = {PRE_N, DS(142), DS(107), DS(379), DS(277), DS(672) + EXC + 2, DS(4453), DS(1800),
                                DS(3720), DS(908) + EXC + 2, DS(4217), DS(2656), DS(3163)};
#define DL_TOTAL (PRE_N + DS(142) + DS(107) + DS(379) + DS(277) + DS(672) + DS(4453) + DS(1800) + DS(3720) + DS(908) + \
                  DS(4217) + DS(2656) + DS(3163) + 2 * (EXC + 2))
static float pl_buf[DL_TOTAL] OM_POOL;
typedef struct {
    float *b;
    int n, i;
} dl_t;
static dl_t dl[L_N];
static float pl_bw, pl_damp_l, pl_damp_r, pl_decay = 0.5f, pl_dd2 = 0.5f, lfo_s, lfo_c = 1.0f;

static inline float tap(const dl_t *d, int k)          /* the value written k samples ago, 1..n */
{
    int j = d->i - k;
    return d->b[j < 0 ? j + d->n : j];
}
static inline void push(dl_t *d, float x)
{
    d->b[d->i] = x;
    if (++d->i >= d->n)
        d->i = 0;
}
static inline float allpass(dl_t *d, int len, float x, float g)
{
    float z = tap(d, len), v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}
static inline float allpass_mod(dl_t *d, float len, float x, float g)   /* a fractional, moving length */
{
    int k = (int)len;
    float f = len - (float)k, z = tap(d, k) + (tap(d, k + 1) - tap(d, k)) * f, v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}

static inline void reverb(float in, float *ol, float *or_)
{
    float x, a, b, fb_l = tap(&dl[L_D2R], DS(3163)), fb_r = tap(&dl[L_D2L], DS(3720));
    push(&dl[L_PRE], in);
    x = tap(&dl[L_PRE], PRE_N);
    pl_bw += 0.7f * (x - pl_bw);                     /* input bandwidth */
    x = allpass(&dl[L_IN1], DS(142), pl_bw, 0.75f);
    x = allpass(&dl[L_IN2], DS(107), x, 0.75f);
    x = allpass(&dl[L_IN3], DS(379), x, 0.625f);
    x = allpass(&dl[L_IN4], DS(277), x, 0.625f);
    {   /* the modulation: ~0.9 Hz, sine and cosine for the two halves */
        float s = lfo_s + 0.000128f * lfo_c, c = lfo_c - 0.000128f * s;
        lfo_s = s;
        lfo_c = c;
    }
    a = allpass_mod(&dl[L_APL], (float)DS(672) + (float)EXC * lfo_s, x + pl_decay * fb_l, -0.7f);
    push(&dl[L_D1L], a);
    b = tap(&dl[L_D1L], DS(4453));
    pl_damp_l += 0.62f * (b - pl_damp_l);
    b = allpass(&dl[L_AP2L], DS(1800), pl_damp_l * pl_decay, pl_dd2);
    push(&dl[L_D2L], b);
    a = allpass_mod(&dl[L_APR], (float)DS(908) + (float)EXC * lfo_c, x + pl_decay * fb_r, -0.7f);
    push(&dl[L_D1R], a);
    b = tap(&dl[L_D1R], DS(4217));
    pl_damp_r += 0.62f * (b - pl_damp_r);
    b = allpass(&dl[L_AP2R], DS(2656), pl_damp_r * pl_decay, pl_dd2);
    push(&dl[L_D2R], b);
    *ol = 0.6f * (tap(&dl[L_D1R], DS(266)) + tap(&dl[L_D1R], DS(2974)) - tap(&dl[L_AP2R], DS(1913)) +
                  tap(&dl[L_D2R], DS(1996)) - tap(&dl[L_D1L], DS(1990)) - tap(&dl[L_AP2L], DS(187)) -
                  tap(&dl[L_D2L], DS(1066)));
    *or_ = 0.6f * (tap(&dl[L_D1L], DS(353)) + tap(&dl[L_D1L], DS(3627)) - tap(&dl[L_AP2L], DS(1228)) +
                   tap(&dl[L_D2L], DS(2673)) - tap(&dl[L_D1R], DS(2111)) - tap(&dl[L_AP2R], DS(335)) -
                   tap(&dl[L_D2R], DS(121)));
}

static void plate_init(void)
{
    float *p = pl_buf;
    int k, i;
    for (k = 0; k < L_N; k++) {
        dl[k].b = p;
        dl[k].n = DL_LEN[k];
        dl[k].i = 0;
        p += DL_LEN[k];
    }
    for (i = 0; i < DL_TOTAL; i++)
        pl_buf[i] = 0.0f;
    pl_bw = pl_damp_l = pl_damp_r = 0.0f;
    lfo_s = 0.0f;
    lfo_c = 1.0f;
}

/* ---- parameters */
static void apply(int p, int v)
{
    const om_param_t *pi = &PARAMS[p];
    int i;
    if (v < pi->lo)
        v = pi->lo;
    if (v > pi->hi)
        v = pi->hi;
    par[p] = (int16_t)v;
    switch (p) {
    case P_VOICE1: v1 = (float)v * 0.01f; break;
    case P_VOICE2: v2 = (float)v * 0.01f; break;
    case P_SUSTAIN:
        sus_rate = (1.0f / OM_SR) / (0.366f + 2.734f * (float)v * 0.01f);
        h_decay = 0.2f + 0.5f * (float)v * 0.01f;
        ver_h++;
        break;
    case P_HTIMBRE: h_timbre = (float)v * 0.01f; ver_h++; break;
    case P_HCOLOR: h_color = (float)v * 0.01f; ver_h++; break;
    case P_HLEVEL: hvol = (float)v * 0.01f; break;
    case P_CTIMBRE: ver_c++; break;
    case P_BTIMBRE: ver_b++; break;
    case P_CHORD: cvol = (float)v * 0.01f; break;
    case P_BLEVEL: bvol = (float)v * 0.01f; break;
    case P_RHYVOL: rvol = (float)v * 0.01f; break;
    case P_REVERB: rsend = (float)v * 0.01f * (float)v * 0.01f; break;
    case P_CREV: csend = (float)v * 0.01f * (float)v * 0.01f; break;
    case P_SPACE:                                 /* the plate's decay: a small room to a long hall */
        pl_decay = 0.35f + 0.55f * (float)v * 0.01f;
        pl_dd2 = fm_clampf(pl_decay + 0.15f, 0.25f, 0.5f);
        break;
    case P_TEMPO: tempo_set(); break;
    case P_RHYTHM:
        rh_next = v;
        if (!om_playing) {
            rh = v;
            om_steps = OM_RHYTHM[rh].len;
        }
        break;
    case P_TUNE:
        tunefac = fm_exp2f((float)v * (1.0f / 1200.0f));
        retune();
        break;
    case P_TRANSPOSE: case P_OCTAVE: retune(); break;
    case P_WIDTH:
        for (i = 0; i < OM_NSTR; i++) {           /* low strings left, high right */
            float p0 = ((float)i / 15.0f * 2.0f - 1.0f) * (float)v * 0.01f, a = (p0 + 1.0f) * (FM_PI / 4.0f);
            pan_l[i] = fm_cosf(a);
            pan_r[i] = fm_sinf(a);
        }
        break;
    case P_MIDI:
        if (!v)
            om_midi_out(0xB0, 123, 0);            /* all notes off, as it goes quiet */
        break;
    case P_BSRC:
        om_bsrc = (uint8_t)v;
        lp_valid = 0;
        chord_eval();
        break;
    case P_CHANGES:
        if (!v)
            gen_pad = -1;
        chord_eval();
        break;
    default: break;
    }
}

static void quiet(void)
{
    int i;
    for (i = 0; i < OM_NSTR; i++) {
        str[i].on = 0;
        str_midi[i] = 0;
    }
    for (i = 0; i < NV; i++)
        mv[i].on = 0;
    for (i = 0; i < OM_HV_MAX; i++)
        hv_str[i] = -1;
    for (i = 0; i < OM_NCHORD; i++)
        chv[i].st = E_OFF;
    bsv.st = E_OFF;
    for (i = 0; i < OM_NDRUM; i++)
        drum[i].on = 0;
    for (i = 0; i < 3; i++)
        om_midi_out(0xB0 | (uint32_t)i, 123, 0);
}

void om_init(void)
{
    int i;
    for (i = 0; i < 360; i++) {
        float s = fm_sinf((float)(i + 125) * (FM_PI / 180.0f));
        sin3[i] = s == 0.0f ? 0.0f : (s > 0.0f ? 1.0f : -1.0f) * fm_powf(fm_fabsf(s), 0.6f);
    }
    mv_tables();
    for (i = 0; i < NV; i++)
        mv_init(&mv[i], mv_dl[i], mv_st[i]);
    for (i = 0; i < OM_HV_MAX; i++)
        hv_str[i] = -1;
    om_poly_n = (uint8_t)hv_n;
    plate_init();
    q_r = q_w = 0;
    root = live_root = 0;
    type = live_type = CH_MAJ;
    for (i = 0; i < P_NPARAMS; i++)
        apply(i, PARAMS[i].def);
    retune();
}

/* the rhythm's clock: a step every 6 pulses. From its own tempo (pulse_left) or from MIDI clock in */
static void clock_pulse(void)
{
    if (pulse == 0)
        do_step();
    if (!om_ext)
        midi(0xF8, 0, 0);
    if (++pulse >= 6)
        pulse = 0;
}

static void play_start(int from_top)
{
    if (from_top) {
        rh = rh_next;
        om_steps = OM_RHYTHM[rh].len;
        step = 0;
        pulse = 0;
        gstep = 0;
        loop_origin = 0;
        lp_valid = 0;
        gen_pad = -1;
    }
    pulse_left = 0.0f;
    om_playing = 1;
    if (!om_ext)
        midi(from_top ? 0xFA : 0xFB, 0, 0);
}

static void seq_release(void)                    /* the sequence stopped: what it held lets go */
{
    gen_pad = -1;
    chord_eval();
    if (!gate) {
        chord_cmd('R');
        bass_play('R', 0);
    }
}

static void play_stop(void)
{
    om_playing = 0;
    if (!om_ext)
        midi(0xFC, 0, 0);
    if (rec_state == 2)
        rec_end(rec_pos);
    rec_state = 0;
    om_rec_state = 0;
    seq_release();
}

/* a track on or off; the clock starts with the first and stops with the last */
static void track(int seq, int on)
{
    if (seq) {
        if (seq_on == on)
            return;
        seq_on = on;
        om_seq_on = (uint8_t)on;
        lp_valid = 0;
    } else {
        if (drums_on == on)
            return;
        drums_on = on;
        om_drums_on = (uint8_t)on;
    }
    if (on && !om_playing) {
        play_start(1);
    } else if (!on && !drums_on && !seq_on && om_playing) {
        play_stop();
    } else if (!on && seq) {
        if (rec_state)
            rec_end(rec_state == 2 ? rec_pos : 0);
        seq_release();
    } else if (on && !seq && pulse > 0 && pulse < 3) {
        drums_hit(OM_RHYTHM[rh].drum[om_step]);    /* just after a step: its drums, late by less than half */
    }
    if (on && seq)
        chord_eval();
}

static void drain(void)
{
    while (q_r != q_w) {
        uint32_t c = q[q_r % QN];
        int a = (int)(c >> 16 & 0xFFu), b = (int)(int16_t)(c & 0xFFFFu);
        BARRIER();
        q_r++;
        switch (c >> 24) {
        case C_SET:
            if (a < P_NPARAMS)
                apply(a, b);
            break;
        case C_CHORD:
            live_root = a % 12;
            live_type = b < CH_NTYPES ? b : 0;
            chord_eval();
            break;
        case C_PAD:
            live_pad = a < OM_NPADS ? a : -1;
            if (live_pad >= 0 && gen_pad >= 0)
                gen_pad = live_pad;                 /* Markov goes on from the button played */
            break;
        case C_GATE:
            live_gate = a;
            if (!seq_on) {                          /* by hand: the chord sounds while the pad is down */
                chord_eval();
                chord_cmd(a ? 'T' : 'R');
                bass_kind = 0;
                bass_cmd(a ? 'T' : 'R', 'R');
            } else {
                chord_eval();
                if (!gate) {
                    chord_rec('R');
                    bass_play('R', 0);
                    rec_put(2, 1);
                }
            }
            break;
        case C_STRUM:
            if (a < OM_NSTR)
                strum(a, b);
            break;
        case C_PLAY:
            track(0, a);
            break;
        case C_SEQ:
            track(1, a);
            break;
        case C_REC:
            if (a == OM_REC_START) {
                if (!om_playing) {
                    track(1, 1);
                    rec_state = 1;
                    rec_begin();                    /* the clock starts at the top: the loop with it */
                } else {
                    if (!seq_on)
                        track(1, 1);
                    rec_state = 1;                  /* from the next bar */
                    om_rec_state = 1;
                }
            } else if (a == OM_REC_STOP) {
                if (rec_state)                      /* (the loop goes on in step: it began on a bar) */
                    rec_end(rec_state == 2 ? rec_pos : 0);
            } else {
                rec_state = 0;
                om_rec_state = 0;
                *loop_len = 0;
                if (par[P_BSRC] == SRC_LOOP) {
                    par[P_BSRC] = SRC_RHYTHM;
                    om_bsrc = SRC_RHYTHM;
                }
                om_loop_done++;
                chord_eval();
            }
            break;
        case C_BNOTE:
            if (b) {
                bass_kind = 3;
                bass_play('T', a);
                rec_put(2, a & 0x7F);
            } else if (bsv.note == a) {
                bass_play('R', 0);
                rec_put(2, 1);
            }
            break;
        case C_POLY:
            hv_n = a < 2 ? 2 : a > OM_HV_MAX ? OM_HV_MAX : a;
            om_poly_n = (uint8_t)hv_n;
            break;
        case C_CLOCK:
            om_ext = 1;                             /* a clock message: someone else leads */
            ext_age = 0;
            if (a == OM_CLK_TICK) {
                if (om_playing)
                    clock_pulse();
            } else if (a == OM_CLK_START) {
                drums_on = seq_on = 1;
                om_drums_on = om_seq_on = 1;
                play_start(1);
            } else if (a == OM_CLK_CONTINUE) {
                if (!om_playing) {
                    drums_on = seq_on = 1;
                    om_drums_on = om_seq_on = 1;
                    play_start(0);
                }
            } else if (om_playing) {
                drums_on = seq_on = 0;
                om_drums_on = om_seq_on = 0;
                play_stop();
            }
            break;
        case C_PANIC:
            quiet();
            break;
        }
    }
}

/* ------------------------------------------------------------- render --- */
#define BLK 256
static float mixl[BLK], mixr[BLK], send[BLK], vbuf[BLK];

static void render_strings(uint32_t n)
{
    const float g1 = v1 * (1.0f - 0.5f * v2) * 1.2f * hvol / 0.8f,
                g2 = v2 * (1.0f - 0.5f * v1) * 0.85f * 1.2f * hvol / 0.8f;
    int s, i;
    for (s = 0; s < OM_NSTR; s++) {
        str_t *v = &str[s];
        float pos = v->pos, y1 = v->y1, y2 = v->y2, e = v->v, g = v->g, c = v->c, m = v->m;
        const float inc = v->inc, gl = pan_l[s], gr = pan_r[s];
        uint32_t j;
        om_str_level[s] = 0.0f;
        if (!v->on)
            continue;
        for (j = 0; j < n; j++) {
            float x1, x2, o;
            if (!(j & 15u)) {                       /* every 16 samples: the filter and the shimmer */
                float c96 = (0.15f + 0.85f * e) * v->k;
                int ph = (int)shim_ph + v->shim_off;
                c = 1.0f - fm_exp2f(2.1768707f * fm_log2f(1.0f - c96));   /* 96 kHz coefficient at 44.1 */
                m = (0.61f + 0.61f * sin3[ph % 360]) * v->modamt;
            }
            x1 = wave(OM_WAVE_HARP_BASE, pos) + wave(OM_WAVE_HARP_MOD, pos) * m;
            x2 = wave(OM_WAVE_HARP, pos);
            y1 += c * (x1 - y1);
            y2 += c * (x2 - y2);
            g += 0.05f * (e - g);
            o = (y1 * g1 + y2 * g2) * g;
            mixl[j] += o * gl;
            mixr[j] += o * gr;
            send[j] += o * rsend;
            pos = adv(pos, inc);
            e -= sus_rate * (0.34f + 8.5f * e * e * e);
            if (e <= 0.0f) {
                e = 0.0f;
                if (g < 1e-4f) {
                    v->on = 0;
                    str_midi_off(s, v->note);
                    break;
                }
            }
        }
        v->pos = pos;
        v->y1 = fm_flush(y1);
        v->y2 = fm_flush(y2);
        v->v = e;
        v->g = g;
        v->c = c;
        v->m = m;
        om_str_level[s] = v->on ? e : 0.0f;
    }
    shim_ph += shim_rate * (float)n;
    while (shim_ph >= 360.0f)
        shim_ph -= 360.0f;
    hv_clock += n;
    om_hv_active = 0;
    for (i = 0; i < OM_HV_MAX; i++) {               /* the pool */
        mv_t *v = &mv[i];
        uint32_t j;
        float gl, gr, gs;
        int st = hv_str[i];
        if (!v->on)
            continue;
        om_hv_active++;
        if (v->ver != ver_h) {                       /* a knob turned: the ringing voices follow */
            harp_par(&v->p);
            mv_coef(v, 0, 12);
            v->ver = ver_h;
        }
        for (j = 0; j < n; j++)
            vbuf[j] = 0.0f;
        if (!mv_render(v, -1.0f, 0.0f, G_HARP[v->model] * hvol, G_HARP[v->model] * hvol, vbuf, n)) {
            if (st >= 0)
                str_midi_off(st, hv_note[i]);
            hv_str[i] = -1;
            continue;
        }
        gl = pan_l[hv_pan[i]];
        gr = pan_r[hv_pan[i]];
        gs = rsend;
        for (j = 0; j < n; j++) {
            float o = vbuf[j];
            mixl[j] += o * gl;
            mixr[j] += o * gr;
            send[j] += o * gs;
        }
        if (st >= 0)
            om_str_level[st] = fm_maxf(om_str_level[st], v->level);
    }
}

static float env_step(cv_t *v, int bass)
{
    switch (v->st) {
    case E_DEC:
        v->v -= (1.0f / OM_SR) / 0.333f;
        if (v->v <= 0.95f) {
            v->v = 0.95f;
            v->st = E_SUS;
        }
        break;
    case E_BDEC:
        v->v -= (1.0f / OM_SR) / 1.222f;
        if (v->v <= 0.95f) {
            v->v = 0.95f;
            v->st = E_REL;
        }
        break;
    case E_REL:
        v->v -= bass ? (1.0f / OM_SR) / 0.333f * (0.5f + v->v) : (1.0f / OM_SR) / 0.18f;
        break;
    case E_CUT:
        v->v -= (1.0f / OM_SR) / 0.004f;
        break;
    default:
        break;
    }
    if (v->v <= 0.0f) {
        v->v = 0.0f;
        if (v->g < 1e-4f) {
            v->st = E_OFF;
            midi(bass ? 0x82 : 0x81, v->note, 0);
        }
    }
    return v->v;
}

/* a chord or bass voice of a model: its envelope steps along (for its state and the screen), the
 * voice follows it: held (dust, the LPG open) while it sustains, damped when let go, faded when cut */
static void render_held(cv_t *v, mv_t *m, int bass, float gain, float sendg, uint32_t n)
{
    uint32_t j;
    int was = v->st;
    float c0 = v->cut, gate;
    if (v->st != E_OFF)
        for (j = 0; j < n && v->st != E_OFF; j++) {
            float e = env_step(v, bass);
            v->g += 0.05f * (e - v->g);
            if (v->st == E_CUT)
                v->cut = fm_maxf(v->cut - 1.0f / 176.0f, 0.0f);
        }
    if (!m->on)
        return;
    if (m->ver != (bass ? ver_b : ver_c) || ((v->st == E_REL || v->st == E_OFF) && was != v->st)) {
        held_par(m, bass, v->st == E_REL || v->st == E_OFF);
        mv_coef(m, !bass, bass ? 12 : 8);
        m->ver = bass ? ver_b : ver_c;
    }
    gate = v->st == E_OFF || v->st == E_REL || v->st == E_CUT ? 0.0f : v->g;
    for (j = 0; j < n; j++)
        vbuf[j] = 0.0f;
    if (!mv_render(m, gate * (bass ? 1.0f : 0.9f), bass ? 0.0f : gate, gain * c0, gain * v->cut, vbuf, n) || v->cut <= 0.0f) {
        m->on = 0;
        return;
    }
    for (j = 0; j < n; j++) {
        mixl[j] += vbuf[j];
        mixr[j] += vbuf[j];
        send[j] += vbuf[j] * sendg;
    }
}

static void render_chord(uint32_t n)
{
    int i;
    uint32_t j;
    const float gc = 0.4f * cvol * 0.7071f, gb = bvol * 0.7071f;
    float lvl = 0.0f;
    for (i = 0; i < OM_NCHORD; i++) {
        cv_t *v = &chv[i];
        if (v->model != M_OM84) {
            render_held(v, &mv[V_CHORD + i], 0, gc * G_HELD[v->model], csend, n);
        } else if (v->st != E_OFF) {
            for (j = 0; j < n && v->st != E_OFF; j++) {
                float e = env_step(v, 0), o;
                v->g += 0.05f * (e - v->g);
                o = wave(OM_WAVE_CHORD, v->pos) * v->g * gc;
                mixl[j] += o;
                mixr[j] += o;
                send[j] += o * csend;
                v->pos = adv(v->pos, v->inc);
            }
        }
        lvl = fm_maxf(lvl, v->v);
    }
    om_chord_level = lvl;
    if (bsv.model != M_OM84) {
        render_held(&bsv, &mv[V_BASS], 1, gb * G_HELD[bsv.model], csend * 0.5f, n);
    } else if (bsv.st != E_OFF) {
        cv_t *v = &bsv;
        for (j = 0; j < n && v->st != E_OFF; j++) {
            float e = env_step(v, 1), o;
            v->g += 0.05f * (e - v->g);
            o = (wave(OM_WAVE_BASS_BASE, v->pos) + wave(OM_WAVE_BASS_MOD, v->pos) * (1.0f - e * e)) * v->g * gb;
            mixl[j] += o;
            mixr[j] += o;
            send[j] += o * csend * 0.5f;               /* a little: a wet bass muddies */
            v->pos = adv(v->pos, v->inc);
        }
    }
    om_bass_level = bsv.st != E_OFF ? bsv.v : 0.0f;
}

static void render_drums(uint32_t n)
{
    int i;
    uint32_t j;
    const float g = rvol * (1.0f / 32768.0f) * 0.7071f;
    for (i = 0; i < OM_NDRUM; i++) {
        drum_t *d = &drum[i];
        const int16_t *w = OM_DRUM[i];
        uint32_t len = OM_DRUM_LEN[i];
        if (!d->on)
            continue;
        for (j = 0; j < n; j++) {
            float o;
            if (d->pos >= len) {
                d->on = 0;
                break;
            }
            o = (float)w[d->pos++] * g;
            mixl[j] += o;
            mixr[j] += o;
        }
    }
}

static void render_chunk(int32_t *out, uint32_t n, float gain)
{
    uint32_t j;
    for (j = 0; j < n; j++)
        mixl[j] = mixr[j] = send[j] = 0.0f;
    render_strings(n);
    render_chord(n);
    render_drums(n);
    for (j = 0; j < n; j++) {
        float wl, wr, l, r;
        reverb(send[j] * 0.5f, &wl, &wr);
        l = (mixl[j] + wl) * 0.42f * gain;
        r = (mixr[j] + wr) * 0.42f * gain;
        out[2 * j] = (int32_t)(fm_tanhf(l) * 8300000.0f);
        out[2 * j + 1] = (int32_t)(fm_tanhf(r) * 8300000.0f);
    }
}

void om_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 2.0f;
    drain();
    if (om_ext && ++ext_age > 100u)                /* ~0.6 s without a tick: back to its own tempo */
        om_ext = 0;
    while (n) {
        uint32_t k = n > BLK ? BLK : n;
        if (om_playing && !om_ext) {
            if (pulse_left <= 0.0f) {
                clock_pulse();
                pulse_left += step_len * (1.0f / 6.0f);
            }
            if ((float)k > pulse_left)
                k = (uint32_t)pulse_left + 1u;
            if (k > n)
                k = n;
            pulse_left -= (float)k;
        }
        render_chunk(out, k, gain);
        out += 2 * k;
        n -= k;
    }
}
