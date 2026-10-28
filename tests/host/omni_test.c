/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument (dsp/omni.c) on its own: the strum plate's voicing against the OM-108 owner's
 * manual (p.49), the chord and bass registers, envelopes that end, the rhythm's timing, and an output
 * that stays finite and inside full scale with everything playing at once. Then the voices (each model
 * sounds and ends), the two tracks, the count-in and a loop of chords recorded and played back, and
 * that nothing ever plays a bass or a melody. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "omni.h"

static int bad, midi_n, bass_on_n, chord_on_n;
void om_midi_out(uint32_t st, uint32_t d1, uint32_t d2)
{
    (void)d1;
    (void)d2;
    midi_n++;
    if (st == 0x92)                                    /* channel 3 was the bass's: never now */
        bass_on_n++;
    if (st == 0x91)                                    /* the chord's notes */
        chord_on_n++;
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
    check("a chord button held: the chord sounds", om_chord_level > 0.9f);
    om_gate(0);
    run(44100 / 256);
    check("let go: it ends within 1 s", om_chord_level == 0.0f);

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
    {   /* every voice: a pluck sounds and ends; a held chord sounds and ends when let go */
        int m;
        for (m = 0; m < OM_NMODELS; m++) {
            char what[80];
            om_init();
            om_set(P_HMODEL, m);
            om_set(P_CMODEL, m);
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
            snprintf(what, sizeof what, "%s: a held chord sounds", OM_MODEL_NAME[m]);
            check(what, om_chord_level > 0.9f && peak > 0.05f);
            om_gate(0);
            run(44100 / 256 * 3);
            peak = 0.0f;
            run(44100 / 256);
            snprintf(what, sizeof what, "%s: let go, quiet within 3 s; all finite", OM_MODEL_NAME[m]);
            check(what, om_chord_level == 0.0f && peak < 0.01f && finite_ok);
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
        check("... a chord held by hand sounds", om_chord_level > 0.9f);
        om_gate(0);
        om_play(0);
        run(2);
        check("both off: the clock stops", !om_playing);
    }
    {   /* a loop: four beats counted in (2 s at 120: a click each, nothing else), then one bar of C, then Am
         * from the bar's middle; played back with no button held, the chords held in turn; a D held over it
         * sounds instead; overdubbed, the loop is D */
        static uint8_t lp[OM_LOOP_MAX], ll;
        int b, seen_c = 0, seen_a = 0, held = 1, beats = 0, last = 0, steps_during = 0;
        om_init();
        om_link(lp, &ll);
        om_set(P_TEMPO, 120);
        om_set(P_BARS, 0);
        om_play(1);                                     /* (the drums on: they wait for the count) */
        run(2);
        om_play(0);
        run(2);
        om_chord(0, CH_MAJ);
        om_gate(1);
        om_drum_hit = 0;
        om_rec(OM_REC_START);
        run(2);
        check("REC, stopped: counting in, the sequence on", om_seq_on && om_rec_state == 4 && om_count == 1);
        for (b = 0; b < 2 * 44100 / 256 - 4 && om_rec_state == 4; b++) {
            run(1);
            if (om_count != last) {
                beats++;
                last = om_count;
            }
            steps_during |= om_step != 0;
        }
        /* the fourth beat's last sixteenth begins at 1.875 s; the top is at 2 s */
        check("... four beats (1, 2, 3, 4), the last beginning at 1.5 s", beats == 4 && last == 4 &&
              b * 256 > 44100 * 18 / 10 - 512 && b * 256 < 44100 * 19 / 10);
        check("... no step of the rhythm while it counts", !steps_during);
        for (b = 0; b < 40 && om_rec_state != 2; b++)
            run(1);
        check("... then recording, from the top, at 2 s", om_rec_state == 2 && om_loop_pos == 0 && b > 2 && b < 30);
        run(44100 / 256 - 8);                           /* 1 s: half the bar */
        om_chord(9, CH_MIN);
        om_gate(1);
        run(44100 / 256 + 10);
        check("after the bar: a loop of 16 steps", ll == 16 && om_rec_state == 0);
        om_gate(0);
        run(4);
        for (b = 0; b < 2 * 44100 / 256; b++) {
            run(1);
            seen_c |= om_root == 0 && om_loop_pos < 7;
            seen_a |= om_root == 9 && om_loop_pos >= 9;
            held &= om_chord_level > 0.5f;
        }
        check("played back: C, then Am, with no button held", seen_c && seen_a && om_gen);
        check("... each chord held all the while", held);
        {   /* REC again, playing: armed for the next bar, then four beats counted over the loop */
            int counted = 0;
            om_rec(OM_REC_START);
            run(2);
            check("REC, playing: waiting for the bar", om_rec_state == 1);
            for (b = 0; b < 5 * 44100 / 256 && om_rec_state != 2; b++) {   /* (the bar: up to 2 s; the count: 2 s) */
                run(1);
                counted |= om_rec_state == 4;
            }
            check("... counted in, then recording", counted && om_rec_state == 2);
            om_rec(OM_REC_STOP);
            run(2);
        }
        om_chord(2, CH_MAJ);
        om_gate(1);
        run(10);
        check("a D held over the loop sounds instead", om_root == 2 && om_chord_level > 0.5f);
        om_rec(OM_REC_OVERDUB);                         /* overdub, D held for a whole pass: the loop is D */
        run(2);
        check("OVERDUB: recording over the loop playing", om_rec_state == 3);
        for (b = 0; b < 3 * 44100 / 256 && om_rec_state; b++)
            run(1);
        check("... once round, then it plays", om_rec_state == 0 && ll == 16);
        om_gate(0);
        {
            int all_d = 1;
            for (b = 0; b < 2 * 44100 / 256; b++) {
                run(1);
                all_d &= om_root == 2 && om_chord_level > 0.5f;
            }
            check("... no button held: the loop is D now, held", all_d);
        }
        om_restart();
        run(1);
        check("restart: the top of the loop", om_loop_pos <= 1);
        om_rec(OM_REC_CLEAR);
        run(2);
        check("REC held (clear): no loop", ll == 0);
        om_seq(0);
        run(10);
        check("SEQ off: the chord it held lets go", om_chord_level < 0.9f);
        run(2);
    }
    {   /* the kits: each plays the rhythm's drums, finite, within a few dB of the samples, and goes quiet */
        double ref = 0.0;
        int k;
        for (k = 0; k < 3; k++) {
            double e = 0.0;
            int b, i, quiet = 1;
            char what[80];
            om_init();
            om_set(P_KIT, k);
            om_set(P_TEMPO, 120);
            om_set(P_REVERB, 0);
            om_play(1);
            finite_ok = 1;
            for (b = 0; b < 4 * 44100 / 256; b++) {
                run(1);
                for (i = 0; i < 512; i++)
                    e += (double)buf[i] * (double)buf[i];
            }
            if (!k)
                ref = e;
            om_play(0);
            run(44100 / 256 * 3);
            run(1);
            for (i = 0; i < 512; i++)
                quiet &= buf[i] == 0;
            snprintf(what, sizeof what, "kit %s: drums, finite, level within 3 dB, quiet after", OM_KIT_NAME[k]);
            check(what, e > 0.0 && finite_ok && e < ref * 2.0 && e > ref * 0.5 && quiet);
        }
    }
    check("no bass, ever: nothing on channel 3", bass_on_n == 0 && chord_on_n > 10);
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
