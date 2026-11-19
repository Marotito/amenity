/* F6c: how a track sits on the chords: the share of its sounding time on a chord tone, a tension, an avoid note
 * (a half step over a chord tone) or out of the chord's scale; and the same at the bars' joins (their last 16th
 * and the next's first). The melody (default) or the bass ("bass"), each style, 240 s, the form off.
 *   sh tests/clash.sh [bass] */
#include "music.c"
#include <stdio.h>
int jungle_playing(void) { return 1; }
void jungle_pattern(struct jungle_step out[JUNGLE_STEPS]) { memset(out, 0, sizeof(struct jungle_step) * JUNGLE_STEPS); }
int jungle_roll_active(void) { return -1; }
void jungle_request_fill(unsigned k, unsigned l) { (void)k; (void)l; }
void jungle_swing(unsigned t) { (void)t; }
int main(int argc, char **argv)
{
    int style = atoi(argv[1]), secs = argc > 2 ? atoi(argv[2]) : 240, bt = argc > 3 && !strcmp(argv[3], "bass");
    static int32_t lr[512];
    music_init();
    music_style((unsigned)style);
    music_mute(bt ? TRK_BASS : TRK_MELODY, 0, 0);
    double sps = 44100.0 * 60.0 / 170.0 / 4.0, next = 0; unsigned step = 0;
    long frames = (long)secs * 44100, f = 0;
    double cls[4] = {0}, endc[4] = {0}; long endn = 0;
    while (f < frames) {
        while (next < f + 256) { music_step(step++ % 32, (unsigned)(next - f)); next += sps; }
        memset(lr, 0, sizeof lr);
        music_render(lr, 256);
        f += 256;
        float hz = bt ? (bass.on && bass.gate_left > 0.0f ? bass.target : 0.0f) : (lead.on && lead.stage != 3 ? lead.target : 0.0f);
        if (f < 44100 * 8 || hz <= 0.0f || !cur_chord) continue;
        int midi = (int)lrintf(12.0f * log2f(hz / 440.0f) + 69.0f), pc = ((midi % 12) + 12) % 12;
        int root = (key_root + cur_chord->root) % 12, rel = (pc - root + 12) % 12, k = 3;
        int ct = 0, avoid = 0;
        for (int i = 0; i < 4; ++i) { int t = chord_pc(cur_chord, i); if (t == pc) ct = 1; if ((t + 1) % 12 == pc) avoid = 1; }
        int inscale = SCALE[cur_chord->sc] >> rel & 1u;
        k = ct ? 0 : !inscale ? 3 : avoid ? 2 : 1;
        cls[k] += 1;
        /* the bar's last 16th and the first of the next (the joins), at the BASE's 16ths */
        unsigned s16 = (step + 31) % 16;
        if (s16 == 15 || s16 == 0) { endc[k] += 1; ++endn; }
    }
    double t = cls[0] + cls[1] + cls[2] + cls[3], te = endc[0] + endc[1] + endc[2] + endc[3];
    printf("%-9s chord %4.1f%% tension %4.1f%% AVOID %4.1f%% OUT %4.1f%% | at the joins: AVOID+OUT %4.1f%%\n", STYLE[style].name,
           100 * cls[0] / t, 100 * cls[1] / t, 100 * cls[2] / t, 100 * cls[3] / t, 100 * (endc[2] + endc[3]) / (te ? te : 1));
    return 0;
}
