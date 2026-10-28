/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument (dsp/omni.c) on its own: the strum plate's voicing against the OM-108 owner's
 * manual (p.49), the chord and bass registers, envelopes that end, the rhythm's timing, and an output
 * that stays finite and inside full scale with everything playing at once. Then the voices (each model
 * sounds and ends), the two tracks, a loop recorded and played back, and the Markov chains. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "omni.h"

static int bad, midi_n, bass_on_n, last_bass = -1;
void om_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    (void)d2;
    midi_n++;
    if (st == 0x92) {                                  /* the bass's notes */
        bass_on_n++;
        last_bass = (int)d1;
    }
}

static void check(const char *what, int ok)
{
    printf("%-58s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static int32_t buf[2 * 256];
static float peak;
static int finite_ok = 1;
static void run(int blocks)
{
    int b, i;
    for (b = 0; b < blocks; b++) {
        om_render(buf, 256, 4096);
        for (i = 0; i < 512; i++) {
            float x = (float)buf[i] / 8388608.0f;
            if (!(x == x))
                finite_ok = 0;
            if (fabsf(x) > peak)
                peak = fabsf(x);
        }
    }
}

int main(void)
{
    uint8_t h[OM_NSTR], c[OM_NCHORD], bass;
    /* the manual's C major, zones 1..13: C4 E4 G3 | C5 E5 G4 | C6 E6 G5 | C7 E7 G6 | C8. OMNI's 16 keys
     * add a group below (octave 0), so the manual's zones are OMNI's strings 3..15 */
    static const uint8_t C_OM[13] = {60, 64, 55, 72, 76, 67, 84, 88, 79, 96, 100, 91, 108};
    om_voicing(0, CH_MAJ, 0, 0, h, c, &bass);
    check("C major: strings 3..15 are the OM-108's 13 zones", !memcmp(h + 3, C_OM, 13));
    check("C major: strings 0..2 are the group below (C3 E3 G2)", h[0] == 48 && h[1] == 52 && h[2] == 43);
    check("C major: chord C4 E4 G3 (the F#3..F4 window), bass C2", c[0] == 60 && c[1] == 64 && c[2] == 55 && bass == 36);
    om_voicing(7, CH_MAJ, 0, 0, h, c, &bass);
    check("G major: zone 1 is G3 B3 D4 (manual), bass G2", c[0] == 55 && c[1] == 59 && c[2] == 62 && bass == 43);
    om_voicing(0, CH_7, 0, 0, h, c, &bass);
    check("C7 is a triad without its fifth: C E Bb", c[0] == 60 && c[1] == 64 && c[2] == 58);
    om_voicing(0, CH_MAJ, 2, 1, h, c, &bass);
    check("transpose +2, octave +1: D, a group up", h[0] == 62 && c[0] == 62 && bass == 38);
    {
        int i, ok = 1;
        for (i = 0; i < 12 && ok; i++) {
            int t;
            for (t = 0; t < CH_NTYPES; t++) {
                int k;
                om_voicing(i, t, 0, 0, h, c, &bass);
                for (k = 0; k < OM_NSTR; k++)
                    ok &= h[k] >= 42 && h[k] <= 113;
                for (k = 0; k < OM_NCHORD; k++)
                    ok &= c[k] >= 54 && c[k] <= 65;
                ok &= bass >= 36 && bass <= 47;
            }
        }
        check("every chord: strings F#2..F8, chord F#3..F4, bass C2..B2", ok);
    }

    om_init();
    om_chord(0, CH_MAJ);
    om_strum(0);
    run(2);
    check("a pluck sounds", om_str_level[0] > 0.5f);
    run(44100 / 256 * 8);
    check("a pluck ends (sustain 50: within 8 s)", om_str_level[0] == 0.0f);
    om_set(P_SUSTAIN, 0);
    om_strum(5);
    run(44100 / 256);
    check("sustain 0: gone within 1 s", om_str_level[5] == 0.0f);

    om_gate(1);
    run(10);
    check("a chord button held: the chord and its bass sound", om_chord_level > 0.9f && om_bass_level > 0.9f);
    om_gate(0);
    run(44100 / 256);
    check("let go: both end within 1 s", om_chord_level == 0.0f && om_bass_level == 0.0f);

    {   /* the rhythm: Rock 1 at 120 BPM is a 16th every 5512.5 samples; 32 steps in 4 s */
        int n = 0, last = -1, b;
        om_set(P_TEMPO, 120);
        om_set(P_RHYTHM, 0);
        om_play(1);
        for (b = 0; b < 4 * 44100 / 256; b++) {
            run(1);
            if (om_step != last) {
                n++;
                last = om_step;
            }
        }
        check("Rock 1 at 120: 32 steps (+-1) in 4 s", n >= 31 && n <= 33);
        {   /* swing 50%: the even steps half as long again, the odd ones half; a pair as long as before */
            int t0 = 0, t1 = 0, t2 = 0, k;
            om_set(P_SWING, 50);
            last = om_step;
            for (k = 0; k < 4000 && om_step == last; k++)
                run(1);
            last = om_step;
            for (k = 0; k < 4000 && om_step == last; k++, t0++)
                run(1);
            last = om_step;
            for (k = 0; k < 4000 && om_step == last; k++, t1++)
                run(1);
            last = om_step;
            for (k = 0; k < 4000 && om_step == last; k++, t2++)
                run(1);
            check("swing 50: steps alternate long and short", (t0 > 2 * t1 && t2 > 2 * t1) || (t1 > 2 * t0 && t1 > 2 * t2));
            om_set(P_SWING, 0);
        }
        om_play(0);
        run(2);
        check("stop", !om_playing);
    }
    {   /* everything at once, levels up: finite and inside full scale */
        int i, s;
        om_set(P_VOICE1, 100);
        om_set(P_VOICE2, 100);
        om_set(P_CHORD, 100);
        om_set(P_RHYVOL, 100);
        om_set(P_REVERB, 100);
        om_set(P_SPACE, 100);
        om_set(P_SUSTAIN, 100);
        om_gate(1);
        om_play(1);
        peak = 0.0f;
        for (i = 0; i < 8; i++)
            for (s = 0; s < OM_NSTR; s++) {
                om_strum(s);
                run(1);
            }
        run(44100 / 256 * 3);
        check("all at full: finite output", finite_ok);
        check("all at full: below full scale (the soft clip)", peak < 1.0f && peak > 0.3f);
        printf("  (peak %.2f)\n", (double)peak);
    }
    {   /* the plate: a tail that ends at the longest Space, and stays finite at full send */
        int b, i;
        double e0 = 0.0, e = 0.0;
        om_play(0);
        om_gate(0);
        run(1);                                        /* (om_init empties the queue) */
        om_init();
        om_set(P_REVERB, 100);
        om_set(P_SPACE, 100);
        om_set(P_SUSTAIN, 0);
        om_strum(8);
        for (b = 0; b < 44100 * 20 / 256; b++) {
            om_render(buf, 256, 4096);
            e = 0.0;
            for (i = 0; i < 512; i++)
                e += (double)buf[i] * (double)buf[i];
            if (b == 60)
                e0 = e;
        }
        check("plate at Space 100: the tail is 60 dB down within 20 s", e < e0 * 1e-6);
    }
    {   /* every voice: a pluck sounds and ends; a held chord and bass sound and end when let go */
        int m;
        for (m = 0; m < OM_NMODELS; m++) {
            char what[80];
            om_init();
            om_set(P_HMODEL, m);
            om_set(P_CMODEL, m);
            om_set(P_BMODEL, m);
            om_set(P_SUSTAIN, 30);
            om_chord(0, CH_MAJ);
            om_strum(6);
            peak = 0.0f;
            finite_ok = 1;
            run(4);
            snprintf(what, sizeof what, "%s: a pluck sounds", OM_MODEL_NAME[m]);
            check(what, om_str_level[6] > 0.05f && peak > 0.02f);
            run(44100 / 256 * 4);
            snprintf(what, sizeof what, "%s: ... and ends within 4 s (sustain 30)", OM_MODEL_NAME[m]);
            check(what, om_str_level[6] == 0.0f);
            om_gate(1);
            peak = 0.0f;
            run(44100 / 256);
            snprintf(what, sizeof what, "%s: a held chord and bass sound", OM_MODEL_NAME[m]);
            check(what, om_chord_level > 0.9f && om_bass_level > 0.9f && peak > 0.05f);
            om_gate(0);
            run(44100 / 256 * 3);
            peak = 0.0f;
            run(44100 / 256);
            snprintf(what, sizeof what, "%s: let go, quiet within 3 s; all finite", OM_MODEL_NAME[m]);
            check(what, om_chord_level == 0.0f && om_bass_level == 0.0f && peak < 0.01f && finite_ok);
        }
    }
    {   /* a strum across all sixteen strings with the pool at its smallest: the newest ring */
        int s, ringing = 0;
        om_init();
        om_poly(2);
        for (s = 0; s < OM_NSTR; s++) {
            om_strum(s);
            run(2);
        }
        for (s = 0; s < OM_NSTR; s++)
            ringing += om_str_level[s] > 0.0f;
        check("poly 2: two strings ring, the last two plucked", ringing == 2 && om_str_level[15] > 0.0f &&
              om_str_level[14] > 0.0f);
    }
    {   /* the tracks: each on its own; the clock runs while either does */
        uint8_t hits;
        om_init();
        om_set(P_TEMPO, 120);
        om_seq(1);
        run(2);
        check("SEQ alone: the clock runs, the drums do not", om_playing && om_seq_on && !om_drums_on);
        om_drum_hit = 0;
        run(44100 / 256);
        hits = om_drum_hit;
        check("... no drum hits", hits == 0);
        om_play(1);
        run(44100 / 256);
        check("PLAY too: the drums join", om_drums_on && om_drum_hit != 0);
        om_seq(0);
        run(2);
        check("SEQ off: the drums go on", om_playing && om_drums_on && !om_seq_on);
        om_gate(1);
        run(10);
        check("... a chord held by hand sounds (no pattern)", om_chord_level > 0.9f && om_bass_level > 0.9f);
        om_gate(0);
        om_play(0);
        run(2);
        check("both off: the clock stops", !om_playing);
    }
    {   /* a loop: one bar (16 steps at 120: 2 s) of C, then Am from the bar's middle; played back with no
         * button held; a D held over it moves its bass up a tone */
        static uint8_t lp[OM_LOOP_MAX * 3], ll, rt[OM_NPADS] = {5, 0, 7, 2, 9, 4, 7, 4, 2, 10, 9},
                                                ty[OM_NPADS] = {0, 0, 0, 1, 1, 1, 2, 2, 2, 0, 2}, lr[OM_NPADS * OM_NPADS];
        int b, seen_c = 0, seen_a = 0, bass_c = 0;
        om_init();
        om_link(rt, ty, lr, lp, &ll);
        om_set(P_TEMPO, 120);
        om_set(P_BARS, 0);
        om_chord(0, CH_MAJ);
        om_gate(1);
        om_rec(OM_REC_START);
        run(2);
        check("REC, stopped: the sequence starts, recording", om_seq_on && om_rec_state == 2);
        run(44100 / 256);                               /* 1 s: half the bar */
        om_chord(9, CH_MIN);
        run(44100 / 256 + 10);
        check("after the bar: a loop of 16 steps, the bass's source LOOP", ll == 16 && om_bsrc == SRC_LOOP &&
              om_rec_state == 0);
        om_gate(0);
        bass_on_n = 0;
        for (b = 0; b < 2 * 44100 / 256; b++) {
            run(1);
            seen_c |= om_root == 0 && om_loop_pos < 7;
            seen_a |= om_root == 9 && om_loop_pos >= 9;
            if (om_root == 0 && last_bass >= 0 && last_bass % 12 == 0)
                bass_c = 1;
        }
        check("played back: C, then Am, with no button held", seen_c && seen_a && om_gen);
        check("... and its bass (Rock 1: three notes a bar)", bass_on_n >= 3 && bass_c);
        om_chord(2, CH_MAJ);
        om_gate(1);
        last_bass = -1;
        for (b = 0; b < 44100 / 256 && last_bass < 0; b++)
            run(1);
        check("a D held over the loop: its bass moves to D", last_bass >= 0 && last_bass % 12 == 2);
        om_rec(OM_REC_OVERDUB);                         /* overdub, D held for a whole pass: the loop is in D */
        run(2);
        check("OVERDUB: recording over the loop playing", om_rec_state == 3);
        for (b = 0; b < 3 * 44100 / 256 && om_rec_state; b++)
            run(1);
        check("... once round, then it plays", om_rec_state == 0 && ll == 16 && om_bsrc == SRC_LOOP);
        om_gate(0);
        {
            int all_d = 1, n = 0;
            for (b = 0; b < 2 * 44100 / 256; b++) {
                int lb = last_bass;
                run(1);
                all_d &= om_root == 2;
                if (last_bass != lb && (om_loop_pos <= 1 || (om_loop_pos >= 6 && om_loop_pos <= 7))) {
                    n++;
                    all_d &= last_bass % 12 == 2;
                }
            }
            check("... no button held: the loop is in D now, chord and bass", n > 0 && all_d);
        }
        om_restart();
        run(1);
        check("restart: the top of the loop", om_loop_pos <= 1);
        om_rec(OM_REC_CLEAR);
        run(2);
        check("REC held (clear): no loop, the source back to RHYTHM", ll == 0 && om_bsrc == SRC_RHYTHM);
        om_seq(0);
        run(2);
    }
    {   /* Markov: the probabilities, what is learnt, and the changes it makes */
        static uint8_t lp[OM_LOOP_MAX * 3], ll, rt[OM_NPADS] = {5, 0, 7, 2, 9, 4, 7, 4, 2, 10, 9},
                                                ty[OM_NPADS] = {0, 0, 0, 1, 1, 1, 2, 2, 2, 0, 2}, lr[OM_NPADS * OM_NPADS];
        float p[OM_NPADS], q2[OM_NPADS], sum = 0.0f;
        int i, b, changes = 0, last = -1, s, plucks = 0;
        om_init();
        om_link(rt, ty, lr, lp, &ll);
        om_markov_probs(1, 30, p);
        for (i = 0; i < OM_NPADS; i++)
            sum += p[i];
        check("Markov: from C the probabilities sum to 1", fabsf(sum - 1.0f) < 1e-4f);
        check("... G (up a fifth) and F (down a fifth) the likeliest, C itself unlikely",
              p[2] > p[4] && p[0] > p[4] && p[1] < p[2] * 0.2f);
        lr[1 * OM_NPADS + 10] = 20;                     /* C -> A7, played twenty times */
        om_markov_probs(1, 30, q2);
        check("... a change played often grows likelier", q2[10] > 3.0f * p[10]);
        om_markov_probs(1, 100, q2);
        check("... chaos 100: nearer even", q2[10] < 0.5f && q2[1] > p[1]);
        lr[1 * OM_NPADS + 10] = 0;
        om_set(P_TEMPO, 240);
        om_set(P_CHANGES, 1);
        om_set(P_BSRC, SRC_MARKOV);
        om_set(P_ARP, 100);
        om_seq(1);
        bass_on_n = 0;
        for (b = 0; b < 8 * 44100 / 256; b++) {
            run(1);
            if (om_gen_pad != last) {
                changes++;
                last = om_gen_pad;
            }
            for (s = 0; s < OM_NSTR; s++)
                plucks += om_str_level[s] > 0.5f;
        }
        check("CHANGES every bar, no button held: the chord changes", changes >= 6 && om_gen);
        check("... the Markov bass plays", bass_on_n > 10);
        check("... the arp plucks the strings", plucks > 100);
        om_set(P_ARPRANGE, 4);                          /* the arp's span: the middle four strings */
        om_set(P_HMODEL, M_ANALOG);
        run(44100 / 256 * 2);
        plucks = 0;
        for (b = 0; b < 2 * 44100 / 256; b++) {
            run(1);
            for (s = 0; s < OM_NSTR; s++)
                if (om_str_level[s] > 0.9f && (s < 6 || s > 9))
                    plucks++;
        }
        check("Arp range 4: only strings 6..9 plucked", plucks == 0);
        om_seq(0);
        run(2);
        check("SEQ off: Markov stops", om_gen_pad < 0 && !om_gen);
    }
    check("MIDI out sent notes", midi_n > 100);
    om_set(P_MIDI, 0);
    run(1);
    midi_n = 0;
    om_strum(3);
    run(2);
    check("MIDI out off: nothing sent", midi_n == 0);
    printf("%s\n", bad ? "OMNI TEST FAILED" : "omni test passed");
    return bad != 0;
}
