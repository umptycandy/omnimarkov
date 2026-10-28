/* SPDX-License-Identifier: GPL-3.0-only */
/* The synthesized drum kits, beside the OM-84's samples (om_drums.h). The rhythms' five sounds (dsp/om_data.h:
 * bass drum, claves, closed hat, ride, snare), made two ways:
 *
 *   808   after Roland's TR-808: the bass drum a sine with a falling pitch; the snare two tones and noise;
 *         the hat and the cymbal the 808's six square oscillators (205.3..800 Hz), high-passed; the claves a
 *         2.5 kHz ping.
 *   CHIP  a game console's: the bass drum a square sweeping down; the snare, the hat and the ride the NES's
 *         15-bit noise (the hat its short, metallic mode); the claves a two-note blip; all at 4 bits.
 *
 * Included once, by omni.c (after fastmath.h and models.h: mv_sin1, mv_randf). Rates per sample at 44.1 kHz. */
#pragma once
#include <stdint.h>

#include "omni.h"                      /* the kits: KIT_* */

typedef struct {
    uint8_t on;
    float env, env2, pe;               /* the level, a second (the snare's noise), the pitch's envelope */
    float ph[6], lp1, lp2;             /* oscillators' phases; the high-pass filters' states */
    float hold, nz;                    /* chip noise: the clock's phase, the value held */
    uint32_t lfsr;                     /* the NES's noise register */
    uint32_t t;                        /* samples since the hit */
} ds_t;

/* the decay of an envelope that falls to 1/e in tau seconds, per sample */
#define DS_K(tau) (1.0f - 1.0f / ((tau) * 44100.0f))

static void ds_hit(ds_t *d, int slot)
{
    int i;
    d->on = 1;
    d->env = d->pe = 1.0f;
    d->env2 = slot == 4 ? 1.0f : 0.0f;              /* (only the snare has a second envelope) */
    for (i = 0; i < 6; i++)
        d->ph[i] = 0.0f;
    d->hold = 1.0f;
    d->t = 0;
    if (!d->lfsr)
        d->lfsr = 1u;
}

static inline float ds_sq(float ph) { return ph < 0.5f ? 1.0f : -1.0f; }
static inline float ds_adv(float ph, float hz)
{
    ph += hz * (1.0f / 44100.0f);
    return ph >= 1.0f ? ph - 1.0f : ph;
}
/* the NES's noise: 15 bits, fed back from bit 0 with bit 1 (long) or bit 6 (short, metallic) */
static inline float ds_lfsr(ds_t *d, float clock_hz, int short_mode)
{
    d->hold += clock_hz * (1.0f / 44100.0f);
    while (d->hold >= 1.0f) {
        uint32_t r = d->lfsr, fb = (r ^ (r >> (short_mode ? 6 : 1))) & 1u;
        d->hold -= 1.0f;
        d->lfsr = (r >> 1) | (fb << 14);
        d->nz = (d->lfsr & 1u) ? 1.0f : -1.0f;
    }
    return d->nz;
}
/* the 808's metallic source: six squares, a band of inharmonic partials */
static inline float ds_metal(ds_t *d)
{
    static const float HZ[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
    float s = 0.0f;
    int i;
    for (i = 0; i < 6; i++) {
        d->ph[i] = ds_adv(d->ph[i], HZ[i] * 1.8f);
        s += ds_sq(d->ph[i]);
    }
    return s * (1.0f / 6.0f);
}
/* two one-pole high-passes, about 6 kHz: the hat's and the cymbal's sizzle */
static inline float ds_hp(ds_t *d, float x)
{
    float y;
    d->lp1 += 0.55f * (x - d->lp1);
    y = x - d->lp1;
    d->lp2 += 0.55f * (y - d->lp2);
    return y - d->lp2;
}

/* one sample of drum `slot` (OM_BD, OM_CLICK, OM_HH, OM_RIDE, OM_SD) of a kit; ends itself when quiet */
static float ds_sample(ds_t *d, int kit, int slot)
{
    float o = 0.0f;
    d->t++;
    if (kit == KIT_808) {
        switch (slot) {
        case 0:                        /* bass drum: 48 Hz, from 160 Hz down in 30 ms; half a second */
            d->pe *= DS_K(0.03f);
            d->ph[0] = ds_adv(d->ph[0], 48.0f + 112.0f * d->pe);
            d->env *= DS_K(0.45f);
            o = mv_sin1(d->ph[0]) * d->env * 1.6f;
            o = o / (1.0f + fm_fabsf(o) * 0.45f);
            break;
        case 1:                        /* claves: a 2.5 kHz ping */
            d->ph[0] = ds_adv(d->ph[0], 2450.0f);
            d->env *= DS_K(0.025f);
            o = mv_sin1(d->ph[0]) * d->env * 0.8f;
            break;
        case 2:                        /* closed hat */
            d->env *= DS_K(0.04f);
            o = ds_hp(d, ds_metal(d)) * d->env * 1.3f;
            break;
        case 3:                        /* cymbal */
            d->env *= DS_K(0.4f);
            o = ds_hp(d, ds_metal(d) + (mv_randf() - 0.5f) * 0.3f) * d->env * 0.7f;
            break;
        default:                       /* snare: 185 and 330 Hz, and a brighter, longer noise */
            d->ph[0] = ds_adv(d->ph[0], 185.0f);
            d->ph[1] = ds_adv(d->ph[1], 330.0f);
            d->env *= DS_K(0.08f);
            d->env2 *= DS_K(0.15f);
            o = (mv_sin1(d->ph[0]) * 0.6f + mv_sin1(d->ph[1]) * 0.4f) * d->env * 0.8f +
                ds_hp(d, mv_randf() * 2.0f - 1.0f) * d->env2 * 0.9f;
            break;
        }
    } else {
        switch (slot) {
        case 0:                        /* bass drum: a square from 200 Hz down to 40 */
            d->pe *= DS_K(0.05f);
            d->ph[0] = ds_adv(d->ph[0], 40.0f + 160.0f * d->pe);
            d->env *= DS_K(0.15f);
            o = ds_sq(d->ph[0]) * d->env * 0.8f;
            break;
        case 1:                        /* claves: a blip, 1.6 kHz then 2.4 */
            d->ph[0] = ds_adv(d->ph[0], d->t < 660u ? 1600.0f : 2400.0f);
            d->env *= DS_K(0.035f);
            o = ds_sq(d->ph[0]) * d->env * 0.45f;
            break;
        case 2:                        /* hat: the short, metallic noise */
            d->env *= DS_K(0.04f);
            o = ds_lfsr(d, 22000.0f, 1) * d->env * 0.5f;
            break;
        case 3:                        /* ride: long noise, high */
            d->env *= DS_K(0.35f);
            o = ds_lfsr(d, 15000.0f, 0) * d->env * 0.3f;
            break;
        default:                       /* snare: noise, and a short square under it */
            d->ph[0] = ds_adv(d->ph[0], 180.0f);
            d->env *= DS_K(0.12f);
            d->env2 *= DS_K(0.04f);
            o = ds_lfsr(d, 7000.0f, 0) * d->env * 0.65f + ds_sq(d->ph[0]) * d->env2 * 0.4f;
            break;
        }
        o = (float)(int)(o * 7.5f) * (1.0f / 7.5f);   /* 4 bits */
    }
    if (d->env < 1e-3f && d->env2 < 1e-3f)              /* -60 dB: done */
        d->on = 0;
    return o;
}
