/* SPDX-License-Identifier: GPL-3.0-only */
/* Facility: the tracks that follow the break (track 1, Amenity's own). From the host prototypes (proto2..7) and
 * Marote's choices:
 *   the conductor   the BASE style's four progressions (each chord with its scale and its length), one after
 *                   another (AUTO) or one held; how fast the chords change; the key from the feedback's root
 *   the bass        mono: SUB, ACID or REESE; its line KICK (a note on each kick), LONG (held roots, slides,
 *                   pushes), RAGGA (a riff that repeats and answers), ACID (16 steps, slides, accents), WALK (a
 *                   walking line, chromatic into the next chord), ONE DROP (beat one left empty), RIFF (a riff on
 *                   the chord's scale: pentatonic, blues)
 *   a rhythm layer  3 voices: the skank family (A B C D), the jazz family (J comping, hemiola, pushes), stabs
 *   a pad           3 voices of strings, the filter moving over the phrase
 * Each track plays in its own style (or the BASE's): its modes and sounds (AUTO: the style picks, a family a few
 * phrases at a time) and its speed: half, normal or double, the one that brings the tempo nearest the style's centre.
 * A track's speed is a clock of its own (virtual 16ths, aligned on the break's phrases); the chords are the
 * conductor's, read when a note sounds, so every track at every speed plays the chord that is sounding.
 * Each track renders into its own buffer, then the tracks are mixed with their mute (a 5 ms fade, or from the next
 * phrase), AUTO DUB's drops and their level: so a track can move to the second core, or to an effect, later.
 * Float, as the engine's clock code: the FM-1's FPU is single precision, no libm (fastmath.h), no 64-bit maths.
 * Everything runs in the audio interrupt; the controls are single words the main loop writes. */
#include "music.h"
#include "jungle.h"
#include "jungle_libc.h"
#include "fastmath.h"

#define SR 44100.0f

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
static float fbright_v = 1.0f;
#define fbright fbright_v
static float knob_mul(unsigned v) { return fm_exp2f(((float)v - 64.0f) * (1.0f / 32.0f)); }   /* 0.25 .. 1 .. 4 */

/* ------------------------------------------------------------ the harmony's data --- */
/* chord qualities: the four tones over the root */
enum { Q_MIN, Q_MAJ, Q_DOM, Q_MAJ7, Q_HDIM, Q_DIM };
static const int8_t QUAL[6][4] = { {0,3,7,10}, {0,4,7,12}, {0,4,7,10}, {0,4,7,11}, {0,3,6,10}, {0,3,6,9} };
/* the scales, over the chord's root (bit n: n semitones up) */
enum { S_AEO, S_DOR, S_PHR, S_PHRDOM, S_HARM, S_MEL, S_MIX, S_ION, S_LYD, S_LOC, S_MPENT, S_BLUES };
#define B(n) (1u << (n))
static const uint16_t SCALE[12] = {
    B(0)|B(2)|B(3)|B(5)|B(7)|B(8)|B(10),  B(0)|B(2)|B(3)|B(5)|B(7)|B(9)|B(10), B(0)|B(1)|B(3)|B(5)|B(7)|B(8)|B(10),
    B(0)|B(1)|B(4)|B(5)|B(7)|B(8)|B(10),  B(0)|B(2)|B(3)|B(5)|B(7)|B(8)|B(11), B(0)|B(2)|B(3)|B(5)|B(7)|B(9)|B(11),
    B(0)|B(2)|B(4)|B(5)|B(7)|B(9)|B(10),  B(0)|B(2)|B(4)|B(5)|B(7)|B(9)|B(11), B(0)|B(2)|B(4)|B(6)|B(7)|B(9)|B(11),
    B(0)|B(1)|B(3)|B(5)|B(6)|B(8)|B(10),  B(0)|B(3)|B(5)|B(7)|B(10),           B(0)|B(3)|B(5)|B(6)|B(7)|B(10),
};
struct chord { int8_t root; uint8_t q, sc, beats; const char *nm; };
struct prog { const char *name; uint8_t n; struct chord c[8]; };
struct style {
    const char *name; float centre;
    struct prog p[4];
    uint8_t bass_w[MBASS_MODES];             /* AUTO's weights for the bass's lines */
    uint8_t bass_sound;
    uint16_t harm_md;                        /* AUTO's rhythm figures (MD_ bits) */
    uint8_t pad_pct, harm_sound, stabs_after_build;
};
enum { MD_A, MD_B, MD_C, MD_D, MD_JCOMP, MD_JHEM, MD_JPUSH, MD_STABS, MD_COUNT };
#define M_SKANK (B(MD_A) | B(MD_B) | B(MD_C) | B(MD_D))
#define M_JAZZ (B(MD_JCOMP) | B(MD_JHEM) | B(MD_JPUSH))
/* Marote's five (16 nov): Venetian Snares, US3, The Prodigy, Gorillaz, High Tone. The chords: root over the key,
 * quality, scale, beats (4 a bar) */
static const struct style STYLE[MSTYLES] = {
    { "BREAKCORE", 170.0f, {
        { "EPIC", 4, { {0,Q_MIN,S_AEO,4,"i"}, {8,Q_MAJ,S_LYD,4,"VI"}, {3,Q_MAJ,S_ION,4,"III"}, {10,Q_MAJ,S_MIX,4,"VII"} } },
        { "DRAMA", 4, { {0,Q_MIN,S_HARM,4,"i"}, {5,Q_MIN,S_DOR,4,"iv"}, {7,Q_DOM,S_PHRDOM,4,"V"}, {0,Q_MIN,S_HARM,4,"i"} } },
        { "ANDALUZ", 4, { {0,Q_MIN,S_AEO,4,"i"}, {10,Q_MAJ,S_MIX,4,"VII"}, {8,Q_MAJ,S_LYD,4,"VI"}, {7,Q_DOM,S_PHRDOM,4,"V"} } },
        { "BAROQUE", 8, { {0,Q_MIN,S_AEO,2,"i"}, {5,Q_MIN,S_DOR,2,"iv"}, {10,Q_DOM,S_MIX,2,"VII"}, {3,Q_MAJ7,S_ION,2,"III"},
                          {8,Q_MAJ7,S_LYD,2,"VI"}, {2,Q_HDIM,S_LOC,2,"ii0"}, {7,Q_DOM,S_PHRDOM,2,"V"}, {0,Q_MIN,S_HARM,2,"i"} } } },
      /* KICK LONG RAGGA ACID WALK 1DROP RIFF */
      { 15, 40, 30, 15, 0, 0, 0 }, MTIMBRE_REESE, B(MD_A) | B(MD_D) | M_JAZZ, 70, MHSOUND_SAW, 1 },
    { "JAZZ", 100.0f, {
        { "DORIAN", 2, { {0,Q_MIN,S_DOR,16,"i7"}, {5,Q_DOM,S_MIX,16,"IV7"} } },
        { "II-V-I", 3, { {2,Q_HDIM,S_LOC,4,"ii0"}, {7,Q_DOM,S_PHRDOM,4,"V7"}, {0,Q_MIN,S_MEL,8,"i7"} } },
        { "DESCENT", 4, { {0,Q_MIN,S_DOR,4,"i7"}, {10,Q_DOM,S_MIX,4,"VII7"}, {8,Q_MAJ7,S_LYD,4,"VImaj"}, {7,Q_DOM,S_PHRDOM,4,"V7"} } },
        { "BLUES 12", 7, { {0,Q_MIN,S_DOR,16,"i7"}, {5,Q_MIN,S_DOR,8,"iv7"}, {0,Q_MIN,S_DOR,8,"i7"}, {8,Q_DOM,S_LYD,4,"VI7"},
                           {7,Q_DOM,S_PHRDOM,4,"V7"}, {0,Q_MIN,S_DOR,4,"i7"}, {7,Q_DOM,S_PHRDOM,4,"V7"} } } },
      { 0, 20, 0, 0, 80, 0, 0 }, MTIMBRE_SUB, M_JAZZ, 20, MHSOUND_ORGAN, 0 },
    { "ACID", 135.0f, {
        { "ONE CHORD", 2, { {0,Q_MIN,S_PHR,12,"i"}, {1,Q_MAJ,S_LYD,4,"bII"} } },
        { "RAVE", 4, { {0,Q_MIN,S_AEO,4,"i"}, {10,Q_MAJ,S_MIX,4,"VII"}, {8,Q_MAJ,S_LYD,4,"VI"}, {10,Q_MAJ,S_MIX,4,"VII"} } },
        { "PHRYGIAN", 4, { {0,Q_MIN,S_PHR,4,"i"}, {1,Q_MAJ,S_LYD,4,"bII"}, {0,Q_MIN,S_PHR,4,"i"}, {10,Q_MAJ,S_MIX,4,"VII"} } },
        { "RISE", 4, { {0,Q_MIN,S_AEO,4,"i"}, {8,Q_MAJ,S_LYD,4,"VI"}, {10,Q_MAJ,S_MIX,4,"VII"}, {0,Q_MIN,S_AEO,4,"i"} } } },
      { 20, 0, 0, 80, 0, 0, 0 }, MTIMBRE_ACID, B(MD_STABS) | B(MD_D), 10, MHSOUND_SAW, 1 },
    { "HIP HOP", 125.0f, {
        { "TWO", 2, { {0,Q_MIN,S_DOR,4,"i7"}, {5,Q_MIN,S_DOR,4,"iv7"} } },
        { "ROCK", 4, { {0,Q_MIN,S_AEO,4,"i"}, {10,Q_MAJ,S_MIX,4,"VII"}, {8,Q_MAJ,S_LYD,4,"VI"}, {10,Q_MAJ,S_MIX,4,"VII"} } },
        { "MIXO", 4, { {0,Q_MAJ,S_MIX,4,"I"}, {10,Q_MAJ,S_ION,4,"bVII"}, {5,Q_MAJ,S_LYD,4,"IV"}, {0,Q_MAJ,S_MIX,4,"I"} } },
        { "BLUES 8", 7, { {0,Q_MIN,S_BLUES,8,"i7"}, {5,Q_MIN,S_DOR,4,"iv7"}, {0,Q_MIN,S_BLUES,4,"i7"}, {7,Q_DOM,S_PHRDOM,4,"V7"},
                          {5,Q_MIN,S_DOR,4,"iv7"}, {0,Q_MIN,S_BLUES,4,"i7"}, {7,Q_DOM,S_PHRDOM,4,"V7"} } } },
      { 0, 20, 20, 0, 0, 0, 60 }, MTIMBRE_SUB, B(MD_A) | B(MD_B) | B(MD_JCOMP), 40, MHSOUND_ORGAN, 0 },
    { "DUB", 85.0f, {
        { "ONE CHORD", 1, { {0,Q_MIN,S_AEO,16,"i"} } },
        { "I-VII", 2, { {0,Q_MIN,S_AEO,8,"i"}, {10,Q_MAJ,S_MIX,8,"VII"} } },
        { "I-IV", 2, { {0,Q_MIN,S_DOR,8,"i"}, {5,Q_MIN,S_DOR,8,"iv"} } },
        { "ROOTS", 4, { {0,Q_MAJ,S_ION,4,"I"}, {5,Q_MAJ,S_LYD,4,"IV"}, {7,Q_DOM,S_MIX,4,"V"}, {5,Q_MAJ,S_LYD,4,"IV"} } } },
      { 0, 15, 25, 0, 0, 60, 0 }, MTIMBRE_SUB, B(MD_A) | B(MD_B) | B(MD_C), 15, MHSOUND_ORGAN, 0 },
};
const char *music_style_name(unsigned s) { return s < MSTYLES ? STYLE[s].name : "BASE"; }
const char *music_prog_name(unsigned s, unsigned p) { return s < MSTYLES && p < 4u ? STYLE[s].p[p].name : "AUTO"; }

/* ------------------------------------------------------------- the form's data --- */
/* A section: its energy (from .. to, a PRE rises), its tracks, the progression it takes (A the verse's, B the
 * chorus's, C the bridge's), what the harmony and the bass do when they are in AUTO, AUTO DUB's share */
enum { S_INTRO, S_VERSE, S_PRE, S_CHORUS, S_BRIDGE, S_BREAKDOWN, S_DROP, S_DUB, S_KINDS };
enum { H_PAD, H_RHY, H_FULL };   /* the harmony: the pad alone, the rhythm layer (pad as the style likes), both */
enum { BL_LONG, BL_STYLE, BL_STRONG };   /* the bass: held roots, the style's lines, the style's strongest */
#define PL_BREAK 1u
#define PL_BASS 2u
#define PL_HARM 4u
struct sect_kind { const char *name; float e0, e1; uint8_t play, harm, bass; float dub; };
static const struct sect_kind SECT[S_KINDS] = {
    { "INTRO",     0.30f, 0.30f, PL_BREAK | PL_BASS | PL_HARM, H_PAD,  BL_LONG,   0.0f },   /* the bass or the pad */
    { "VERSE",     0.40f, 0.40f, PL_BREAK | PL_BASS | PL_HARM, H_RHY,  BL_STYLE,  0.0f },
    { "PRE",       0.50f, 0.85f, PL_BREAK | PL_BASS | PL_HARM, H_RHY,  BL_STYLE,  0.0f },
    { "CHORUS",    0.80f, 0.80f, PL_BREAK | PL_BASS | PL_HARM, H_FULL, BL_STYLE,  0.0f },
    { "BRIDGE",    0.50f, 0.50f, PL_BREAK | PL_BASS | PL_HARM, H_PAD,  BL_LONG,   0.0f },
    { "BREAKDOWN", 0.20f, 0.20f, PL_BASS | PL_HARM,            H_PAD,  BL_LONG,   0.0f },
    { "DROP",      0.95f, 0.95f, PL_BREAK | PL_BASS | PL_HARM, H_FULL, BL_STRONG, 0.0f },
    { "DUB",       0.50f, 0.50f, PL_BREAK | PL_BASS | PL_HARM, H_RHY,  BL_STYLE,  0.6f },
};
/* the forms: two a style (Marote, 17 nov), {section, bars of the style, progression 0 A 1 B 2 C} */
struct form { uint8_t n; uint8_t s[10][3]; };
static const struct form FORM[MSTYLES][2] = {
    { { 5, { {S_INTRO,8,0}, {S_DROP,16,1}, {S_BREAKDOWN,8,0}, {S_PRE,4,0}, {S_DROP,16,1} } },
      { 5, { {S_INTRO,4,0}, {S_DROP,8,1}, {S_BRIDGE,8,2}, {S_DROP,16,1}, {S_BREAKDOWN,8,0} } } },
    { { 8, { {S_VERSE,8,0}, {S_VERSE,8,0}, {S_BRIDGE,8,2}, {S_VERSE,8,0},
             {S_CHORUS,8,0}, {S_CHORUS,8,0}, {S_BRIDGE,8,2}, {S_CHORUS,8,0} } },   /* AABA twice, the second denser */
      { 4, { {S_INTRO,4,0}, {S_VERSE,16,0}, {S_BRIDGE,8,2}, {S_CHORUS,16,0} } } },
    { { 6, { {S_INTRO,8,0}, {S_PRE,8,0}, {S_DROP,16,1}, {S_BREAKDOWN,8,0}, {S_PRE,8,0}, {S_DROP,16,1} } },
      { 4, { {S_INTRO,16,0}, {S_DROP,16,1}, {S_BREAKDOWN,16,0}, {S_DROP,16,1} } } },
    { { 7, { {S_INTRO,4,0}, {S_VERSE,16,0}, {S_CHORUS,8,1}, {S_VERSE,16,0}, {S_CHORUS,8,1}, {S_BRIDGE,8,2}, {S_CHORUS,8,1} } },
      { 7, { {S_INTRO,8,0}, {S_VERSE,8,0}, {S_CHORUS,8,1}, {S_VERSE,8,0}, {S_CHORUS,8,1}, {S_VERSE,8,0}, {S_CHORUS,8,1} } } },
    { { 4, { {S_VERSE,8,0}, {S_CHORUS,8,1}, {S_VERSE,8,0}, {S_DUB,16,0} } },
      { 4, { {S_INTRO,4,0}, {S_VERSE,16,0}, {S_DUB,16,0}, {S_CHORUS,8,1} } } },
};
/* the fill each style asks into a bigger section (white keys: 0 1/4 REPEAT, 1 BUILD, 2 STRETCH, 3 ECHO, 4 1/8 REPEAT,
 * 7 1/8 SHRINK, 8 1/16 REPEAT) */
static const uint8_t STYLE_FILL[MSTYLES] = { 7, 0, 1, 4, 3 };

/* ---------------------------------------------------------------- the controls --- */
static volatile uint8_t c_style = MSTYLE_BREAKCORE, c_prog = MAUTO, c_stress;
static volatile uint8_t c_tstyle[TRACKS] = { MSTYLE_BASE, MSTYLE_BASE, MSTYLE_BASE, MSTYLE_BASE };
static volatile uint8_t c_bass_mode = MAUTO, c_timbre = MAUTO, c_harm = MAUTO, c_hsound = MAUTO;
static volatile float c_energy = 0.5f, c_speed = 1.0f, c_dub;
static volatile uint8_t c_form;
static volatile int8_t c_root = -1;
static volatile uint8_t want[TRACKS] = { 0, 1, 1, 1 }, want_q[TRACKS];   /* muted: as asked (main loop) */
static volatile uint8_t heard_muted[TRACKS] = { 0, 1, 1, 1 };            /* as it sounds (audio) */
static volatile float lvl[TRACKS] = { 1.0f, 1.0f, 1.0f, 1.0f };
static volatile float bass_cut = 1.0f, bass_k = 1.0f, bass_slide = 0.0009f;   /* K2..K4 of the bass */
static volatile float h_bright = 1.0f, h_move = 1.0f, h_decay = 1.0f;         /* K2..K4 of the harmony */
static volatile uint8_t h_retime;
void music_mute(unsigned t, int muted, int at_phrase)
{
    if (t >= TRACKS) return;
    want_q[t] = (uint8_t)(at_phrase && jungle_playing());
    want[t] = (uint8_t)!!muted;
}
int music_muted(unsigned t) { return t < TRACKS && heard_muted[t]; }
int music_mute_pending(unsigned t) { return t < TRACKS && want[t] != heard_muted[t]; }
void music_track_level(unsigned t, unsigned v)
{
    float g = (float)v * (1.0f / 100.0f);
    if (t < TRACKS) lvl[t] = g * g;
}
void music_param(unsigned t, unsigned k, unsigned v)
{
    if (t == TRK_BASS) {
        if (k == 0) bass_cut = knob_mul(v);
        else if (k == 1) bass_k = 2.0f - (float)v * (1.9f / 127.0f);          /* 64: as built; up: more resonance */
        else if (k == 2) bass_slide = ms_coef(4.0f + (float)v * ((float)v * 0.006f)); /* slide 4 .. 100 ms; 64: ~28 ms */
    } else if (t == TRK_HARMONY) {
        if (k == 0) h_bright = knob_mul(v);
        else if (k == 1) h_move = (float)v * (1.0f / 64.0f);
        else if (k == 2) { h_decay = knob_mul(v); h_retime = 1; }
    }
}
void music_style(unsigned s) { if (s < MSTYLES) c_style = (uint8_t)s; }
void music_prog(unsigned p) { c_prog = (uint8_t)(p < 4u ? p : MAUTO); }
void music_energy(unsigned v) { c_energy = (float)v * (1.0f / 127.0f); }
void music_chord_speed(unsigned v) { c_speed = v < 43u ? 2.0f : v < 86u ? 1.0f : 0.5f; }
void music_autodub(unsigned v) { c_dub = (float)v * (1.0f / 127.0f); }
void music_form(unsigned v) { c_form = (uint8_t)(v < 43u ? 0 : v < 86u ? 1 : 2); }
void music_track_style(unsigned t, unsigned s) { if (t < TRACKS) c_tstyle[t] = (uint8_t)(s < MSTYLES ? s : MSTYLE_BASE); }
void music_bass_mode(unsigned m) { c_bass_mode = (uint8_t)(m < MBASS_MODES ? m : MAUTO); }
void music_bass_timbre(unsigned m) { c_timbre = (uint8_t)(m < MTIMBRES ? m : MAUTO); }
void music_harm_mode(unsigned m) { c_harm = (uint8_t)(m < MHARM_MODES ? m : MAUTO); }
void music_harm_sound(unsigned m) { c_hsound = (uint8_t)(m < MHSOUNDS ? m : MAUTO); }
void music_root(unsigned pc) { c_root = (int8_t)(pc % 12u); }
void music_stress(int on) { c_stress = (uint8_t)!!on; }

/* ------------------------------------------------------------ the conductor --- */
/* Time in the BASE's own 16ths, in halves (an integer at every speed): a real 16th moves it 1 (half speed), 2 or 4
 * (double). The progression's chords are read from that time, scaled by the chord speed (K2 on BASE). */
static int key_root = 4;
static uint32_t g;                                 /* the break's 16ths since the start */
static uint32_t phrase_count;
static float sps = SR * 60.0f / 170.0f / 4.0f;     /* samples a 16th: measured between steps */
static uint32_t frame_base, last_step_at;
static int have_step;
static uint8_t base_style = MSTYLE_BREAKCORE, prog_ix, next_prog_ix, prog_auto = 1;
static uint8_t feel[TRACKS] = { 2, 2, 2, 2 };     /* 1 half, 2 normal, 4 double (16ths a real 16th, x2) */
static uint32_t bpos, cyc_start;                   /* half-16ths of the BASE */
static float cyc_speed = 1.0f;
static const struct chord *cur_chord;
static const struct prog *P(void) { return &STYLE[base_style].p[prog_ix]; }
static uint32_t prog_len(const struct prog *p, float speed)   /* in half-16ths */
{
    uint32_t n = 0;
    for (int i = 0; i < p->n; ++i) n += (uint32_t)((float)p->c[i].beats * 8.0f * speed);
    return n;
}
static const struct chord *chord_in(const struct prog *p, uint32_t at, float speed)
{
    for (int i = 0; i < p->n; ++i) {
        uint32_t d = (uint32_t)((float)p->c[i].beats * 8.0f * speed);
        if (at < d) return &p->c[i];
        at -= d;
    }
    return &p->c[0];
}
/* the chord sounding k real 16ths from now (the progression after this one when it falls beyond) */
static const struct chord *chord_ahead(uint32_t k)
{
    uint32_t at = bpos + k * feel[TRK_BREAK] - cyc_start, len = prog_len(P(), cyc_speed);
    if (at < len) return chord_in(P(), at, cyc_speed);
    return chord_in(&STYLE[base_style].p[next_prog_ix], at - len, cyc_speed);
}
static int chord_pc(const struct chord *c, int i) { return (key_root + c->root + QUAL[c->q][i]) % 12; }
static uint8_t speed_for(float centre)   /* half, normal or double: the one nearest the centre (a margin, no flapping) */
{
    float bpm = SR * 15.0f / sps;
    float r = bpm / centre;
    if (r > 1.5f) return 1;
    if (r < 0.66f) return 4;
    return 2;
}
static uint8_t track_style(int t) { return c_tstyle[t] == MSTYLE_BASE ? base_style : c_tstyle[t]; }
/* the form: the song (a form of the style's, its three progressions), the section sounding */
static uint8_t form_on, song_var, sec_i, sec_prog[3], song_req = 1;
static uint32_t sec_start, sec_len;                 /* half-16ths of the BASE */
static uint8_t form_mute[TRACKS], form_override[TRACKS], last_want[TRACKS] = { 0, 1, 1, 1 };
static uint8_t trans_up, trans_drop, trans_down, fill_asked;
/* (fbright, the harmony's filter closing into a smaller section: with the small pieces above) */
static const struct sect_kind *sect(void) { return &SECT[FORM[base_style][song_var].s[sec_i][0]]; }
static int sect_next_i(void)   /* the next section's index: the song again without its intro */
{
    const struct form *F = &FORM[base_style][song_var];
    int n = sec_i + 1;
    if (n >= F->n) n = F->s[0][0] == S_INTRO && F->n > 1 ? 1 : 0;
    return n;
}
static float energy(void)   /* the knob's energy (K1 on BASE), and the section's when the form plays */
{
    if (!form_on) return c_energy;
    const struct sect_kind *k = sect();
    float done = sec_len ? (float)(bpos - sec_start) / (float)sec_len : 0.0f;
    float e = (k->e0 + (k->e1 - k->e0) * done) * c_energy * 2.0f;
    return e > 1.0f ? 1.0f : e;
}
static void pick_next_prog(void)
{
    if (c_prog != MAUTO) { next_prog_ix = c_prog; prog_auto = 0; return; }
    if (form_on) {   /* the section's progression, again; the next section's when this cycle ends it */
        prog_auto = 1;
        next_prog_ix = prog_ix;
        if (sec_start + sec_len <= bpos + prog_len(P(), cyc_speed))
            next_prog_ix = sec_prog[FORM[base_style][song_var].s[sect_next_i()][2]];
        return;
    }
    prog_auto = 1;
    next_prog_ix = prog_ix;
    if (rnd(100) < 40) do next_prog_ix = (uint8_t)rnd(4); while (next_prog_ix == prog_ix);
}
static void cycle_start(void)
{
    if (c_root >= 0) key_root = c_root;
    prog_ix = next_prog_ix;
    cyc_start = bpos;
    cyc_speed = c_speed;
    pick_next_prog();
}

/* --------------------------------------------------------------- the bass --- */
static struct {
    int mode; float ph[3], freq, target, env, fenv, gate_left, glide, acc; struct svf f; int on, rising;
} bass;
static void bass_play(int midi, float gate_samples, int slide, int accent)
{
    bass.target = mtof((float)midi);
    if (slide && bass.on && bass.gate_left > 0) {        /* a slide: no new attack, the pitch glides there */
        bass.glide = bass_slide;
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
    s = svf_lp(&bass.f, s, g_of(cut * bass_cut), (bass.mode == MTIMBRE_ACID ? 0.35f : 1.2f) * bass_k);
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
    int timbre; float sus, cut, famt, k, gain, ca, cd, cr, pan0, dec0, rel0;
};
static struct layer pad, rhy;
static void layer_shape(struct layer *L, int timbre, float atk, float dec, float sus, float rel,
                        float cut, float famt, float k, float gain)
{
    L->timbre = timbre; L->sus = sus; L->cut = cut; L->famt = famt; L->k = k; L->gain = gain;
    L->dec0 = dec; L->rel0 = rel;
    L->ca = ms_coef(atk); L->cd = ms_coef(dec); L->cr = ms_coef(rel);
}
static void layer_retime(struct layer *L, float mul)   /* the harmony's K4: its notes shorter or longer */
{
    L->cd = ms_coef(L->dec0 * mul); L->cr = ms_coef(L->rel0 * mul);
}
static void voice_lead(struct layer *L, const int pcs[3])   /* each voice: its chord tone's nearest octave */
{
    static const int8_t perm[6][3] = { {0,1,2}, {0,2,1}, {1,0,2}, {1,2,0}, {2,0,1}, {2,1,0} };
    int best[3] = { 0, 0, 0 }, best_cost = 1 << 30;
    for (int p = 0; p < 6; ++p) {
        int cand[3], cost = 0;
        for (int v = 0; v < 3; ++v) {
            int pc = pcs[perm[p][v]], old = L->voicing[v], n = old - ((old - pc) % 12 + 12) % 12;
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
static void layer_chord(struct layer *L, const struct chord *c, const int idx[3], int octave_up, float vel,
                        int gate_steps, int legato)
{
    int pcs[3] = { chord_pc(c, idx[0]), chord_pc(c, idx[1]), chord_pc(c, idx[2]) };
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
            float g2 = L->timbre == T_STRINGS ? g_moving : g_of((L->cut + L->famt * h->fenv) * h_bright * fbright);
            s = svf_lp(&h->f, s, g2, L->k);
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
static struct { float wait; int octave; float vel; int gate; int live; const struct chord *c; } pend[8];
static void schedule(float delay, int octave, float vel, int gate, const struct chord *c)   /* c: a chord ahead, or 0 */
{
    for (int i = 0; i < 8; ++i) if (!pend[i].live) {
        pend[i].wait = delay; pend[i].octave = octave; pend[i].vel = vel; pend[i].gate = gate;
        pend[i].live = 1; pend[i].c = c;
        return;
    }
}
static void pend_tick(void)
{
    for (int i = 0; i < 8; ++i) if (pend[i].live && (pend[i].wait -= 1.0f) <= 0.0f) {
        pend[i].live = 0;
        layer_chord(&rhy, pend[i].c ? pend[i].c : cur_chord, TRIAD, pend[i].octave, pend[i].vel, pend[i].gate, 0);
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

static const int8_t FAMILY[MD_COUNT] = { 0, 0, 0, 0, 1, 1, 1, 2 };
static const char *const MD_NAME[MD_COUNT] = { "A", "B", "C", "D", "J COMP", "J HEMIOLA", "J PUSH", "STABS" };
static int md = MD_A, fam_left, build_was_on, rhythm_on, pad_on, pad_left, stress;
static uint8_t harm_mode_now = MHARM_MIX, harm_sound_now = MHSOUND_ORGAN;
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
    float e = energy();
    jz_hemiola = jz_kind == 1 || (jz_kind == 0 && !jz_last_hemiola && rnd(100) < (unsigned)(10 + 20 * e));
    jz_last_hemiola = jz_hemiola;
    jz_shift = jz_kind == 1 && rnd(2) ? 2 : 0;
    if (jz_hemiola) return;
    if (jz_kind == 2) { jz_bar[0] = rnd(3) ? M_SPACE : 2; jz_bar[1] = 2; return; }
    jz_bar[0] = rnd(100) < (unsigned)(16 - 14 * e) ? M_SPACE : (int)rnd(M_COUNT - 1);
    unsigned x = rnd(100);
    if (x < 45) jz_bar[1] = jz_bar[0];
    else if (x < 85) do jz_bar[1] = (int)rnd(M_COUNT - 1); while (jz_bar[1] == jz_bar[0]);
    else jz_bar[1] = M_SPACE;
    if (jz_bar[0] == M_SPACE && jz_bar[1] == M_SPACE) jz_bar[1] = 2;
}
static float vsps(int t) { return sps * 2.0f / (float)feel[t]; }   /* samples a 16th of the track's own */
static void jazz_hit(int v, int gate, float vel)
{
    int in_bar = v % 16;
    if (in_bar == 0 && jz_tied) { jz_tied = 0; return; }   /* after a push the downbeat is not struck again */
    if (in_bar == 14) {                                    /* the "and of 4": the next bar's chord, held over */
        uint32_t ahead = (uint32_t)(2.0f * 2.0f / (float)feel[TRK_HARMONY] + 0.5f);
        schedule(0, 0, vel, 3, chord_ahead(ahead));
        jz_tied = 1;
    } else schedule(0, 0, vel, gate, 0);
}
static void jazz_step(int v)
{
    if (v == 0) jazz_plan();
    if (jz_hemiola) {
        int t = v - jz_shift;
        if (t >= 0 && t % 6 == 0 && !(jz_shift && t == 30)) jazz_hit(v, 2, t % 12 ? 0.75f : 0.9f);
        return;
    }
    const struct motif *m = &MOT[jz_bar[v / 16]];
    int fired = 0;
    for (int i = 0; i < m->n; ++i) if (m->h[i].at == v % 16) { jazz_hit(v, m->h[i].gate, m->h[i].vel); fired = 1; }
    if (!fired && v == 30 && jz_bar[1] != M_SPACE) {
        int room = 1;
        for (int i = 0; i < MOT[jz_bar[1]].n; ++i) if (MOT[jz_bar[1]].h[i].at >= 12) room = 0;
        if (room && rnd(100) < 60) jazz_hit(v, 3, 0.85f);
    }
    if (v % 16 == 0) jz_tied = 0;
}
static void skank_step(int v)
{
    int in8 = v % 8;
    float e = energy();
    switch (md) {
    case MD_A: if (in8 == 4) schedule(0, 0, 0.9f, 1, 0); break;
    case MD_B: if (in8 == 4) schedule(0, 0, 0.9f, 1, 0); else if (in8 == 6 && frnd() < 0.5f + e) schedule(0, 0, 0.45f, 1, 0); break;
    case MD_C: if (in8 == 4) schedule(0, 0, 0.9f, 1, 0); else if ((in8 == 2 || in8 == 6) && frnd() < 0.5f + e) schedule(0, 1, 0.4f, 1, 0); break;
    case MD_D: if (v % 4 == 2) schedule(0, 0, 0.9f, 1, 0); break;
    case MD_STABS:
        if (stab_mask >> (v % 16) & 1u && frnd() < 0.6f + 0.4f * e)
            schedule(frnd() * 0.004f * SR, 1, 0.8f + 0.2f * frnd(), 2, 0);
        break;
    default: break;
    }
}
static uint16_t harm_md_mask(void)   /* the figures this mode allows */
{
    switch (harm_mode_now) {
    case MHARM_SKANK: return M_SKANK;
    case MHARM_JAZZ: return M_JAZZ;
    case MHARM_MIX: case MHARM_MIX_PAD: return M_SKANK | M_JAZZ;
    case MHARM_STABS: return B(MD_STABS);
    default: return STYLE[track_style(TRK_HARMONY)].harm_md;
    }
}
static int pick_in(uint16_t mask, int fam)
{
    int m, tries = 0;
    do m = (int)rnd(MD_COUNT); while ((!(mask >> m & 1u) || (fam >= 0 && FAMILY[m] != fam) || (m == md && rnd(3))) && ++tries < 64);
    return mask >> m & 1u ? m : md;
}
static void rhythm_phrase(void)
{
    const struct style *S = &STYLE[track_style(TRK_HARMONY)];
    uint16_t mask = harm_md_mask();
    int fam = FAMILY[md];
    if (build_was_on && (S->stabs_after_build || harm_mode_now == MHARM_STABS)) {
        md = MD_STABS; fam_left = 1; build_was_on = 0; goto set;
    }
    build_was_on = 0;
    if (!(mask >> md & 1u) || --fam_left <= 0) {   /* a family stays 2 to 4 phrases */
        int fams[3], n = 0;
        for (int f = 0; f < 3; ++f) {
            int any = 0;
            for (int m = 0; m < MD_COUNT; ++m) if (mask >> m & 1u && FAMILY[m] == f) any = 1;
            if (any && (f != FAMILY[md] || rnd(10) < 2)) fams[n++] = f;
        }
        fam = n ? fams[rnd((unsigned)n)] : fam;
        fam_left = 2 + (int)rnd(3);
        md = pick_in(mask, fam);
    } else if (rnd(10) < 6) md = pick_in(mask, fam);
set:
    if (md == MD_STABS) { rhythm_set(R_STABS); stab_mask = STAB_PATTERNS[rnd(3)]; }
    else if (FAMILY[md] == 1) {
        rhythm_set(R_SKANK); rhy.lo = 58; rhy.hi = 74; jz_kind = md - MD_JCOMP;
        layer_shape(&rhy, T_ORGAN, 3, 160, 0.55f, 45, 5000, 0, 1.4f, 0.26f);
    } else rhythm_set(md == MD_C ? R_BUBBLE : R_SKANK);
    if (harm_sound_now == MHSOUND_SAW && md != MD_STABS) {   /* the same rhythm on detuned saws through a filter */
        rhy.timbre = T_STAB; rhy.cut = 1100.0f; rhy.famt = 2600.0f; rhy.k = 0.9f; rhy.gain *= 0.8f;
    }
    layer_retime(&rhy, h_decay);
}
static void harmony_phrase(void);
static void harmony_section(void)   /* a section starts: the harmony in AUTO does as it says, now */
{
    if (!form_on || c_harm != MAUTO || stress) return;
    harmony_phrase();
}
static void harmony_phrase(void)   /* the harmony track's own phrase starts */
{
    const struct style *S = &STYLE[track_style(TRK_HARMONY)];
    harm_mode_now = c_harm;
    harm_sound_now = c_hsound == MAUTO ? S->harm_sound : c_hsound;
    if (stress) {   /* everything held: the CPU's worst case */
        rhythm_on = 0;
        pad_on = 1;
        rhythm_set(R_STABS); rhy.sus = 0.5f;
        layer_chord(&rhy, cur_chord, TRIAD, 1, 0.6f, 0, 1);
        return;
    }
    rhythm_on = harm_mode_now != MHARM_PAD && !(form_on && harm_mode_now == MAUTO && sect()->harm == H_PAD);
    if (rhythm_on) {
        if (form_on && harm_mode_now == MAUTO && sect() == &SECT[S_DROP] && S->stabs_after_build) build_was_on = 1;
        rhythm_phrase();
    } else layer_release(&rhy);
    if (harm_mode_now == MHARM_PAD || harm_mode_now == MHARM_MIX_PAD) pad_on = 1;
    else if (harm_mode_now != MAUTO) pad_on = 0;
    else if (form_on && sect()->harm != H_RHY) pad_on = 1;   /* the section's pad */
    else if (--pad_left <= 0) { pad_on = rnd(100) < S->pad_pct; pad_left = 2 + (int)rnd(3); }   /* AUTO: a few phrases */
    if (pad_on) layer_chord(&pad, cur_chord, COLOUR, 0, 1.0f, 0, 1);
    else layer_release(&pad);
}

/* ------------------------------------------------------- the bass line --- */
static int form_hole(int t);
/* An event's pitch: a chord degree (0 root 1 3rd 2 5th 3 7th 4 octave 5 / 6 a half step under / over the next
 * bar's root), or steps of the chord's scale over its root (K_SCALE), both read from the chord sounding then */
enum { K_DEG, K_SCALE };
struct bev { int8_t on, kind, deg, len, slide, acc; };
static struct bev bseq[32], riff[16], acid[16];
static int b_age, b_left, bmode = MBASS_LONG, bass_sound_now = MTIMBRE_REESE;
static int root_midi(const struct chord *c) { return 33 + ((key_root + c->root) % 12 + 3) % 12; }   /* A1..G#2 */
static int scale_note(const struct chord *c, int idx)   /* the idx-th step of the chord's scale over its root */
{
    uint16_t m = SCALE[c->sc];
    int n = 0, steps[12];
    for (int i = 0; i < 12; ++i) if (m >> i & 1u) steps[n++] = i;
    int oct = 0;
    while (idx < 0) { idx += n; --oct; }
    while (idx >= n) { idx -= n; ++oct; }
    return root_midi(c) + steps[idx] + 12 * oct;
}
static int bev_note(const struct bev *e, int v)
{
    if (e->kind == K_SCALE) return scale_note(cur_chord, e->deg);
    if (e->deg == 5 || e->deg == 6) {   /* into the next bar's root (the track's bar, at its speed) */
        uint32_t ahead = (uint32_t)((float)(16 - v % 16) * 2.0f / (float)feel[TRK_BASS] + 0.5f);
        return root_midi(chord_ahead(ahead)) + (e->deg == 5 ? -1 : 1);
    }
    if (e->deg == 4) return root_midi(cur_chord) + 12;
    return root_midi(cur_chord) + (QUAL[cur_chord->q][e->deg] % 12);
}
static int pick_deg(void) { unsigned x = rnd(100); return x < 35 ? 0 : x < 55 ? 4 : x < 80 ? 2 : x < 90 ? 3 : 1; }
static void set_ev(struct bev *e, int kind, int deg, int len, int slide, int acc)
{
    e->on = 1; e->kind = (int8_t)kind; e->deg = (int8_t)deg; e->len = (int8_t)len; e->slide = (int8_t)slide; e->acc = (int8_t)acc;
}
static void make_cells(const int8_t (*T)[5][2], int nt, int scale)   /* a riff from one of nt rhythm cells */
{
    int t = (int)rnd((unsigned)nt), prev = 0;
    memset(riff, 0, sizeof riff);
    for (int i = 0; i < 5 && T[t][i][1]; ++i) {
        int d = i == 0 ? 0 : scale ? (int)rnd(6) - 1 : pick_deg();
        set_ev(&riff[T[t][i][0]], scale ? K_SCALE : K_DEG, d, T[t][i][1], i && d != prev && rnd(100) < 22, i == 0 || rnd(3) == 0);
        prev = d;
    }
}
static const int8_t RAGGA_CELLS[5][5][2] = {   /* {step, length}; length 0 ends */
    { {0, 3}, {3, 1}, {6, 2}, {10, 3}, {14, 2} },
    { {0, 4}, {6, 2}, {8, 1}, {10, 4}, {0, 0} },
    { {0, 2}, {3, 2}, {8, 3}, {11, 1}, {12, 3} },
    { {0, 6}, {7, 1}, {8, 2}, {12, 4}, {0, 0} },
    { {0, 2}, {2, 1}, {6, 4}, {11, 3}, {14, 1} },
};
static const int8_t ONEDROP_CELLS[4][5][2] = {   /* beat one empty: the bass comes in after it */
    { {4, 3}, {8, 2}, {10, 2}, {12, 4}, {0, 0} },
    { {2, 2}, {6, 2}, {8, 4}, {14, 2}, {0, 0} },
    { {4, 6}, {11, 1}, {12, 3}, {0, 0}, {0, 0} },
    { {6, 2}, {8, 6}, {14, 2}, {0, 0}, {0, 0} },
};
static const int8_t RIFF_CELLS[4][5][2] = {
    { {0, 2}, {3, 1}, {4, 2}, {7, 1}, {10, 4} },
    { {0, 3}, {3, 3}, {6, 2}, {8, 6}, {0, 0} },
    { {0, 1}, {2, 1}, {4, 2}, {8, 2}, {12, 3} },
    { {0, 4}, {6, 1}, {7, 1}, {8, 4}, {14, 2} },
};
static void make_acid(int mutate)
{
    static const int8_t STEPS[11] = { 0, 0, 0, 0, 7, 7, 1, 2, 4, 3, -1 };   /* scale steps: 7 is the octave of a 7-note scale */
    float e = energy();
    int from = mutate ? (int)rnd(16) : 0, to = mutate ? from + 2 : 16;
    for (int k = from; k < to; ++k) {
        int i = k & 15;
        struct bev *ev = &acid[i];
        ev->on = (int8_t)(i == 0 || frnd() < 0.42f + 0.4f * e); ev->kind = K_SCALE; ev->deg = STEPS[rnd(11)]; ev->len = 1;
        ev->slide = (int8_t)(rnd(100) < 24); ev->acc = (int8_t)(rnd(100) < 30);
    }
    acid[0].deg = 0;
}
static void make_walk(void)   /* quarter notes: root, a step or a chord tone, the fifth, into the next root */
{
    float e = energy();
    for (int bar = 0; bar < 2; ++bar) {
        int b = bar * 16, up = (int)rnd(2);
        set_ev(&bseq[b], K_DEG, 0, 3, 0, 1);
        set_ev(&bseq[b + 4], K_SCALE, up ? 1 : -1, 3, 0, 0);
        set_ev(&bseq[b + 8], rnd(2) ? K_DEG : K_SCALE, rnd(2) ? 2 : (up ? 2 : -2), 3, 0, 0);
        set_ev(&bseq[b + 12], K_DEG, rnd(2) ? 5 : 6, 3, 0, 0);
        if (frnd() < 0.3f * e) { bseq[b + 12].len = 2; set_ev(&bseq[b + 14], K_DEG, rnd(2) ? 5 : 6, 2, 0, 0); bseq[b + 12].deg = 2; }
    }
}
static int pick_bass_mode(const struct style *S)
{
    unsigned tot = 0, x;
    for (int m = 0; m < MBASS_MODES; ++m) tot += S->bass_w[m];
    x = rnd(tot ? tot : 1);
    for (int m = 0; m < MBASS_MODES; ++m) { if (x < S->bass_w[m]) return m; x -= S->bass_w[m]; }
    return MBASS_LONG;
}
static void bass_plan(void)   /* the bass track's own phrase starts */
{
    const struct style *S = &STYLE[track_style(TRK_BASS)];
    int want_mode = c_bass_mode == MAUTO ? -1 : c_bass_mode;
    float e = energy();
    if (want_mode >= 0) { if (want_mode != bmode) { bmode = want_mode; b_age = 0; } }
    else if (form_on && sect()->bass != BL_STYLE) {   /* the section says: held roots, or the style's strongest */
        int m = MBASS_LONG;
        if (sect()->bass == BL_STRONG) {
            unsigned best = 0;
            for (int i = 0; i < MBASS_MODES; ++i) if (i != MBASS_KICK && S->bass_w[i] > best) { best = S->bass_w[i]; m = i; }
        }
        if (m != bmode) { bmode = m; b_age = 0; }
    } else if (--b_left <= 0) {   /* AUTO: a line a few phrases at a time */
        int m = pick_bass_mode(S);
        if (m != bmode) { bmode = m; b_age = 0; }
        b_left = 2 + (int)rnd(3);
    }
    bass_sound_now = c_timbre == MAUTO ? S->bass_sound : c_timbre;
    memset(bseq, 0, sizeof bseq);
    switch (bmode) {
    case MBASS_LONG:
        for (int bar = 0; bar < 2; ++bar) {
            int split = frnd() < 0.25f + 0.4f * e;
            set_ev(&bseq[bar * 16], K_DEG, 0, split ? 10 : 16, 0, 1);
            if (split) set_ev(&bseq[bar * 16 + 10], K_DEG, rnd(2) ? 2 : 4, 6, (int)rnd(2), 0);
        }
        if (rnd(100) < 55) {   /* the push: into the next phrase's root, an 8th early */
            if (bseq[16].len > 14) bseq[16].len = 14;
            if (bseq[26].on) bseq[26].len = 4;
            set_ev(&bseq[30], K_DEG, rnd(2) ? 5 : 6, 2, 1, 1);
        }
        break;
    case MBASS_RAGGA: case MBASS_ONEDROP: case MBASS_RIFF:
        if (b_age++ % 2 == 0) {   /* a riff lives two phrases */
            if (bmode == MBASS_RAGGA) make_cells(RAGGA_CELLS, 5, 0);
            else if (bmode == MBASS_ONEDROP) make_cells(ONEDROP_CELLS, 4, 0);
            else make_cells(RIFF_CELLS, 4, 1);
        }
        for (int bar = 0; bar < 2; ++bar) for (int i = 0; i < 16; ++i) bseq[bar * 16 + i] = riff[i];
        if (rnd(100) < 60) {      /* the answer: walking into the next root */
            for (int i = 12; i < 16; ++i) bseq[16 + i].on = 0;
            set_ev(&bseq[28], K_DEG, 2, 2, 0, 0);
            set_ev(&bseq[30], K_DEG, rnd(2) ? 5 : 6, 2, 1, 1);
        }
        break;
    case MBASS_ACID:
        make_acid(b_age++ != 0);  /* the same line, two steps changed a phrase */
        for (int bar = 0; bar < 2; ++bar) for (int i = 0; i < 16; ++i) bseq[bar * 16 + i] = acid[i];
        break;
    case MBASS_WALK:
        make_walk();
        break;
    default: break;
    }
}
static void bass_step(int v)
{
    if (v == 0) bass_plan();
    if (bmode == MBASS_KICK || jungle_roll_active() == 1) return;   /* BUILD: the bass waits for the drop */
    if (form_hole(TRK_BASS)) { bass.gate_left = 0.0f; return; }
    struct bev *e = &bseq[v];
    if (!e->on) return;
    int len = e->len, slide = e->slide;
    if (bmode == MBASS_ACID) {
        len = 1;
        if (e->slide) { int j = v + 1; while (j < 32 && !bseq[j].on) ++j; len = j - v + 1; }
        slide = v > 0 && bseq[v - 1].slide && bseq[v - 1].on;
    }
    bass_play(bev_note(e, v), (float)len * vsps(TRK_BASS) * (len == 1 ? 0.55f : 0.92f), slide, e->acc);
}

/* ------------------------------------------------------------- the form --- */
static void harmony_section(void);
static void section_enter(int i)
{
    const struct form *F = &FORM[base_style][song_var];
    const struct sect_kind *k;
    uint32_t plen, bars;
    int up = trans_up || trans_drop;
    sec_i = (uint8_t)i;
    k = sect();
    if (c_prog == MAUTO) prog_ix = sec_prog[F->s[i][2]];
    next_prog_ix = prog_ix;
    cyc_start = bpos;   /* the progression from its first chord */
    cyc_speed = c_speed;
    if (c_root >= 0) key_root = c_root;
    /* the section's length: whole progressions (a blues is not cut: a progression longer than the section stretches it) */
    plen = prog_len(P(), cyc_speed) / 32u;
    bars = F->s[i][1];
    if (plen) { uint32_t n = (bars + plen / 2u) / plen; bars = (n ? n : 1u) * plen; }
    sec_len = bars * 32u;
    pick_next_prog();
    /* its tracks: the form's own mutes (yours stay yours); an intro has the bass or the pad */
    for (int t = 0; t < TRACKS; ++t) { form_mute[t] = 0; form_override[t] = 0; }
    if (!(k->play & PL_BREAK) && c_form == 2) form_mute[TRK_BREAK] = 1;
    if (F->s[i][0] == S_INTRO) form_mute[rnd(2) ? TRK_BASS : TRK_HARMONY] = 1;
    b_left = 0;                                     /* the bass picks its line again */
    harmony_section();
    if (up && !heard_muted[TRK_HARMONY] && !form_mute[TRK_HARMONY])   /* the downbeat after a rise: a chord struck */
        schedule(0, 0, 1.0f, 4, 0);
    trans_up = trans_drop = trans_down = 0;
    fill_asked = 0;
    fbright = 1.0f;
}
static void song_new(void)   /* at PLAY, at a new style, with the form turned on: a form, three progressions */
{
    song_var = (uint8_t)rnd(2);
    sec_prog[0] = (uint8_t)rnd(4);
    do sec_prog[1] = (uint8_t)rnd(4); while (sec_prog[1] == sec_prog[0]);
    do sec_prog[2] = (uint8_t)rnd(4); while (sec_prog[2] == sec_prog[0] || sec_prog[2] == sec_prog[1]);
    trans_up = trans_drop = trans_down = 0;
    section_enter(0);
}
static void form_step(void)   /* each 16th of the break, the form playing */
{
    uint32_t left_h = sec_start + sec_len > bpos ? sec_start + sec_len - bpos : 0;
    uint32_t left = left_h / feel[TRK_BREAK];      /* real 16ths to the section's end */
    if (!left) { sec_start += sec_len; section_enter(sect_next_i()); return; }
    if (left <= 32u && !trans_up && !trans_drop && !trans_down) {   /* the last phrase: which way it goes */
        const struct sect_kind *now = sect(), *next = &SECT[FORM[base_style][song_var].s[sect_next_i()][0]];
        if (next == &SECT[S_DROP] && now != &SECT[S_DROP]) trans_drop = 1;
        else if (next->e0 > now->e1 + 0.1f) trans_up = 1;
        else if (next->e0 < now->e1 - 0.1f) trans_down = 1;
    }
    /* the fill, asked a phrase ahead (the break chooses its fill as its phrase starts); not over a roll of yours */
    if (c_form == 2 && !fill_asked && left > 32u && left <= 40u && jungle_roll_active() < 0) {
        const struct sect_kind *now = sect(), *next = &SECT[FORM[base_style][song_var].s[sect_next_i()][0]];
        if (next == &SECT[S_DROP] && now != &SECT[S_DROP]) jungle_request_fill(1, 16);   /* BUILD, the whole last bar */
        else if (next->e0 > now->e1 + 0.1f) jungle_request_fill(STYLE_FILL[base_style], 8);
        fill_asked = 1;
    }
    fbright = trans_down && left <= 32u ? 0.3f + 0.7f * (float)left / 32.0f : 1.0f;
    if (left == 2u && trans_up && !heard_muted[TRK_BASS] && !form_mute[TRK_BASS] && bmode != MBASS_KICK) {
        uint32_t ahead = 2u;   /* the bass's push: a half step under the next root, sliding in */
        bass_play(root_midi(chord_ahead(ahead)) - 1, 2.0f * sps, 0, 1);
    }
}
static int form_hole(int t)   /* the harmony's hole before a rise (half a bar), everything's last bar before a drop */
{
    uint32_t left;
    if (!form_on || !(trans_up || trans_drop)) return 0;
    left = (sec_start + sec_len - bpos) / feel[TRK_BREAK];
    if (trans_drop) return left <= 16u;
    return t == TRK_HARMONY && left <= 8u;
}

/* ------------------------------------------------------ the tracks' clocks --- */
/* A track's 16th v (0..31, its phrase) from the break's 16th g: half speed on even g (v = g/2), double twice (2g now,
 * 2g+1 half a 16th later). Aligned on g, so every speed meets on the break's phrases. */
static float second_wait[TRACKS];
static uint8_t second_v[TRACKS];
static void harmony_vstep(int v)
{
    if (v == 0) harmony_phrase();
    layer_step_gates(&rhy);
    if (form_hole(TRK_HARMONY)) { layer_release(&rhy); if (trans_drop) layer_release(&pad); return; }
    if (rhythm_on) { if (FAMILY[md] == 1) jazz_step(v); else skank_step(v); }
}
static void track_vstep(int t, int v)
{
    if (t == TRK_BASS) bass_step(v);
    else if (t == TRK_HARMONY) harmony_vstep(v);
}
static void track_tick(int t)
{
    uint8_t f = feel[t];
    if (f == 2) track_vstep(t, (int)(g % 32u));
    else if (f == 1) { if (!(g & 1u)) track_vstep(t, (int)(g / 2u % 32u)); }
    else {
        track_vstep(t, (int)(2u * g % 32u));
        second_v[t] = (uint8_t)((2u * g + 1u) % 32u);
        second_wait[t] = sps * 0.5f;
    }
}

/* ------------------------------------------------------------- AUTO DUB --- */
static uint8_t dub_drop[TRACKS];   /* bars left out */
static void autodub_bar(void)
{
    int t, any = 0;
    for (t = 0; t < TRACKS; ++t) if (dub_drop[t]) { --dub_drop[t]; any = 1; }
    float amount = c_dub + (form_on ? sect()->dub : 0.0f);
    if (any || amount <= 0.0f || frnd() >= amount * 0.3f) return;
    static const uint8_t CAN[3] = { TRK_BREAK, TRK_BASS, TRK_HARMONY };
    t = CAN[rnd(3)];
    if (!heard_muted[t]) dub_drop[t] = (uint8_t)(1 + rnd(2));
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

static void on_step(int step, uint32_t at)
{
    if (have_step) {   /* the tempo, from the steps themselves: right with a MIDI clock, a tempo change, halving */
        uint32_t d = at - last_step_at;
        if (d > 1000u && d < 40000u) sps += ((float)d - sps) * 0.5f;
    }
    have_step = 1;
    last_step_at = at;
    if (step == 0) {
        ++phrase_count;
        for (int t = 0; t < TRACKS; ++t) if (want_q[t]) heard_muted[t] = want[t];   /* the mutes asked for the phrase */
        stress = c_stress;
    }
    g = phrase_count * 32u + (uint32_t)step;
    if (g % 64u == 0) {   /* every 4 bars (a phrase of every speed starts): the styles and their speeds */
        int new_style = c_style != base_style;
        base_style = c_style;
        feel[TRK_BREAK] = speed_for(STYLE[base_style].centre);
        for (int t = TRK_BASS; t < TRACKS; ++t) feel[t] = speed_for(STYLE[track_style(t)].centre);
        if (new_style) { next_prog_ix = c_prog == MAUTO ? (uint8_t)rnd(4) : c_prog; cycle_start(); song_req = 1; }
    }
    {   /* the form: on, off; a new song (at PLAY, a new style, turned on) from a phrase's start */
        int f = c_form != 0;
        if (f != form_on) { form_on = (uint8_t)f; song_req = 1; for (int t = 0; t < TRACKS; ++t) form_mute[t] = 0; }
        if (form_on && song_req && step == 0) { song_req = 0; sec_start = bpos; song_new(); }
    }
    if (c_prog != MAUTO && c_prog != prog_ix && step == 0) { next_prog_ix = c_prog; cycle_start(); }   /* a chosen one: now */
    if (form_on && !song_req) form_step();
    if (bpos - cyc_start >= prog_len(P(), cyc_speed)) cycle_start();
    {
        const struct chord *c = chord_in(P(), bpos - cyc_start, cyc_speed);
        if (c != cur_chord) { cur_chord = c; if (pad_on && !form_hole(TRK_HARMONY)) layer_chord(&pad, c, COLOUR, 0, 1.0f, 0, 1); }
    }
    if (g % 16u == 0) autodub_bar();
    track_tick(TRK_BASS);
    track_tick(TRK_HARMONY);
    if (jungle_roll_active() == 1) build_was_on = 1;
    bpos += feel[TRK_BREAK];   /* the BASE's time: its own speed */
}
static void on_hit(int slice, int pitch)
{
    static const uint16_t kick_mask = (1u << 0) | (1u << 1) | (1u << 5) | (1u << 9) | (1u << 13);   /* Amen */
    (void)pitch;
    if (bmode != MBASS_KICK || jungle_roll_active() == 1 || !cur_chord) return;
    if (!(kick_mask >> slice & 1u)) return;
    int note = root_midi(cur_chord);
    bass_play(note < 36 ? note + 12 : note, SR * 0.12f, jungle_roll_active() == 2, 1);
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
    bass_slide = ms_coef(25.0f);
    next_prog_ix = 0;
    cycle_start();
    cur_chord = chord_in(P(), 0, cyc_speed);
}

void music_view(struct music_view *v)
{
    v->style = base_style;
    v->prog = prog_ix;
    v->prog_auto = prog_auto;
    v->prog_name = P()->name;
    v->chord = cur_chord ? cur_chord->nm : "";
    v->bass_mode = (uint8_t)bmode;
    v->bass_sound = (uint8_t)bass_sound_now;
    v->harm_mode = harm_mode_now;
    v->harm_sound = harm_sound_now;
    v->harm_pad = (uint8_t)pad_on;
    v->harm_detail = rhythm_on ? MD_NAME[md] : "";
    for (int t = 0; t < TRACKS; ++t) v->feel[t] = feel[t];
    v->form = c_form;
    v->section = form_on && !song_req ? sect()->name : "";
    v->sect_bar = (uint8_t)(form_on && !song_req ? (bpos - sec_start) / 32u + 1u : 0u);
    v->sect_bars = (uint8_t)(form_on && !song_req ? sec_len / 32u : 0u);
}

enum { CHUNK = 256 };
static float buf_bass[CHUNK], buf_harm[2 * CHUNK];
static float gain[TRACKS] = { 1.0f, 0.0f, 0.0f, 0.0f };
static void render_chunk(int32_t *lr, unsigned n)
{
    unsigned e = 0;
    float tgt[TRACKS];
    for (int t = 0; t < TRACKS; ++t) {
        if (!want_q[t]) heard_muted[t] = want[t];   /* a plain tap: now */
        if (want[t] != last_want[t]) { last_want[t] = want[t]; form_override[t] = 1; }   /* your hand: the form lets go */
        tgt[t] = c_stress && t != TRK_BREAK ? lvl[t] :
                 (heard_muted[t] || dub_drop[t] || (form_on && form_mute[t] && !form_override[t])) ? 0.0f : lvl[t];
    }
    bass.mode = bass_sound_now;
    {   /* the transport stopped: every held note lets go (the pad's, the rhythm layer's, the bass's), nothing waits */
        static int was_playing;
        int playing = jungle_playing();
        if (was_playing && !playing) {   /* quickly: a 120 ms release, not the pad's 600 */
            pad.cr = rhy.cr = ms_coef(120.0f);
            layer_release(&pad);
            layer_release(&rhy);
            bass.gate_left = 0.0f;
            for (int i = 0; i < 8; ++i) pend[i].live = 0;
            second_wait[TRK_BASS] = second_wait[TRK_HARMONY] = 0.0f;
        }
        if (!was_playing && playing) { layer_retime(&pad, 1.0f); layer_retime(&rhy, h_decay); song_req = 1; }   /* their own again; a new song */
        was_playing = playing;
    }
    if (h_retime) { h_retime = 0; layer_retime(&rhy, h_decay); }
    static float lfo_ph;
    lfo_ph += (float)n / (sps * 32.0f);
    if (lfo_ph >= 1.0f) lfo_ph -= 1.0f;
    float tri = lfo_ph < 0.5f ? 2.0f * lfo_ph : 2.0f - 2.0f * lfo_ph;
    float g_pad = g_of(900.0f * h_bright * fbright * (0.45f + 2.6f * h_move * tri * tri));
    /* the tracks, each in its own buffer (the events at their frames) */
    for (unsigned i = 0; i < n; ++i) {
        while (e < nev && ev[e].frame <= i) {
            if (ev[e].type == EV_STEP) on_step(ev[e].a, frame_base + i); else on_hit(ev[e].a, ev[e].b);
            ++e;
        }
        for (int t = TRK_BASS; t <= TRK_HARMONY; ++t)   /* a double-speed track's second 16th */
            if (second_wait[t] > 0.0f && (second_wait[t] -= 1.0f) <= 0.0f) track_vstep(t, second_v[t]);
        pend_tick();
        float l = 0.0f, r = 0.0f;
        buf_bass[i] = bass_render() * 0.55f;
        layer_render(&pad, &l, &r, g_pad);
        layer_render(&rhy, &l, &r, g_pad);
        buf_harm[2 * i] = l;
        buf_harm[2 * i + 1] = r;
    }
    for (; e < nev; ++e) if (ev[e].type == EV_STEP) on_step(ev[e].a, frame_base + n);
    nev = 0;
    frame_base += n;
    /* the mix: each track's gain glides to its target in ~5 ms (no click); the break's own gain on what jungle.c made */
    const float glide = 1.0f / 220.0f;
    for (unsigned i = 0; i < n; ++i) {
        for (int t = 0; t < TRACKS; ++t) {
            float d = tgt[t] - gain[t];
            gain[t] = d > glide ? gain[t] + glide : d < -glide ? gain[t] - glide : tgt[t];
        }
        float b = buf_bass[i] * gain[TRK_BASS];
        float L = 0.75f * soft(1.2f * (b + buf_harm[2 * i] * gain[TRK_HARMONY]));
        float R = 0.75f * soft(1.2f * (b + buf_harm[2 * i + 1] * gain[TRK_HARMONY]));
        int32_t a = lr[2 * i], c = lr[2 * i + 1];
        if (gain[TRK_BREAK] != 1.0f) { a = (int32_t)((float)a * gain[TRK_BREAK]); c = (int32_t)((float)c * gain[TRK_BREAK]); }
        a += (int32_t)(L * 8388607.0f);
        c += (int32_t)(R * 8388607.0f);
        lr[2 * i] = a > 8388607 ? 8388607 : a < -8388607 ? -8388607 : a;          /* 24 bits: never wrap */
        lr[2 * i + 1] = c > 8388607 ? 8388607 : c < -8388607 ? -8388607 : c;
    }
}
void music_render(int32_t *lr, unsigned n)
{
    while (n > CHUNK) { render_chunk(lr, CHUNK); lr += 2 * CHUNK; n -= CHUNK; }   /* (the FM-1's block is 256) */
    render_chunk(lr, n);
}
