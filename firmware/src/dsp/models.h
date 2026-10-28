/* SPDX-License-Identifier: GPL-3.0-only AND MIT */
/* The voices after Mutable Instruments' Rings and Plaits: their models, rewritten in C for the FM-1
 * (single-precision floats, no libm: fastmath.h), each a voice the harp, the chord and the bass can play.
 *
 *   STRING   Plaits' string (physical_modelling/string.cc, string_voice.cc; from Rings): a delay line
 *            with a damping lowpass, plucked by a burst of filtered noise. COLOR below the middle
 *            curves the bridge (a sitar's buzz), above it stretches the partials (a piano's stiffness).
 *   MODAL    Plaits' modal resonator (physical_modelling/resonator.cc, modal_voice.cc; from Rings):
 *            a bank of band-pass modes, struck by a filtered click. COLOR is the structure: the modes
 *            from a string's harmonics to a bell's and a plate's inharmonic partials.
 *   ANALOG   Plaits' virtual analog: two band-limited (polyBLEP) oscillators, saw to square to a thin
 *            pulse (TIMBRE), the second one detuned (COLOR), through the low-pass gate.
 *   FM       Plaits' two-operator FM: TIMBRE the index, COLOR the ratio (Plaits' quantized ratios).
 *   FOLD     Plaits' waveshaper: a triangle (COLOR its slope) through Plaits' wavefolder (TIMBRE).
 *
 * The non-physical models go through Plaits' low-pass gate (fx/low_pass_gate.h, envelope.h): a vactrol
 * that a pluck pings, or a gate holds. Rates here are per sample at 44.1 kHz, and the LPG's per 16 samples.
 *
 * Derived from Mutable Instruments' eurorack code, Copyright 2015-2021 Emilie Gillet, under the MIT
 * licence: Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions: The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS",
 * WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS
 * OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * The C here is FoMni's (GPL-3.0-only); included once, by omni.c. */
#pragma once
#include <stdint.h>
#include "fastmath.h"
#include "omni.h"                      /* the models' numbers: M_* */
#define MV_MODES 16                    /* modal: up to 16 modes, four at a time (the caller says how many) */
#define MV_DL 1024u                    /* the string's delay line: down to 43 Hz */
#define MV_ST 256u                     /* the string's dispersion allpass */
#define MV_BS 16                       /* the low-pass gate's control rate, samples */
#ifndef MV_POOL
#define MV_POOL                        /* where the tables go (omni.c: the device's .pool) */
#endif

/* ------------------------------------------------------------ helpers --- */
static uint32_t mv_rng = 0x21u;
static inline uint32_t mv_rand(void) { return mv_rng = mv_rng * 1664525u + 1013904223u; }   /* stmlib Random */
static inline float mv_randf(void) { return (float)(mv_rand() >> 8) * (1.0f / 16777216.0f); }
static inline float mv_dust(float f)                  /* plaits/dsp/noise/dust.h */
{
    float u = mv_randf();
    return u < f ? u / f : 0.0f;
}
static inline float mv_semi(float s) { return fm_exp2f(s * (1.0f / 12.0f)); }   /* SemitonesToRatio */
static inline float mv_atanf(float x)                  /* |x| <= 1, 0.0015 rad */
{
    float a = fm_fabsf(x);
    return (FM_PI / 4.0f) * x - x * (a - 1.0f) * (0.2447f + 0.0663f * a);
}

/* tan(pi f), as stmlib's FREQUENCY_FAST (fitted for 16 Hz..16 kHz; Plaits' modes and filters use it) */
static inline float mv_tan(float f)
{
    const float a = 3.260e-01f * FM_PI * FM_PI * FM_PI, b = 1.823e-01f * FM_PI * FM_PI * FM_PI * FM_PI * FM_PI;
    float f2 = f * f;
    return f * (FM_PI + f2 * (a + b * f2));
}

/* the state-variable filter (stmlib Svf): g = tan(pi f), r = 1 / q */
typedef struct {
    float g, rg, h, s1, s2;
} mv_svf_t;
static inline void svf_set(mv_svf_t *f, float fn, float q)
{
    float g = mv_tan(fm_clampf(fn, 1e-5f, 0.499f)), r = 1.0f / q;
    f->g = g;
    f->rg = r + g;
    f->h = 1.0f / (1.0f + r * g + g * g);
}
static inline float svf_lp(mv_svf_t *f, float in)
{
    float hp = (in - f->rg * f->s1 - f->s2) * f->h, bp = f->g * hp + f->s1, lp;
    f->s1 = f->g * hp + bp;
    lp = f->g * bp + f->s2;
    f->s2 = f->g * bp + lp;
    return lp;
}

/* Plaits' structure -> stiffness (resources/lookup_tables.py lut_stiffness, as a function) */
static float mv_stiffness(float s)
{
    if (s < 0.25f)
        return -(0.25f - s) * 0.25f;
    if (s < 0.3f)
        return 0.0f;
    if (s < 0.9f)
        return 0.01f * fm_exp2f((s - 0.3f) / 0.6f * 2.005f * 3.321928f) - 0.01f;
    s = (s - 0.9f) / 0.1f;
    return 1.5f - fm_cosf(s * s * FM_PI) * 0.5f;
}

/* the sine and the wavefolder, as tables (Plaits' lut_sine, lut_fold) */
#define MV_TAB 512
static float mv_sine[MV_TAB + 1] MV_POOL, mv_fold[MV_TAB + 1] MV_POOL;
static void mv_tables(void)
{
    int i;
    float mx = 0.0f;
    for (i = 0; i <= MV_TAB; i++) {
        float x = (float)i / (MV_TAB / 2) - 1.0f, w = fm_expf(-x * x * 8.0f), a3 = 3.0f * x;
        /* atan(3x) for |3x| up to 3: atan(1/u) = pi/2 - atan(u) */
        float at = fm_fabsf(a3) <= 1.0f ? mv_atanf(a3) : (a3 > 0.0f ? 1.0f : -1.0f) * (FM_PI / 2.0f) - mv_atanf(1.0f / a3);
        mv_sine[i] = fm_sinf(FM_TWO_PI * (float)i / MV_TAB);
        mv_fold[i] = fm_sinf(8.0f * FM_PI * x) * w + at * (1.0f - w);
        mx = fm_maxf(mx, fm_fabsf(mv_fold[i]));
    }
    for (i = 0; i <= MV_TAB; i++)
        mv_fold[i] /= mx;
}
static inline float mv_tab(const float *t, float x)    /* x in [0, 1] */
{
    float p = x * MV_TAB;
    int i = (int)p;
    if (i >= MV_TAB)
        return t[MV_TAB];
    if (i < 0)
        return t[0];
    return t[i] + (t[i + 1] - t[i]) * (p - (float)i);
}
static inline float mv_sin1(float x) { return mv_tab(mv_sine, x - fm_floorf(x)); }   /* sin(2 pi x) */

/* polyBLEP (a band-limited step's residual) for a phase t in [0, 1), increment dt */
static inline float mv_blep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

/* Plaits' FM ratios (resources/lookup_tables.py fm_frequency_ratios) */
static const float MV_RATIO[23] = {0.5f, 0.5047f, 0.7071f, 0.7854f, 1.0f, 1.0093f, 1.4142f, 1.5708f, 1.75f, 2.0f,
                                   2.0187f, 2.25f, 2.75f, 2.8284f, 3.0f, 3.1416f, 3.4641f, 4.0f, 4.2426f, 4.7124f,
                                   5.0f, 5.6569f, 8.0f};

/* -------------------------------------------------------------- voice --- */
/* what a voice plays: f0 (cycles a sample), the knobs 0..1, the strike's accent 0..1 */
typedef struct {
    float f0, timbre, color, decay, accent;
} mv_par_t;

typedef struct {
    uint8_t model, on;
    uint8_t ver;                       /* the parameters' version the coefficients were made from */
    int8_t nm;                         /* modal: modes in use */
    float level;                       /* for the screen: roughly the envelope, 0..1 */
    float quiet;                       /* samples the output has been near silent */
    mv_par_t p;
    float imp;                         /* an impulse for the next sample (a strike) */
    uint32_t noise_n;                  /* samples of noise burst left (the string's pluck) */
    mv_svf_t exc;                      /* the exciter's lowpass */
    /* modal */
    float s1[MV_MODES], s2[MV_MODES], g[MV_MODES], rg[MV_MODES], h[MV_MODES], a[MV_MODES];
    /* string */
    float *dl, *st;
    uint32_t dw, sw;
    float delay, delay_t, dcx, dcy, disp, curved, nl, stretch_pt, stretch_corr, noise_amt, noise_f, bridge, ap_g, loss;
    mv_svf_t damp;
    /* oscillators and the low-pass gate */
    float ph[2], inc[2], amt, vac, lpg_g, lpg_bleed, dec;
    uint8_t ramp;
    mv_svf_t lpg;
} mv_t;

/* the modal resonator's modes (Plaits Resonator::Process): frequencies, Qs and gains */
static void modal_coef(mv_t *v, float structure, float brightness, float damping, float position, int nmax)
{
    float stiffness = mv_stiffness(structure), st = stiffness, stretch = 1.0f, comp = 1.0f, f0, harmonic, q, q_loss;
    int i;
    for (i = 0; i < 2; i++) {              /* NthHarmonicCompensation(3, stiffness) */
        comp += st;
        st *= st < 0.0f ? 0.93f : 0.98f;
    }
    f0 = v->p.f0 / comp;
    harmonic = f0;
    {
        float qs = mv_semi(damping * 79.7f);
        q = 500.0f * qs * qs;
    }
    brightness *= 1.0f - structure * 0.3f;
    brightness *= 1.0f - damping * 0.3f;
    q_loss = brightness * (2.0f - brightness) * 0.85f + 0.15f;
    for (i = 0; i < nmax; i++) {
        float mf = harmonic * stretch, qi, r, g;
        if (mf >= 0.499f)
            break;                         /* above Nyquist: Plaits keeps it at 0.499, all but silent */
        qi = 1.0f + mf * q;
        g = mv_tan(mf);
        r = 1.0f / qi;
        v->g[i] = g;
        v->rg[i] = r + g;
        v->h[i] = 1.0f / (1.0f + r * g + g * g);
        /* the strike's position: Plaits' approximate cosine oscillator, 0..1 */
        v->a[i] = (0.5f + 0.5f * fm_cosf(FM_TWO_PI * position * (float)i)) * 0.25f * (1.0f - mf * 2.0f);
        stretch += stiffness;
        stiffness *= stiffness < 0.0f ? 0.93f : 0.98f;
        harmonic += f0;
        q *= q_loss;
    }
    for (; i < v->nm; i++)                 /* fewer modes than before (a higher note): clear the rest */
        v->s1[i] = v->s2[i] = 0.0f;
    for (; i & 3; i++) {                   /* a whole batch of four: the extra ones silent */
        v->a[i] = v->g[i] = v->s1[i] = v->s2[i] = 0.0f;
        v->rg[i] = v->h[i] = 1.0f;
    }
    v->nm = (int8_t)i;
}

/* the string's coefficients (Plaits String::ProcessInternal) */
static void string_coef(mv_t *v, float structure, float brightness, float damping)
{
    float f0 = v->p.f0, delay = fm_clampf(1.0f / f0, 4.0f, (float)MV_DL - 4.0f), dc, df, nl, t60;
    /* FoMni's: a low string rings for many seconds more than a high one at the same damping (the loss is
     * per period), so the damping follows the note (+0.008 a semitone around G4); and a loss each time
     * round the loop caps the ring at 0.5 + 12 d^2 seconds, as Plaits' highest notes would otherwise ring on */
    damping = fm_clampf(damping + 0.008f * (12.0f * fm_log2f(f0 * (44100.0f / 440.0f)) + 69.0f - 67.0f), 0.0f, 1.0f);
    t60 = 0.5f + 12.0f * damping * damping;
    v->loss = fm_exp2f(-9.966f * delay / (t60 * 44100.0f));
    dc = fm_minf(12.0f + damping * damping * 60.0f + brightness * 24.0f, 84.0f);
    df = fm_minf(f0 * mv_semi(dc), 0.499f);
    if (damping >= 0.95f) {
        float inf = 20.0f * (damping - 0.95f);
        brightness += inf * (1.0f - brightness);
        df += inf * (0.499f - df);
        dc += inf * (128.0f - dc);
    }
    svf_set(&v->damp, df, 0.5f);
    {   /* the lowpass's delay, compensated: Plaits' lut_svf_shift */
        float shift = 1.0f - 2.0f * mv_atanf(1.0f / mv_semi(dc)) / FM_TWO_PI;
        v->delay_t = delay * shift;
    }
    nl = structure < 0.24f ? (structure - 0.24f) * 4.166f : structure > 0.26f ? (structure - 0.26f) * 1.35135f : 0.0f;
    v->nl = nl;
    nl = fm_fabsf(nl);
    v->stretch_pt = nl * (2.0f - nl) * 0.225f;
    v->stretch_corr = fm_clampf((160.0f / 44100.0f) * delay, 1.0f, 2.1f);
    {
        float n = nl > 0.75f ? 4.0f * (nl - 0.75f) : 0.0f;
        v->noise_amt = n * n * 0.1f;
    }
    v->noise_f = 0.06f + 0.94f * brightness * brightness;
    v->bridge = nl * nl * 0.01f;
    v->ap_g = -0.618f * nl / (0.15f + nl);
}

/* the knobs as each model takes them; the coefficients made again when they change */
static void mv_coef(mv_t *v, int sustain, int modes)
{
    const mv_par_t *p = &v->p;
    float b = p->timbre + 0.25f * p->accent * (1.0f - p->timbre), d = p->decay + 0.25f * p->accent * (1.0f - p->decay);
    switch (v->model) {
    case M_MODAL: {
        float range = sustain ? 36.0f : 60.0f, f = sustain ? 4.0f : 2.0f;
        svf_set(&v->exc, fm_minf(f * p->f0 * mv_semi((b * (2.0f - b) - 0.5f) * range), 0.499f), sustain ? 0.7f : 1.5f);
        modal_coef(v, p->color, b, d, 0.015f, modes);
        break;
    }
    case M_STRING:
        svf_set(&v->exc, fm_minf(4.0f * p->f0 * mv_semi((b * (2.0f - b) - 0.5f) * 72.0f), 0.499f), sustain ? 1.0f : 0.5f);
        string_coef(v, p->color, b, d);
        break;
    default:
        v->inc[0] = p->f0;
        break;
    }
}

static void mv_init(mv_t *v, float *dl, float *st)
{
    uint32_t i;
    for (i = 0; i < sizeof *v; i++)
        ((uint8_t *)v)[i] = 0;
    v->dl = dl;
    v->st = st;
    for (i = 0; i < MV_DL; i++)
        dl[i] = 0.0f;
    for (i = 0; i < MV_ST; i++)
        st[i] = 0.0f;
}

/* silence and forget (a new note on a free voice) */
static void mv_reset(mv_t *v, int model)
{
    int i;
    for (i = 0; i < MV_MODES; i++)
        v->s1[i] = v->s2[i] = 0.0f;
    v->nm = 0;
    v->exc.s1 = v->exc.s2 = v->damp.s1 = v->damp.s2 = v->lpg.s1 = v->lpg.s2 = 0.0f;
    if (v->model == M_STRING || model == M_STRING) {
        uint32_t k;
        for (k = 0; k < MV_DL; k++)
            v->dl[k] = 0.0f;
        for (k = 0; k < MV_ST; k++)
            v->st[k] = 0.0f;
    }
    v->dcx = v->dcy = v->disp = v->curved = 0.0f;
    v->vac = v->dec = v->level = v->quiet = 0.0f;
    v->ph[0] = v->ph[1] = 0.0f;
    v->model = (uint8_t)model;
    v->delay = 0.0f;
}

/* a strike: the pluck (STRING), the click (MODAL), the LPG's ping (the others) */
static void mv_strike(mv_t *v)
{
    const mv_par_t *p = &v->p;
    v->on = 1;
    v->quiet = 0.0f;
    if (v->model == M_MODAL) {
        float b = p->timbre + 0.25f * p->accent * (1.0f - p->timbre), d = p->decay + 0.25f * p->accent * (1.0f - p->decay);
        float cutoff = fm_minf(2.0f * p->f0 * mv_semi((b * (2.0f - b) - 0.5f) * 60.0f), 0.499f);
        v->imp += (0.12f + 0.08f * p->accent) * (1.0f - d * 0.5f) * mv_semi(cutoff * cutoff * 24.0f) / cutoff;
    } else if (v->model == M_STRING) {
        v->noise_n = (uint32_t)(1.0f / p->f0);
    } else {
        v->ramp = 1;
        v->dec = 1.0f;
    }
}

/* the LPG's control step, every MV_BS samples (Plaits LPGEnvelope, its block of 12 at 48 kHz as 16 here):
 * gate < 0: a ping (a pluck); else the vactrol follows gate (a held note) */
static void lpg_step(mv_t *v, float gate)
{
    const float bs = (float)MV_BS / 44100.0f, hf = 0.5f;
    float sd = 200.0f * bs * mv_semi(-96.0f * v->p.decay), tail = 20.0f * bs * mv_semi(-72.0f * v->p.decay + 12.0f * hf) - sd;
    float in, err, s2, s4, coef;
    if (gate < 0.0f) {
        if (v->ramp) {
            v->vac += fm_minf(v->p.f0 * (float)MV_BS * 2.0f, 1.0f) + 0.15f;
            if (v->vac >= 1.0f) {
                v->vac = 1.0f;
                v->ramp = 0;
            }
        }
        in = v->ramp ? v->vac : 0.0f;
    } else {
        in = gate;
    }
    v->dec *= 1.0f - sd * 2.0f;
    err = in - v->vac;
    s2 = v->vac * v->vac;
    s4 = s2 * s2;
    coef = err > 0.0f ? 0.6f : sd + (1.0f - s4) * tail;
    v->vac += coef * err;
    v->vac = fm_flush(v->vac);
    {
        float t = 1.0f - v->vac, t2 = t * t;
        v->lpg_g = v->vac;
        v->lpg_bleed = (t2 + (1.0f - t2) * hf) * hf * hf;
        svf_set(&v->lpg, 0.003f + 0.3f * s4 + hf * 0.04f, 0.4f);
    }
    v->level = v->vac;
}

/* the oscillator models, one sample */
static inline float osc_sample(mv_t *v)
{
    const mv_par_t *p = &v->p;
    float o;
    switch (v->model) {
    case M_ANALOG: {   /* saw -> square -> thin pulse; a second saw detuned */
        float dt = v->inc[0], ph = v->ph[0] + dt, saw, sq, t = p->timbre, pw, mix;
        if (ph >= 1.0f)
            ph -= 1.0f;
        v->ph[0] = ph;
        pw = t < 0.5f ? 0.5f : 0.5f + (t - 0.5f) * 0.9f;
        mix = t < 0.5f ? t * 2.0f : 1.0f;
        saw = 2.0f * ph - 1.0f - mv_blep(ph, dt);
        {
            float q = ph + 1.0f - pw;
            if (q >= 1.0f)
                q -= 1.0f;
            sq = (ph < pw ? 1.0f : -1.0f) + mv_blep(ph, dt) - mv_blep(q, dt);
        }
        o = saw + (sq - saw) * mix;
        if (p->color > 0.01f) {
            float d2 = v->inc[1], p2 = v->ph[1] + d2, s2;
            if (p2 >= 1.0f)
                p2 -= 1.0f;
            v->ph[1] = p2;
            s2 = 2.0f * p2 - 1.0f - mv_blep(p2, d2);
            o = (o + s2 * fm_minf(p->color * 4.0f, 1.0f)) * 0.7f;
        }
        return o * 0.5f;
    }
    case M_FM: {       /* Plaits FMEngine at the base rate: the modulator's index tamed in the highs */
        float ph = v->ph[0] + v->inc[0], pm = v->ph[1] + v->inc[1], m, c;
        if (ph >= 1.0f)
            ph -= 1.0f;
        if (pm >= 1.0f)
            pm -= 1.0f;
        v->ph[0] = ph;
        v->ph[1] = pm;
        m = mv_sin1(pm);
        c = mv_sin1(ph + v->amt * m);                  /* SinePM: the index in cycles */
        return c * 0.6f;
    }
    default: {         /* FOLD: Plaits WaveshapingEngine, its slope oscillator into the folder */
        float ph = v->ph[0] + v->inc[0], pw = 0.5f + p->color * 0.45f, tri;
        if (ph >= 1.0f)
            ph -= 1.0f;
        v->ph[0] = ph;
        tri = ph < pw ? 2.0f * ph / pw - 1.0f : 1.0f - 2.0f * (ph - pw) / (1.0f - pw);
        return mv_tab(mv_fold, tri * v->amt + 0.5f) * 0.5f;      /* amt: the folder's gain */
    }
    }
}

/* Plaits' Tame(): less of a shaper as the note rises, against aliasing */
static float mv_tame(float f0, float harmonics, float order)
{
    float mf = 0.5f / order, a;
    f0 *= harmonics;
    a = fm_clampf(1.0f - (f0 - mf) / (0.5f - mf), 0.0f, 1.0f);
    return a * a * a;
}

/* render n samples of a voice, added into out, its gain going from g0 to g1. gate < 0: plucked (the LPG
 * pinged); else the LPG held open at gate 0..1. dust > 0: a physical model excited continuously by Plaits'
 * dust (its unpatched-trigger sustain), the chord's bowed, blown sound. Returns 0 when it has gone quiet */
static int mv_render(mv_t *v, float gate, float dust, float g0, float g1, float *out, uint32_t n)
{
    uint32_t j;
    float peak = 0.0f, g = g0, dg = (g1 - g0) / (float)(n ? n : 1u);
    const mv_par_t *p = &v->p;
    if (!v->on)
        return 0;
    if (v->model == M_MODAL || v->model == M_STRING) {
        static float ex[256] MV_POOL;
        float dust_f = 0.0f, dust_a = 0.0f;
        int nb, b, any = 0;
        if (n > 256u)
            n = 256u;
        if (dust > 0.0f) {                 /* held: dust (Plaits' modal / string voice, sustain) */
            float dens = p->timbre * p->timbre;
            dust_f = 0.00005f + 0.99995f * dens * dens;
            dust_a = (v->model == M_MODAL ? 4.0f - dust_f * 3.0f : 8.0f - dust_f * 6.0f) * p->accent * dust * 0.5f;
        }
        for (j = 0; j < n; j++) {
            float x = v->imp;
            v->imp = 0.0f;
            if (v->noise_n) {
                v->noise_n--;
                x += 2.0f * mv_randf() - 1.0f;
            }
            if (dust_a > 0.0f)
                x += mv_dust(dust_f) * dust_a;
            if (x != 0.0f)
                any = 1;
            ex[j] = x;
        }
        if (any || fm_fabsf(v->exc.s1) + fm_fabsf(v->exc.s2) > 1e-7f) {
            for (j = 0; j < n; j++)
                ex[j] = svf_lp(&v->exc, ex[j]);
        } else {
            v->exc.s1 = v->exc.s2 = 0.0f;
        }
        if (v->model == M_MODAL) {
            nb = v->nm >> 2;
            for (b = 0; b < nb; b++) {     /* four modes at a time, across the block (Plaits' batches) */
                float *s1 = &v->s1[4 * b], *s2 = &v->s2[4 * b];
                const float *G = &v->g[4 * b], *RG = &v->rg[4 * b], *H = &v->h[4 * b], *A = &v->a[4 * b];
                float a1 = s1[0], a2 = s1[1], a3 = s1[2], a4 = s1[3], b1 = s2[0], b2 = s2[1], b3 = s2[2], b4 = s2[3];
                for (j = 0; j < n; j++) {
                    float in = ex[j], hp, bp, o = 0.0f;
#define MODE(k, s, t)                                                                                                   \
    hp = (in - RG[k] * s - t) * H[k];                                                                                   \
    bp = G[k] * hp + s;                                                                                                 \
    s = G[k] * hp + bp;                                                                                                 \
    t = G[k] * bp + t + G[k] * bp;                                                                                      \
    o += A[k] * bp;
                    MODE(0, a1, b1)
                    MODE(1, a2, b2)
                    MODE(2, a3, b3)
                    MODE(3, a4, b4)
#undef MODE
                    out[j] += o * (g0 + dg * (float)j);
                    peak = fm_maxf(peak, fm_fabsf(o));
                }
                s1[0] = fm_flush(a1);
                s1[1] = fm_flush(a2);
                s1[2] = fm_flush(a3);
                s1[3] = fm_flush(a4);
                s2[0] = fm_flush(b1);
                s2[1] = fm_flush(b2);
                s2[2] = fm_flush(b3);
                s2[3] = fm_flush(b4);
            }
        } else {
            float *dl = v->dl, *st = v->st;
            uint32_t dw = v->dw, sw = v->sw;
            float delay = v->delay > 0.0f ? v->delay : v->delay_t, dstep = (v->delay_t - delay) / (float)n;
            const int disp = v->nl > 0.0f;
            for (j = 0; j < n; j++) {
                float d = delay += dstep, s;
                if (disp) {
                    v->disp += v->noise_f * ((mv_randf() - 0.5f) - v->disp);
                    d *= 1.0f + v->disp * v->noise_amt;
                } else {
                    d *= 1.0f - v->curved * v->bridge;
                }
                {   /* the delay line, read d samples back */
                    float ap = d * v->stretch_pt, main = d - ap * (0.408f - v->stretch_pt * 0.308f) * v->stretch_corr;
                    if (disp && ap >= 4.0f && main >= 4.0f) {
                        int k = (int)main;
                        float f = main - (float)k, a = dl[(dw + (uint32_t)k) & (MV_DL - 1u)],
                              bb = dl[(dw + (uint32_t)k + 1u) & (MV_DL - 1u)], r, w;
                        s = a + (bb - a) * f;
                        k = (int)ap;                /* stmlib DelayLine::Allpass */
                        f = ap - (float)k;
                        a = st[(sw + (uint32_t)k) & (MV_ST - 1u)];
                        bb = st[(sw + (uint32_t)k + 1u) & (MV_ST - 1u)];
                        r = a + (bb - a) * f;
                        w = s + v->ap_g * r;
                        st[sw] = w;
                        sw = (sw - 1u) & (MV_ST - 1u);
                        s = -w * v->ap_g + r;
                    } else {                        /* Hermite */
                        int k = (int)d;
                        float f = d - (float)k, xm1 = dl[(dw + (uint32_t)k - 1u) & (MV_DL - 1u)],
                              x0 = dl[(dw + (uint32_t)k) & (MV_DL - 1u)], x1 = dl[(dw + (uint32_t)k + 1u) & (MV_DL - 1u)],
                              x2 = dl[(dw + (uint32_t)k + 2u) & (MV_DL - 1u)];
                        float c = (x1 - xm1) * 0.5f, vv = x0 - x1, w = c + vv, a = w + vv + (x2 - x0) * 0.5f, bn = w + a;
                        s = (((a * f) - bn) * f + c) * f + x0;
                    }
                }
                if (!disp) {                        /* the curved bridge */
                    float val = fm_fabsf(s) - 0.025f, sign = s > 0.0f ? 1.0f : -1.5f;
                    v->curved = (fm_fabsf(val) + val) * sign;
                }
                s = fm_clampf(s * v->loss + ex[j], -20.0f, 20.0f);
                {   /* DC blocker (stmlib DCBlocker, 20 Hz), the damping lowpass */
                    float y = s - v->dcx + (1.0f - 20.0f / 44100.0f) * v->dcy;
                    v->dcx = s;
                    v->dcy = fm_flush(y);
                    s = svf_lp(&v->damp, y);
                }
                dl[dw] = fm_flush(s);
                dw = (dw - 1u) & (MV_DL - 1u);
                out[j] += s * g;
                g += dg;
                peak = fm_maxf(peak, fm_fabsf(s));
            }
            v->delay = delay;
            v->dw = dw;
            v->sw = sw;
            v->damp.s1 = fm_flush(v->damp.s1);
            v->damp.s2 = fm_flush(v->damp.s2);
        }
        v->level = fm_minf(peak * 4.0f, 1.0f);
        if (peak < 8e-5f && !v->noise_n && v->imp == 0.0f && dust <= 0.0f) {
            v->quiet += (float)n;
            if (v->quiet > 2048.0f) {
                v->on = 0;
                return 0;
            }
        } else {
            v->quiet = 0.0f;
        }
        return 1;
    }
    /* ANALOG, FM, FOLD: the oscillator, then the low-pass gate */
    for (j = 0; j < n; j++) {
        float s, lp;
        if (!(j & (MV_BS - 1))) {
            lpg_step(v, gate);
            if (v->model == M_ANALOG) {
                v->inc[0] = p->f0;
                v->inc[1] = p->f0 * mv_semi(p->color * p->color * 0.35f + 0.03f);
            } else if (v->model == M_FM) {
                float ratio = MV_RATIO[(int)(p->color * 22.99f)], mn = 12.0f * fm_log2f(p->f0 * 44100.0f / 440.0f) + 69.0f +
                                                                     12.0f * fm_log2f(ratio),
                      tame = fm_clampf(1.0f - (mn - 84.0f) * 0.03f, 0.0f, 1.0f);
                v->inc[0] = p->f0;
                v->inc[1] = fm_minf(p->f0 * ratio, 0.45f);
                v->amt = 2.0f * p->timbre * p->timbre * tame * tame * (gate < 0.0f ? 0.35f + 0.65f * v->dec : 1.0f);
            } else {
                float slope = 3.0f + fm_fabsf(p->color - 0.5f) * 5.0f;
                v->inc[0] = p->f0;
                v->amt = 0.03f + 0.46f * p->timbre * mv_tame(p->f0, slope * 3.0f, 12.0f);
            }
        }
        s = osc_sample(v) * v->lpg_g;
        lp = svf_lp(&v->lpg, s);
        s = lp + (s - lp) * v->lpg_bleed;
        out[j] += s * g;
        g += dg;
    }
    v->lpg.s1 = fm_flush(v->lpg.s1);
    v->lpg.s2 = fm_flush(v->lpg.s2);
    if (gate <= 0.0f && !v->ramp && v->vac < 1e-4f) {
        v->on = 0;
        return 0;
    }
    return 1;
}
