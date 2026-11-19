/* F7a: how the melody's rhythm sits with the break and the harmony, each style (240 s, the form off, an amen-like
 * pattern: kicks 0 10 16 26, snares 4 12 20 28, hats between):
 *   long on snare  its held notes (2+ of its 16ths) starting on a snare's 16th (a lead landing on the backbeat)
 *   in threes      its attacks in a chain of three dotted spacings or more (3 3 3, 6 6 6 of its 16ths: a hemiola across
 *                  the bar; the tresillo, 3 3 2, closes inside it and is not counted)
 *   with the harm. its attacks with a full harmony strike on the same 16th (one stepped back by half not counted),
 *                  the bars' downbeats left out (the two crossing)
 *   sh tests/rhythm.sh */
#include "music.c"
#include <stdio.h>
int jungle_playing(void) { return 1; }
void jungle_pattern(struct jungle_step out[JUNGLE_STEPS])
{
    for (int i = 0; i < JUNGLE_STEPS; ++i) {
        int s = i % 16, kick = s == 0 || s == 10, snare = s == 4 || s == 12;
        out[i].active = 1; out[i].slice = (uint8_t)(kick ? 0 : snare ? 1 : 6); out[i].stretch = 0; out[i].pitch = 0; out[i].flip = 0;
    }
}
int jungle_roll_active(void) { return -1; }
void jungle_request_fill(unsigned k, unsigned l) { (void)k; (void)l; }
void jungle_swing(unsigned t) { (void)t; }
int main(int argc, char **argv)
{
    int style = atoi(argv[1]), secs = argc > 2 ? atoi(argv[2]) : 240;
    static int32_t lr[512];
    music_init();
    music_style((unsigned)style);
    music_snares(1u << 1);
    music_mute(TRK_MELODY, 0, 0); music_mute(TRK_HARMONY, 0, 0);
    double sps = 44100.0 * 60.0 / 170.0 / 4.0, next = 0; unsigned step = 0;
    long frames = (long)secs * 44100, f = 0;
    float pg = 0.0f, pe[3] = { 0, 0, 0 }; int ps[3] = { 0, 0, 0 };
    long mel = 0, snare = 0, off8 = 0, both = 0, nlong = 0, nheld = 0, prev_on = -1; int prev_ioi = 0, run = 0;
    float harm_vel = 0.0f;
    long harm_at = -1000000;   /* the last harmony strike, in samples */
    long mel_at[4096]; int nm = 0;
    while (f < frames) {
        while (next < f + 256) { music_step(step++ % 32, (unsigned)(next - f)); next += sps; }
        memset(lr, 0, sizeof lr);
        /* render sample by sample (the attacks' times) */
        for (int i = 0; i < 256; ++i) {
            music_render(lr + 2 * i, 1);
            long t = f + i;
            for (int k = 0; k < 3; ++k) {   /* a strike: its stage back to the attack, or its envelope dropped to start again */
                const struct voice *h = &rhy.v[k];
                if (h->stage == 1 && (ps[k] != 1 || h->env < pe[k] - 0.05f)) { harm_at = t; harm_vel = h->vel; }
                ps[k] = h->stage; pe[k] = h->env;
            }
            float gl = lead.gate_left;
            if (gl > pg + 1.0f && t > 44100 * 8) {   /* an attack of the melody */
                double pos = (double)t / sps;                     /* in 16ths */
                long s16 = (long)(pos + 0.5); double fr = pos - (double)s16;
                int st = (int)(s16 % 16);
                double u16 = sps * 2.0 / (double)feel[TRK_MELODY];          /* one of its 16ths, in samples */
                double len = (double)gl / (0.9 * u16);                         /* its length, in its 16ths */
                ++mel;
                if (len >= 1.8) { ++nheld; if (st == 4 || st == 12) ++snare; }
                if (prev_on >= 0) {   /* the spacing from the last attack, in its 16ths: a chain of threes? */
                    int ioi = (int)((double)(t - prev_on) / u16 + 0.5);
                    run = (ioi == 3 || ioi == 6) && ioi == prev_ioi ? run + 1 : (ioi == 3 || ioi == 6);
                    if (run >= 3) ++off8;
                    prev_ioi = ioi;
                }
                prev_on = t; ++nlong;
                if (nm < 4096) mel_at[nm++] = t;
            }
            pg = gl;
            /* a harmony strike within half a 16th of the melody's last attack, not on a bar's downbeat */
            if (nm && harm_at == t) {
                long d = t - mel_at[nm - 1];
                long s16 = (long)((double)mel_at[nm - 1] / sps + 0.5);
                if (d >= 0 && d < (long)(sps * 0.5) && s16 % 16 != 0 && harm_vel > 0.45f) ++both;
            }
        }
        f += 256;
    }
    printf("%-9s attacks %4ld | long on snare %4.1f%% | in threes %4.1f%% | with the harmony %4.1f%%\n",
           STYLE[style].name, mel, 100.0 * snare / (nheld ? nheld : 1), 100.0 * off8 / (nlong ? nlong : 1), 100.0 * both / (mel ? mel : 1));
    return 0;
}
