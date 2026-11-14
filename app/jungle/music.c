/* SPDX-License-Identifier: GPL-3.0-only */
/* Amenity+: the music that follows the break. From the host prototypes (proto2..7, Marote's feedback):
 *   a conductor     two progressions (i-VI-III-VII, i-iv-i-v) that take turns every 4 phrases, a chord a bar
 *   the bass        mono: SUB, ACID or REESE; its line KICK (a note on each kick, the old one), LONG (held roots,
 *                   slides, pushes), RAGGA (a riff that repeats and answers), ACID (16 steps, slides, accents)
 *   a rhythm layer  3 voices of organ: the skank family (A B C D) and the jazz family (J comping, hemiola,
 *                   pushes), a family 2 to 4 phrases at a time; stabs after a BUILD
 *   a pad           3 voices of strings, the filter moving over the phrase
 * Float, as the engine's clock code: the FM-1's FPU is single precision, no libm (fastmath.h), no 64-bit maths.
 * Everything runs in the audio interrupt; the controls are single words the main loop writes. */
#include "music.h"
#include "jungle.h"
#include "jungle_libc.h"
#include "fastmath.h"

#define SR 44100.0f

/* ---------------------------------------------------------------- the controls --- */
static volatile uint8_t c_level = MUSIC_OFF, c_bass_mode = MBASS_LONG, c_timbre = MTIMBRE_REESE, c_pool = MPOOL_MIX;
static volatile int8_t c_root = -1;
void music_level(unsigned level) { if (level < MUSIC_LEVELS) c_level = (uint8_t)level; }
void music_bass_mode(unsigned mode) { if (mode < MBASS_MODES) c_bass_mode = (uint8_t)mode; }
void music_bass_timbre(unsigned timbre) { if (timbre < MTIMBRES) c_timbre = (uint8_t)timbre; }
void music_pool(unsigned pool) { if (pool < MPOOLS) c_pool = (uint8_t)pool; }
void music_root(unsigned pc) { c_root = (int8_t)(pc % 12u); }

/* ------------------------------------------------------------- small pieces --- */
static uint32_t rng = 0x2545f491u;
static unsigned rnd(unsigned n) { rng = rng * 1664525u + 1013904223u; return (unsigned)(((rng >> 8) & 0xffffu) * n >> 16); }
static float frnd(void) { return (float)rnd(10000) * 0.0001f; }
static int iabs(int x) { return x < 0 ? -x : x; }
static float mtof(float m) { return 440.0f * fm_exp2f((m - 69.0f) * (1.0f / 12.0f)); }
static float blep(float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
static float saw(float *ph, float inc)
{
    float s = 2.0f * *ph - 1.0f - blep(*ph, inc);
    *ph += inc;
    if (*ph >= 1.0f) *ph -= 1.0f;
    return s;
}
struct svf { float ic1, ic2; };
static float svf_lp(struct svf *f, float x, float g, float k)
{
    float a1 = 1.0f / (1.0f + g * (g + k)), v3 = x - f->ic2;
    float v1 = a1 * f->ic1 + g * a1 * v3, v2 = f->ic2 + g * v1;
    f->ic1 = 2.0f * v1 - f->ic1;
    f->ic2 = 2.0f * v2 - f->ic2;
    return v2;
}
static float g_of(float hz)   /* tan(pi f / SR), a polynomial: no libm per sample */
{
    hz = hz < 30.0f ? 30.0f : hz > 16000.0f ? 16000.0f : hz;
    float x = 3.14159265f * hz * (1.0f / SR), x2 = x * x;
    return x * (1.0f + x2 * (0.33333f + x2 * 0.13333f));
}
static float soft(float x) { return x / (1.0f + fm_fabsf(x)); }
static float sine_tab[1024];
static float sine(float ph) { return sine_tab[(int)(ph * 1024.0f) & 1023]; }
static float ms_coef(float ms) { return 1.0f - fm_expf(-1.0f / (ms * 0.001f * SR)); }

/* ------------------------------------------------------------ the conductor --- */
static const int8_t prog_epic[4][4] = { {0,3,7,10}, {8,0,3,7}, {3,7,10,2}, {10,2,5,9} };   /* i VI III VII */
static const int8_t prog_dub[4][4]  = { {0,3,7,10}, {5,8,0,3}, {0,3,7,10}, {7,10,2,5} };   /* i iv i v */
static const int8_t (*prog)[4] = prog_epic;
static int key_root = 4, chord_ix = 3, phrase_count;
static float sps = SR * 60.0f / 170.0f / 4.0f;          /* samples a 16th: measured between steps */
static uint32_t frame_base, last_step_at;
static int have_step;

/* --------------------------------------------------------------- the bass --- */
static struct {
    int mode; float ph[3], freq, target, env, fenv, gate_left, glide, acc; struct svf f; int on, rising;
} bass;
static void bass_play(int midi, float gate_samples, int slide, int accent)
{
    bass.target = mtof((float)midi);
    if (slide && bass.on && bass.gate_left > 0) {        /* a slide: no new attack, the pitch glides there */
        bass.glide = 0.0009f;
        bass.gate_left = gate_samples;
        if (accent) bass.fenv = 0.8f;
        return;
    }
    bass.freq = bass.target;
    bass.glide = 0.0015f;
    bass.fenv = 1.0f;
    bass.acc = accent ? 1.0f : 0.72f;
    bass.on = 1;
    bass.rising = 1;
    bass.gate_left = gate_samples;
}
static float bass_render(void)
{
    if (!bass.on) return 0.0f;
    bass.freq += (bass.target - bass.freq) * bass.glide;
    if (bass.rising) { bass.env += (1.0f - bass.env) * 0.02f; if (bass.env > 0.99f) bass.rising = 0; }
    float inc = bass.freq * (1.0f / SR), s;
    switch (bass.mode) {
    case MTIMBRE_SUB:  s = sine(bass.ph[0]); bass.ph[0] += inc; if (bass.ph[0] >= 1.0f) bass.ph[0] -= 1.0f; break;
    case MTIMBRE_ACID: s = saw(&bass.ph[0], inc); break;
    default:
        s = 0.5f * (saw(&bass.ph[0], inc * 1.004f) + saw(&bass.ph[1], inc * 0.996f)) + 0.6f * sine(bass.ph[2]);
        bass.ph[2] += inc * 0.5f;
        if (bass.ph[2] >= 1.0f) bass.ph[2] -= 1.0f;
        break;
    }
    float cut = bass.mode == MTIMBRE_ACID ? 220.0f + 3200.0f * bass.fenv * bass.acc * bass.acc : 140.0f + 700.0f * bass.fenv;
    s = svf_lp(&bass.f, s, g_of(cut), bass.mode == MTIMBRE_ACID ? 0.35f : 1.2f);
    bass.fenv *= bass.mode == MTIMBRE_ACID ? 0.9993f : 0.9996f;
    if (bass.gate_left > 0) bass.gate_left -= 1.0f;
    else bass.env *= bass.mode == MTIMBRE_ACID ? 0.9975f : bass.mode == MTIMBRE_SUB ? 0.9985f : 0.9992f;
    if (bass.env < 1e-4f) { bass.on = 0; bass.env = 0; }
    return soft(1.6f * s) * bass.env * bass.acc;
}

/* ------------------------------------------------------- a 3-voice layer --- */
enum { T_STRINGS, T_ORGAN, T_STAB };
struct voice { float ph[4], freq, env, fenv, vel; int stage, gate; struct svf f; };
struct layer {
    struct voice v[3]; int voicing[3], lo, hi;
    int timbre; float sus, cut, famt, k, gain, ca, cd, cr, pan0;
};
static struct layer pad, rhy;
static void layer_shape(struct layer *L, int timbre, float atk, float dec, float sus, float rel,
                        float cut, float famt, float k, float gain)
{
    L->timbre = timbre; L->sus = sus; L->cut = cut; L->famt = famt; L->k = k; L->gain = gain;
    L->ca = ms_coef(atk); L->cd = ms_coef(dec); L->cr = ms_coef(rel);
}
static void voice_lead(struct layer *L, const int pcs[3])   /* each voice: its chord tone's nearest octave */
{
    static const int8_t perm[6][3] = { {0,1,2}, {0,2,1}, {1,0,2}, {1,2,0}, {2,0,1}, {2,1,0} };
    int best[3] = { 0, 0, 0 }, best_cost = 1 << 30;
    for (int p = 0; p < 6; ++p) {
        int cand[3], cost = 0;
        for (int v = 0; v < 3; ++v) {
            int pc = (key_root + pcs[perm[p][v]]) % 12, old = L->voicing[v], n = old - ((old - pc) % 12 + 12) % 12;
            if (old - n > 6) n += 12;
            while (n < L->lo) n += 12;
            while (n > L->hi) n -= 12;
            cand[v] = n;
            cost += iabs(n - old);
        }
        if (cost < best_cost) { best_cost = cost; best[0] = cand[0]; best[1] = cand[1]; best[2] = cand[2]; }
    }
    L->voicing[0] = best[0]; L->voicing[1] = best[1]; L->voicing[2] = best[2];
}
static const int TRIAD[3] = { 0, 1, 2 }, COLOUR[3] = { 1, 2, 3 };
static void layer_chord_of(struct layer *L, const int8_t (*pr)[4], int ix, const int idx[3], int octave_up, float vel,
                           int gate_steps, int legato)
{
    int pcs[3] = { pr[ix][idx[0]], pr[ix][idx[1]], pr[ix][idx[2]] };
    voice_lead(L, pcs);
    for (int v = 0; v < 3; ++v) {
        struct voice *h = &L->v[v];
        h->freq = mtof((float)(L->voicing[v] + 12 * octave_up));
        if (!legato || h->stage == 0 || h->stage == 3) { h->stage = 1; if (!legato) h->env = 0; }
        h->fenv = 1.0f;
        h->vel = vel;
        h->gate = gate_steps;
    }
}
static void layer_release(struct layer *L)
{
    for (int v = 0; v < 3; ++v) if (L->v[v].stage && L->v[v].stage != 3) L->v[v].stage = 3;
}
static void layer_step_gates(struct layer *L)
{
    for (int v = 0; v < 3; ++v) if (L->v[v].gate > 0 && --L->v[v].gate == 0) L->v[v].stage = 3;
}
static void layer_render(struct layer *L, float *l, float *r, float g_moving)
{
    for (int v = 0; v < 3; ++v) {
        struct voice *h = &L->v[v];
        if (!h->stage) continue;
        if (h->stage == 1) { h->env += (1.05f - h->env) * L->ca; if (h->env >= 1.0f) { h->env = 1.0f; h->stage = 2; } }
        else if (h->stage == 2) h->env += (L->sus - h->env) * L->cd;
        else { h->env -= h->env * L->cr; if (h->env < 1e-4f) { h->stage = 0; continue; } }
        float inc = h->freq * (1.0f / SR), s;
        if (L->timbre == T_ORGAN) {   /* drawbars 8' 4' 2 2/3' 2', a percussion click */
            s = 0.55f * sine(h->ph[0]) + 0.4f * sine(h->ph[1]) + 0.3f * sine(h->ph[2]) + 0.25f * sine(h->ph[3]);
            h->ph[0] += inc; h->ph[1] += inc * 2.0f; h->ph[2] += inc * 3.0f; h->ph[3] += inc * 4.0f;
            for (int k = 0; k < 4; ++k) if (h->ph[k] >= 1.0f) h->ph[k] -= 1.0f;
            s += 0.35f * h->fenv * sine(h->ph[3] * 2.0f);
            h->fenv *= 0.9985f;
        } else {
            s = 0.5f * (saw(&h->ph[0], inc * 1.0035f) + saw(&h->ph[1], inc * 0.9965f));
            float g = L->timbre == T_STRINGS ? g_moving : g_of(L->cut + L->famt * h->fenv);
            s = svf_lp(&h->f, s, g, L->k);
            h->fenv *= 0.9992f;
        }
        s *= h->env * L->gain * h->vel;
        float pan = L->pan0 + 0.2f * (float)v;
        *l += s * (1.0f - pan);
        *r += s * pan;
    }
}

/* ------------------------------------------- the rhythm layer's hits --- */
enum { R_REST, R_SKANK, R_BUBBLE, R_STABS };
static struct { float wait; int octave; float vel; int gate; int live; int ant; } pend[8];
static int sched_ant;
static const int8_t (*next_prog)[4];
static void schedule(float delay, int octave, float vel, int gate)
{
    for (int i = 0; i < 8; ++i) if (!pend[i].live) {
        pend[i].wait = delay; pend[i].octave = octave; pend[i].vel = vel; pend[i].gate = gate;
        pend[i].live = 1; pend[i].ant = sched_ant;
        return;
    }
}
static void pend_tick(void)
{
    for (int i = 0; i < 8; ++i) if (pend[i].live && (pend[i].wait -= 1.0f) <= 0.0f) {
        pend[i].live = 0;
        if (pend[i].ant) layer_chord_of(&rhy, next_prog, (chord_ix + 1) % 4, TRIAD, pend[i].octave, pend[i].vel, pend[i].gate, 0);
        else layer_chord_of(&rhy, prog, chord_ix, TRIAD, pend[i].octave, pend[i].vel, pend[i].gate, 0);
    }
}
static void rhythm_set(int r)
{
    switch (r) {
    case R_SKANK:  rhy.lo = 60; rhy.hi = 76; layer_shape(&rhy, T_ORGAN, 2, 60, 0.0f, 35, 5000, 0, 1.4f, 0.30f); break;
    case R_BUBBLE: rhy.lo = 55; rhy.hi = 72; layer_shape(&rhy, T_ORGAN, 2, 50, 0.0f, 30, 5000, 0, 1.4f, 0.26f); break;
    case R_STABS:  rhy.lo = 64; rhy.hi = 79; layer_shape(&rhy, T_STAB, 1, 140, 0.05f, 90, 900, 5000, 0.9f, 0.24f); break;
    default: layer_release(&rhy); break;
    }
}

/* the modes and their families */
enum { MD_A, MD_B, MD_C, MD_D, MD_JCOMP, MD_JHEM, MD_JPUSH, MD_STABS, MD_COUNT };
static const int8_t FAMILY[MD_COUNT] = { 0, 0, 0, 0, 1, 1, 1, 2 };
static int md = MD_A, fam_left, build_was_on;
static const uint16_t STAB_PATTERNS[3] = {
    (1u << 0) | (1u << 3) | (1u << 6) | (1u << 10) | (1u << 13),
    (1u << 2) | (1u << 7) | (1u << 10) | (1u << 14),
    (1u << 0) | (1u << 6) | (1u << 11),
};
static uint16_t stab_mask;

/* J: jazz comping on the straight 16th grid */
struct hit { int8_t at, gate; float vel; };
struct motif { int8_t n; struct hit h[2]; };
static const struct motif MOT[] = {
    { 2, { {0, 5, 0.90f}, {6, 1, 0.70f} } },   /* charleston (1 . 2&) */
    { 2, { {2, 1, 0.70f}, {8, 5, 0.90f} } },   /* reverse charleston (1& . 3) */
    { 2, { {6, 1, 0.75f}, {14, 3, 0.85f} } },  /* pushes (2& . 4&) */
    { 2, { {2, 1, 0.80f}, {10, 2, 0.80f} } },  /* offbeats (1& . 3&) */
    { 2, { {6, 1, 0.75f}, {12, 3, 0.85f} } },  /* late (2& . 4) */
    { 0, { {0, 0, 0}, {0, 0, 0} } },           /* space */
};
enum { M_COUNT = 6, M_SPACE = 5 };
static int jz_bar[2], jz_hemiola, jz_tied, jz_last_hemiola, jz_kind, jz_shift;
static void jazz_plan(void)
{
    jz_hemiola = jz_kind == 1 || (jz_kind == 0 && !jz_last_hemiola && rnd(100) < 20);
    jz_last_hemiola = jz_hemiola;
    jz_shift = jz_kind == 1 && rnd(2) ? 2 : 0;
    if (jz_hemiola) return;
    if (jz_kind == 2) { jz_bar[0] = rnd(3) ? M_SPACE : 2; jz_bar[1] = 2; return; }
    jz_bar[0] = rnd(100) < 8 ? M_SPACE : (int)rnd(M_COUNT - 1);
    unsigned x = rnd(100);
    if (x < 45) jz_bar[1] = jz_bar[0];
    else if (x < 85) do jz_bar[1] = (int)rnd(M_COUNT - 1); while (jz_bar[1] == jz_bar[0]);
    else jz_bar[1] = M_SPACE;
    if (jz_bar[0] == M_SPACE && jz_bar[1] == M_SPACE) jz_bar[1] = 2;
}
static void jazz_hit(int step, int gate, float vel)
{
    int in_bar = step % 16;
    if (in_bar == 0 && jz_tied) { jz_tied = 0; return; }   /* after a push the downbeat is not struck again */
    sched_ant = in_bar == 14;                              /* the "and of 4": the next chord, held over */
    next_prog = (step >= 16 && (phrase_count + 1) % 4 == 0) ? (prog == prog_epic ? prog_dub : prog_epic) : prog;
    schedule(0, 0, vel, sched_ant ? 3 : gate);
    if (sched_ant) jz_tied = 1;
    sched_ant = 0;
}
static void jazz_step(int step)
{
    if (step == 0) jazz_plan();
    if (jz_hemiola) {
        int t = step - jz_shift;
        if (t >= 0 && t % 6 == 0 && !(jz_shift && t == 30)) jazz_hit(step, 2, t % 12 ? 0.75f : 0.9f);
        return;
    }
    const struct motif *m = &MOT[jz_bar[step / 16]];
    int fired = 0;
    for (int i = 0; i < m->n; ++i) if (m->h[i].at == step % 16) { jazz_hit(step, m->h[i].gate, m->h[i].vel); fired = 1; }
    if (!fired && step == 30 && jz_bar[1] != M_SPACE) {
        int room = 1;
        for (int i = 0; i < MOT[jz_bar[1]].n; ++i) if (MOT[jz_bar[1]].h[i].at >= 12) room = 0;
        if (room && rnd(100) < 60) jazz_hit(step, 3, 0.85f);
    }
    if (step % 16 == 0) jz_tied = 0;
}
static void skank_step(int step)
{
    int in8 = step % 8;
    switch (md) {
    case MD_A: if (in8 == 4) schedule(0, 0, 0.9f, 1); break;
    case MD_B: if (in8 == 4) schedule(0, 0, 0.9f, 1); else if (in8 == 6) schedule(0, 0, 0.45f, 1); break;
    case MD_C: if (in8 == 4) schedule(0, 0, 0.9f, 1); else if (in8 == 2 || in8 == 6) schedule(0, 1, 0.4f, 1); break;
    case MD_D: if (step % 4 == 2) schedule(0, 0, 0.9f, 1); break;
    case MD_STABS: if (stab_mask >> (step % 16) & 1u && rnd(8)) schedule(frnd() * 0.004f * SR, 1, 0.8f + 0.2f * frnd(), 2); break;
    default: break;
    }
}
static int pick_in(int fam)
{
    int m;
    do m = (int)rnd(MD_COUNT - 1); while (FAMILY[m] != fam || (m == md && rnd(3)));
    return m;
}
static void rhythm_phrase(void)
{
    int pool = c_pool, fam = FAMILY[md];
    if (pool < MPOOL_MIX) { fam = pool; build_was_on = 0; }
    else if (build_was_on) { md = MD_STABS; fam_left = 1; build_was_on = 0; goto set; }
    else if (fam == 2 || --fam_left <= 0) {                     /* a family stays 2 to 4 phrases */
        fam = fam == 2 ? (int)rnd(2) : (rnd(10) < 8 ? !fam : fam);
        if (fam != FAMILY[md] && rnd(10) == 0) { md = MD_STABS; fam_left = 1; goto set; }
        fam_left = 2 + (int)rnd(3);
    }
    if (rnd(10) < 6 || FAMILY[md] != fam) md = pick_in(fam);
set:
    if (md == MD_STABS) { rhythm_set(R_STABS); stab_mask = STAB_PATTERNS[rnd(3)]; }
    else if (FAMILY[md] == 1) {
        rhythm_set(R_SKANK); rhy.lo = 58; rhy.hi = 74; jz_kind = md - MD_JCOMP;
        layer_shape(&rhy, T_ORGAN, 3, 160, 0.55f, 45, 5000, 0, 1.4f, 0.26f);
    } else rhythm_set(md == MD_C ? R_BUBBLE : R_SKANK);
}

/* ------------------------------------------------------- the bass line --- */
struct bev { int8_t on, deg, len, slide, acc; };
static struct bev bseq[32], riff[16], acid[16];
static int b_root[3], b_iv[3][4], b_age, bmode = MBASS_LONG;
static int root_midi(const int8_t (*pr)[4], int ix) { return 33 + ((key_root + pr[ix][0]) % 12 + 3) % 12; }   /* A1..G#2 */
static int deg_note(int bar, int deg)   /* 0 root 1 3rd 2 5th 3 7th 4 octave 5 / 6 a half step under / over the next root */
{
    int nb = bar + 1 > 2 ? 2 : bar + 1;
    if (deg == 5) return b_root[nb] - 1;
    if (deg == 6) return b_root[nb] + 1;
    if (deg == 4) return b_root[bar] + 12;
    return b_root[bar] + b_iv[bar][deg];
}
static int pick_deg(void) { unsigned x = rnd(100); return x < 35 ? 0 : x < 55 ? 4 : x < 80 ? 2 : x < 90 ? 3 : 1; }
static void make_riff(void)
{
    static const int8_t T[5][5][2] = {   /* {step, length}; length 0 ends */
        { {0, 3}, {3, 1}, {6, 2}, {10, 3}, {14, 2} },
        { {0, 4}, {6, 2}, {8, 1}, {10, 4}, {0, 0} },
        { {0, 2}, {3, 2}, {8, 3}, {11, 1}, {12, 3} },
        { {0, 6}, {7, 1}, {8, 2}, {12, 4}, {0, 0} },
        { {0, 2}, {2, 1}, {6, 4}, {11, 3}, {14, 1} },
    };
    int t = (int)rnd(5), prev = 0;
    memset(riff, 0, sizeof riff);
    for (int i = 0; i < 5 && T[t][i][1]; ++i) {
        struct bev *e = &riff[T[t][i][0]];
        e->on = 1; e->len = T[t][i][1]; e->deg = (int8_t)(i == 0 ? 0 : pick_deg());
        e->slide = (int8_t)(i && e->deg != prev && rnd(100) < 22);
        e->acc = (int8_t)(i == 0 || rnd(3) == 0);
        prev = e->deg;
    }
}
static void make_acid(int mutate)
{
    static const int8_t OFS[11] = { 0, 0, 0, 0, 12, 12, 3, 7, 10, 5, -2 };
    int from = mutate ? (int)rnd(16) : 0, to = mutate ? from + 2 : 16;
    for (int k = from; k < to; ++k) {
        int i = k & 15;
        struct bev *e = &acid[i];
        e->on = (int8_t)(i == 0 || rnd(100) < 62); e->deg = OFS[rnd(11)]; e->len = 1;
        e->slide = (int8_t)(rnd(100) < 24); e->acc = (int8_t)(rnd(100) < 30);
    }
    acid[0].deg = 0;
}
static void bass_plan(void)
{
    const int8_t (*np)[4] = (phrase_count + 1) % 4 == 0 ? (prog == prog_epic ? prog_dub : prog_epic) : prog;
    int ix[3] = { chord_ix, (chord_ix + 1) % 4, (chord_ix + 2) % 4 };
    const int8_t (*pp[3])[4] = { prog, prog, np };
    if (c_bass_mode != bmode) { bmode = c_bass_mode; b_age = 0; }
    for (int b = 0; b < 3; ++b) {
        b_root[b] = root_midi(pp[b], ix[b]);
        for (int d = 0; d < 4; ++d) b_iv[b][d] = ((pp[b][ix[b]][d] - pp[b][ix[b]][0]) % 12 + 12) % 12;
    }
    memset(bseq, 0, sizeof bseq);
    switch (bmode) {
    case MBASS_LONG:
        for (int bar = 0; bar < 2; ++bar) {
            struct bev *e = &bseq[bar * 16];
            int split = rnd(100) < 45;
            e->on = 1; e->deg = 0; e->len = (int8_t)(split ? 10 : 16); e->acc = 1;
            if (split) { struct bev *f = &bseq[bar * 16 + 10]; f->on = 1; f->deg = (int8_t)(rnd(2) ? 2 : 4); f->len = 6; f->slide = (int8_t)rnd(2); }
        }
        if (rnd(100) < 55) {   /* the push: into the next phrase's root, an 8th early */
            if (bseq[16].len > 14) bseq[16].len = 14;
            if (bseq[26].on) bseq[26].len = 4;
            struct bev *p = &bseq[30];
            p->on = 1; p->deg = (int8_t)(rnd(2) ? 5 : 6); p->len = 2; p->slide = 1; p->acc = 1;
        }
        break;
    case MBASS_RAGGA:
        if (b_age++ % 2 == 0) make_riff();   /* a riff lives two phrases */
        for (int bar = 0; bar < 2; ++bar) for (int i = 0; i < 16; ++i) bseq[bar * 16 + i] = riff[i];
        if (rnd(100) < 60) {                 /* the answer: walking into the next root */
            for (int i = 12; i < 16; ++i) bseq[16 + i].on = 0;
            bseq[28].on = 1; bseq[28].deg = 2; bseq[28].len = 2; bseq[28].slide = 0; bseq[28].acc = 0;
            bseq[30].on = 1; bseq[30].deg = (int8_t)(rnd(2) ? 5 : 6); bseq[30].len = 2; bseq[30].slide = 1; bseq[30].acc = 1;
        }
        break;
    case MBASS_ACID:
        make_acid(b_age++ != 0);             /* the same line, two steps changed a phrase */
        for (int bar = 0; bar < 2; ++bar) for (int i = 0; i < 16; ++i) bseq[bar * 16 + i] = acid[i];
        break;
    default: break;
    }
}
static void bass_step(int step)
{
    if (bmode == MBASS_KICK || jungle_roll_active() == 1) return;   /* BUILD: the bass waits for the drop */
    struct bev *e = &bseq[step];
    if (!e->on) return;
    int bar = step / 16, note = bmode == MBASS_ACID ? b_root[bar] + e->deg : deg_note(bar, e->deg);
    int len = e->len, slide = e->slide;
    if (bmode == MBASS_ACID) {
        len = 1;
        if (e->slide) { int j = step + 1; while (j < 32 && !bseq[j].on) ++j; len = j - step + 1; }
        slide = step > 0 && bseq[step - 1].slide && bseq[step - 1].on;
    }
    bass_play(note, (float)len * sps * (len == 1 ? 0.55f : 0.92f), slide, e->acc);
}

/* ------------------------------------------------- the engine's events --- */
enum { EV_STEP, EV_HIT, NEV = 32 };
static struct { uint16_t frame; uint8_t type, a; int8_t b; } ev[NEV];
static unsigned nev;
void music_step(unsigned step, unsigned frame)
{
    if (nev < NEV) { ev[nev].frame = (uint16_t)frame; ev[nev].type = EV_STEP; ev[nev].a = (uint8_t)step; ++nev; }
}
void music_hit(unsigned slice, int pitch, unsigned frame)
{
    if (nev < NEV) { ev[nev].frame = (uint16_t)frame; ev[nev].type = EV_HIT; ev[nev].a = (uint8_t)slice; ev[nev].b = (int8_t)pitch; ++nev; }
}

static unsigned level, pad_on;
static void on_step(int step, uint32_t at)
{
    if (have_step) {   /* the tempo, from the steps themselves: right with a MIDI clock, a tempo change, halving */
        uint32_t d = at - last_step_at;
        if (d > 1000u && d < 40000u) sps += ((float)d - sps) * 0.5f;
    }
    have_step = 1;
    last_step_at = at;
    if (step == 0) {
        if (c_root >= 0 && c_root != key_root) { key_root = c_root; chord_ix = 3; }
        if (++phrase_count % 4 == 0) prog = prog == prog_epic ? prog_dub : prog_epic;
        rhythm_phrase();
        if (level >= MUSIC_FULL) pad_on = 1; else { pad_on = 0; layer_release(&pad); }
        if (level == MUSIC_MAX) {   /* everything held: the CPU's worst case */
            rhythm_set(R_STABS); rhy.sus = 0.5f;
            layer_chord_of(&rhy, prog, (chord_ix + 1) % 4, TRIAD, 1, 0.6f, 0, 1);
        }
    }
    if (step % 16 == 0) {
        chord_ix = (chord_ix + 1) % 4;
        if (pad_on) layer_chord_of(&pad, prog, chord_ix, COLOUR, 0, 1.0f, 0, 1);
        if (step == 0) bass_plan();
    }
    if (level >= MUSIC_BASS) bass_step(step);
    layer_step_gates(&rhy);
    if (level >= MUSIC_RHYTHM && level != MUSIC_MAX) {
        if (FAMILY[md] == 1) jazz_step(step); else skank_step(step);
    }
    if (jungle_roll_active() == 1) build_was_on = 1;
}
static void on_hit(int slice, int pitch)
{
    static const uint16_t kick_mask = (1u << 0) | (1u << 1) | (1u << 5) | (1u << 9) | (1u << 13);   /* Amen */
    (void)pitch;
    if (level < MUSIC_BASS || bmode != MBASS_KICK || jungle_roll_active() == 1) return;
    if (!(kick_mask >> slice & 1u)) return;
    int note = 36 + (key_root + prog[chord_ix][0]) % 12;
    if (note < 28) note += 12;
    bass_play(note, SR * 0.12f, jungle_roll_active() == 2, 1);
}

void music_init(void)
{
    for (int i = 0; i < 1024; ++i) sine_tab[i] = fm_sinf(6.2831853f * (float)i * (1.0f / 1024.0f));
    pad.lo = 55; pad.hi = 74; pad.pan0 = 0.3f;
    layer_shape(&pad, T_STRINGS, 180, 400, 0.85f, 600, 900, 0, 0.7f, 0.20f);
    rhy.pan0 = 0.4f;
    pad.voicing[0] = rhy.voicing[0] = 60; pad.voicing[1] = rhy.voicing[1] = 64; pad.voicing[2] = rhy.voicing[2] = 67;
    rhythm_set(R_SKANK);
    bass.mode = MTIMBRE_REESE;
}

void music_render(int32_t *lr, unsigned n)
{
    unsigned lv = c_level, e = 0;
    if (lv != level) {
        if (lv < MUSIC_BASS) bass.on = 0;
        if (lv < MUSIC_RHYTHM || (level == MUSIC_MAX && lv != MUSIC_MAX)) { layer_release(&rhy); rhythm_set(md == MD_STABS ? R_STABS : R_SKANK); }
        if (lv < MUSIC_FULL) { pad_on = 0; layer_release(&pad); }
        level = lv;
    }
    bass.mode = c_timbre;
    if (lv == MUSIC_OFF && !bass.on) {   /* off: only the conductor keeps time, so the music comes back in place */
        for (; e < nev; ++e) if (ev[e].type == EV_STEP) on_step(ev[e].a, frame_base + ev[e].frame);
        nev = 0;
        frame_base += n;
        return;
    }
    static float lfo_ph;
    lfo_ph += (float)n / (sps * 32.0f);
    if (lfo_ph >= 1.0f) lfo_ph -= 1.0f;
    float tri = lfo_ph < 0.5f ? 2.0f * lfo_ph : 2.0f - 2.0f * lfo_ph;
    float g_pad = g_of(900.0f * (0.45f + 2.6f * tri * tri));
    for (unsigned i = 0; i < n; ++i) {
        while (e < nev && ev[e].frame <= i) {
            if (ev[e].type == EV_STEP) on_step(ev[e].a, frame_base + i); else on_hit(ev[e].a, ev[e].b);
            ++e;
        }
        pend_tick();
        float l = 0.0f, r = 0.0f, b = bass_render() * 0.55f;
        layer_render(&pad, &l, &r, g_pad);
        layer_render(&rhy, &l, &r, g_pad);
        /* the break peaks about half of full scale (jungle.c: mix << 7); the music beside it, softly limited */
        float L = 0.75f * soft(1.2f * (b + l)), R = 0.75f * soft(1.2f * (b + r));
        int32_t a = lr[2 * i] + (int32_t)(L * 8388607.0f), c = lr[2 * i + 1] + (int32_t)(R * 8388607.0f);
        lr[2 * i] = a > 8388607 ? 8388607 : a < -8388607 ? -8388607 : a;          /* 24 bits: never wrap */
        lr[2 * i + 1] = c > 8388607 ? 8388607 : c < -8388607 ? -8388607 : c;
    }
    for (; e < nev; ++e) if (ev[e].type == EV_STEP) on_step(ev[e].a, frame_base + n);
    nev = 0;
    frame_base += n;
}
