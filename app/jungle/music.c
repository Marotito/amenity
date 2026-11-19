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
#include "plat.h"   /* PLAT_POOL: the effects' lines (F6a) */
#include "fm6_tables.h"    /* F6d: the FM6 core's tables (tools/gen_fm6_tables.py) */
#include "fm6_core.h"      /* F6d: msfa (Dexed's 6-operator FM), Felucca's integer C port, Apache-2.0 */
#include "fm6_factory.h"   /* F6d: M-VAVE's factory DX7 bank, 128 voices, Apache-2.0 */

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
/* the state-variable low-pass (TPT): its coefficients set now and then (svf_set: every 16 samples, F6a), run each sample */
struct svf { float ic1, ic2, g, a1, a2; };
static void svf_set(struct svf *f, float g, float k)
{
    f->g = g;
    f->a1 = 1.0f / (1.0f + g * (g + k));
    f->a2 = g * f->a1;
}
static float svf_run(struct svf *f, float x)
{
    float v3 = x - f->ic2;
    float v1 = f->a1 * f->ic1 + f->a2 * v3, v2 = f->ic2 + f->g * v1;
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
      { 15, 40, 30, 15, 0, 0, 0 }, MTIMBRE_REESE, B(MD_A) | B(MD_D) | M_JAZZ, 70, MHSOUND_HARPSI, 1 },
    { "JAZZ", 100.0f, {
        { "DORIAN", 2, { {0,Q_MIN,S_DOR,16,"i7"}, {5,Q_DOM,S_MIX,16,"IV7"} } },
        { "II-V-I", 3, { {2,Q_HDIM,S_LOC,4,"ii0"}, {7,Q_DOM,S_PHRDOM,4,"V7"}, {0,Q_MIN,S_MEL,8,"i7"} } },
        { "DESCENT", 4, { {0,Q_MIN,S_DOR,4,"i7"}, {10,Q_DOM,S_MIX,4,"VII7"}, {8,Q_MAJ7,S_LYD,4,"VImaj"}, {7,Q_DOM,S_PHRDOM,4,"V7"} } },
        { "BLUES 12", 7, { {0,Q_MIN,S_DOR,16,"i7"}, {5,Q_MIN,S_DOR,8,"iv7"}, {0,Q_MIN,S_DOR,8,"i7"}, {8,Q_DOM,S_LYD,4,"VI7"},
                           {7,Q_DOM,S_PHRDOM,4,"V7"}, {0,Q_MIN,S_DOR,4,"i7"}, {7,Q_DOM,S_PHRDOM,4,"V7"} } } },
      { 0, 20, 0, 0, 80, 0, 0 }, MTIMBRE_UPRIGHT, M_JAZZ, 20, MHSOUND_EPIANO, 0 },
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
      { 0, 20, 20, 0, 0, 0, 60 }, MTIMBRE_SUB, B(MD_A) | B(MD_B) | B(MD_JCOMP), 40, MHSOUND_BANK + 7, 0 },   /* PIANO 8 (F6d) */
    { "DUB", 85.0f, {
        { "ONE CHORD", 1, { {0,Q_MIN,S_AEO,16,"i"} } },
        { "I-VII", 2, { {0,Q_MIN,S_AEO,8,"i"}, {10,Q_MAJ,S_MIX,8,"VII"} } },
        { "I-IV", 2, { {0,Q_MIN,S_DOR,8,"i"}, {5,Q_MIN,S_DOR,8,"iv"} } },
        { "ROOTS", 4, { {0,Q_MAJ,S_ION,4,"I"}, {5,Q_MAJ,S_LYD,4,"IV"}, {7,Q_DOM,S_MIX,4,"V"}, {5,Q_MAJ,S_LYD,4,"IV"} } } },
      { 0, 15, 25, 0, 0, 60, 0 }, MTIMBRE_DUB, B(MD_A) | B(MD_B) | B(MD_C), 15, MHSOUND_GUITAR, 0 },
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

/* ------------------------------------------------------------ the groove's data --- */
/* F5 (Marote, 18 nov): each style's swing (MPC's way: the second 16th of each 8th, at that % of the 8th), its HUMAN
 * (a middle value: how loose the hands), its strum (the rhythm layer's chord, voice after voice, ms at full HUMAN)
 * and its accents: a bar of 16ths, the loudness of a note on each (the downbeats, the backbeat, the ghosts). */
static const struct { uint8_t swing, human; float strum_ms; float acc[16]; } GROOVE[MSTYLES] = {
    { 50, 30, 2.0f, { 1.10f, .62f, .80f, .62f, 1.00f, .62f, .80f, .62f, 1.05f, .62f, .80f, .62f, 1.00f, .62f, .82f, .70f } },
    { 64, 70, 9.0f, { .90f, .55f, .95f, .55f, 1.10f, .55f, .95f, .55f, .90f, .55f, .95f, .55f, 1.10f, .55f, 1.0f, .60f } },
    { 54, 25, 0.0f, { 1.10f, .72f, .90f, .72f, 1.00f, .72f, .90f, .72f, 1.05f, .72f, .90f, .72f, 1.00f, .72f, .92f, .78f } },
    { 58, 60, 5.0f, { 1.10f, .50f, .72f, .55f, 1.05f, .50f, .78f, .55f, 1.00f, .50f, .72f, .60f, 1.05f, .50f, .85f, .62f } },
    { 56, 50, 4.0f, { .85f, .55f, .72f, .55f, 1.10f, .55f, .78f, .55f, .95f, .55f, .72f, .55f, 1.10f, .55f, .78f, .62f } },
};

/* ---------------------------------------------------------------- the controls --- */
static volatile uint8_t c_style = MSTYLE_BREAKCORE, c_prog = MAUTO, c_stress;
static volatile uint8_t c_tstyle[TRACKS] = { MSTYLE_BASE, MSTYLE_BASE, MSTYLE_BASE, MSTYLE_BASE };
static volatile uint8_t c_bass_mode = MAUTO, c_timbre = MAUTO, c_harm = MAUTO, c_hsound = MAUTO;
static volatile float c_energy = 0.5f, c_speed = 1.0f, c_dub;
static volatile uint8_t c_form;
static volatile uint8_t c_mmode = MAUTO, c_msound = MAUTO, c_mseed = MAUTO;
static volatile uint32_t c_snares = (1u << 2) | (1u << 7) | (1u << 10) | (1u << 15);   /* the Amen's, until told */
static volatile float m_bright = 1.0f, m_glide = 0.0f, m_vib = 0.0f;   /* K2..K4 of the melody */
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
    } else if (t == TRK_MELODY) {
        if (k == 0) m_bright = knob_mul(v);
        else if (k == 1) m_glide = v < 4u ? 0.0f : ms_coef(2.0f + (float)v * ((float)v * 0.01f));   /* off .. ~160 ms */
        else if (k == 2) m_vib = (float)v * (0.6f / 127.0f);   /* up to 0.6 semitone */
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
void music_mel_mode(unsigned m) { c_mmode = (uint8_t)(m < MMEL_MODES ? m : MAUTO); }
void music_mel_sound(unsigned m) { c_msound = (uint8_t)(m < MMSOUNDS ? m : MAUTO); }
void music_snares(uint32_t m) { c_snares = m; }
void music_form(unsigned v) { c_form = (uint8_t)(v < 43u ? 0 : v < 86u ? 1 : 2); }
void music_track_style(unsigned t, unsigned s) { if (t < TRACKS) c_tstyle[t] = (uint8_t)(s < MSTYLES ? s : MSTYLE_BASE); }
void music_bass_mode(unsigned m) { c_bass_mode = (uint8_t)(m < MBASS_MODES ? m : MAUTO); }
void music_bass_timbre(unsigned m) { c_timbre = (uint8_t)(m < MTIMBRES ? m : MAUTO); }
void music_harm_mode(unsigned m) { c_harm = (uint8_t)(m < MHARM_MODES ? m : MAUTO); }
void music_harm_sound(unsigned m) { c_hsound = (uint8_t)(m < MHSOUNDS ? m : MAUTO); }
void music_root(unsigned pc) { c_root = (int8_t)(pc % 12u); }
void music_stress(int on) { c_stress = (uint8_t)!!on; }
static volatile uint32_t c_seed, song_seed, blocks;
static volatile uint8_t c_seed_set, block_left = 64;
/* the groove (F5): the swing (50..75 %, MAUTO the style's), HUMAN (0..100, MAUTO the style's), each track's swing
 * (MAUTO: as the BASE's), each track's accents (1: the style's map as written, 0 flat, 2 doubled) */
static volatile uint8_t c_swing = MAUTO, c_human = MAUTO, c_tswing[TRACKS] = { 50, MAUTO, MAUTO, MAUTO };
static volatile float c_acc[TRACKS] = { 1.0f, 1.0f, 1.0f, 1.0f };
void music_swing(unsigned v) { c_swing = (uint8_t)(v >= 50u && v <= 75u ? v : MAUTO); }
void music_human(unsigned v) { c_human = (uint8_t)(v <= 100u ? v : MAUTO); }
void music_track_swing(unsigned t, unsigned v) { if (t < TRACKS) c_tswing[t] = (uint8_t)(v >= 50u && v <= 75u ? v : MAUTO); }
void music_accent(unsigned t, unsigned v) { if (t < TRACKS) c_acc[t] = (float)v * (1.0f / 64.0f); }
void music_seed(uint32_t seed) { c_seed = seed; c_seed_set = 1; }
uint32_t music_song_seed(void) { return song_seed; }
unsigned music_block_left(void) { return block_left; }
uint32_t music_blocks(void) { return blocks; }

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
/* the groove: a random of its own (the music's rng stays the song's: a slot's seed plays the same song) */
static uint32_t hrng = 0x9e3779b9u;
static float hrand(void) { hrng = hrng * 1664525u + 1013904223u; return (float)((hrng >> 8) & 0xffffu) * (1.0f / 65536.0f); }
static unsigned swing_pct(int t)   /* the swing a track plays: its own, the BASE's, or its style's */
{
    unsigned p = c_tswing[t];
    if (p == MAUTO) p = c_swing != MAUTO ? c_swing : GROOVE[track_style(t)].swing;
    return p;
}
static float swing_of(int t) { return (float)swing_pct(t) * 0.02f - 1.0f; }   /* an odd 16th's lateness, in 16ths: 0 .. 0.5 */
static float human_of(int t) { return (float)(c_human == MAUTO ? GROOVE[track_style(t)].human : c_human) * 0.01f; }
static float acc_now[TRACKS] = { 1.0f, 1.0f, 1.0f, 1.0f };   /* the accent of the 16th a track plays now */
static float note_vel(int t)   /* a note's loudness: its 16th's accent, a hand's unevenness */
{
    float v = acc_now[t] * (1.0f + human_of(t) * 0.16f * (2.0f * hrand() - 1.0f));
    return v < 0.2f ? 0.2f : v > 1.25f ? 1.25f : v;
}
static float gate_mul(int t, float vel)   /* louder notes held a little longer, ghosts shorter, a hand's unevenness */
{
    float m = 1.0f + 0.45f * (vel - 0.9f) + human_of(t) * 0.12f * (2.0f * hrand() - 1.0f);
    return m < 0.7f ? 0.7f : m > 1.15f ? 1.15f : m;
}
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

/* ------------------------------------------------- a plucked string (F6b) --- */
/* Karplus-Strong: a line a period long, filled with a burst of noise (darker: a softer pluck), round and round
 * through a little low-pass (the string's loss: its brightness dying) and an allpass (the fraction of a sample that
 * tunes it). CLAVI, HARPSI, GUITAR, UPRIGHT, PLUCK: one string, its pluck, its loss, its damper. Its noise its own:
 * the song's dice stay the song's. */
static uint32_t nz = 0x1234567u;
static float nrand(void) { nz = nz * 1664525u + 1013904223u; return (float)(int32_t)nz * (1.0f / 2147483648.0f); }
struct ks { float *b; uint16_t cap, n, w; float prev, damp, loss, apc, apx, apy; };
static void ks_pluck(struct ks *k, float hz, float bright, float damp, float t60, float vel, float comb)
{
    if (!k->b) return;
    float period = SR / hz, d = period - damp;              /* the loop: n samples + the low-pass's + the allpass's */
    int n = (int)d;
    float fr = d - (float)n;
    if (fr < 0.15f) { --n; fr += 1.0f; }
    if (n > (int)k->cap - 1) n = (int)k->cap - 1;
    if (n < 4) n = 4;
    k->apc = (1.0f - fr) / (1.0f + fr);
    float lp = 0.0f, mean = 0.0f;
    for (int i = 0; i < n; ++i) { lp += (nrand() - lp) * bright; k->b[i] = lp; mean += lp; }
    mean /= (float)n;
    int cp = (int)((float)n * comb);                       /* where it is plucked: a comb on the burst (thinner near the bridge) */
    for (int i = n - 1; i >= 0; --i) k->b[i] = (k->b[i] - mean - (cp > 0 && i >= cp ? k->b[i - cp] - mean : 0.0f)) * vel * 1.4f;
    k->n = (uint16_t)n; k->w = 0; k->prev = 0.0f; k->apx = k->apy = 0.0f;
    k->damp = damp;
    k->loss = fm_expf(-6.9f * period / (t60 * SR));
}
static float ks_run(struct ks *k)
{
    if (!k->n) return 0.0f;
    float y = k->b[k->w];
    float f = (y + (k->prev - y) * k->damp) * k->loss;     /* the loss: a little low-pass, a little less each time */
    k->prev = y;
    float a = k->apc * f + k->apx - k->apc * k->apy;       /* the tuning */
    k->apx = f; k->apy = a;
    k->b[k->w] = a;
    if (++k->w >= k->n) k->w = 0;
    return y;
}
static void ks_mute(struct ks *k, float t60) { if (k->n) k->loss = fm_expf(-6.9f * (float)k->n / (t60 * SR)); }   /* the damper */
static float ks_rhy_buf[3][512] PLAT_POOL, ks_lead_buf[512] PLAT_POOL;
/* the drift (F6b): an analogue's pitch wandering, a few cents, slow; one per voice, moved once a chunk */
static float drift[8], drift_to[8];
static void drift_chunk(unsigned n)
{
    static float t;
    t += (float)n;
    if (t > 0.4f * SR) { t = 0.0f; for (int i = 0; i < 8; ++i) drift_to[i] = nrand(); }
    for (int i = 0; i < 8; ++i) drift[i] += (drift_to[i] - drift[i]) * 0.01f;
}
#define DRIFT(i) (1.0f + 0.0017f * drift[i])               /* +-3 cents */

/* --------------------------------------------------------------- the bass --- */
static struct {
    int mode; float ph[3], freq, target, env, fenv, gate_left, glide, acc, vel; struct svf f, f2; int on, rising, kc;
} bass;
static void bass_play(int midi, float gate_samples, int slide, int accent, float vel)
{
    bass.target = mtof((float)midi);
    if (slide && bass.on && bass.gate_left > 0) {        /* a slide: no new attack, the pitch glides there */
        bass.glide = bass_slide;
        bass.gate_left = gate_samples;
        if (accent) bass.fenv = 0.8f, bass.kc = 0;
        return;
    }
    bass.freq = bass.target;
    if (bass.mode == MTIMBRE_FM) bass.ph[1] = 0.0f;
    bass.glide = 0.0015f;
    bass.fenv = 1.0f;
    bass.kc = 0;   /* its filter: now */
    bass.acc = accent ? 1.0f : 0.72f;
    bass.vel = vel;
    bass.on = 1;
    bass.rising = 1;
    bass.gate_left = gate_samples;
}
static float bass_render(void)
{
    if (!bass.on) return 0.0f;
    bass.freq += (bass.target - bass.freq) * bass.glide;
    if (bass.rising) { bass.env += (1.0f - bass.env) * 0.02f; if (bass.env > 0.99f) bass.rising = 0; }
    float inc = bass.freq * (1.0f / SR), s, sub = 0.0f;
    int filtered = 1;
    switch (bass.mode) {
    case MTIMBRE_SUB:  s = sine(bass.ph[0]); bass.ph[0] += inc; if (bass.ph[0] >= 1.0f) bass.ph[0] -= 1.0f; break;
    case MTIMBRE_ACID: s = fm_tanhf((1.2f + 2.6f * bass.acc * bass.acc) * saw(&bass.ph[0], inc)); break;   /* the 303 into a drive */
    case MTIMBRE_UPRIGHT: {   /* FINGER (F6c, in the upright's place: Marote): a fundamental, its 2nd and 3rd dying
                               * fast (the finger on the string), the pitch settling from a hair sharp, warmed */
        float h = bass.fenv, p2 = bass.ph[0] * 2.0f, p3 = bass.ph[0] * 3.0f;
        p2 -= (float)(int)p2; p3 -= (float)(int)p3;
        s = sine(bass.ph[0]) + (0.12f + 0.45f * h) * sine(p2) + 0.25f * h * h * sine(p3);
        s = fm_tanhf(1.3f * s) * (0.7f + 0.3f * h) * 1.15f;
        bass.ph[0] += inc * (1.0f + 0.02f * h * h * h);
        if (bass.ph[0] >= 1.0f) bass.ph[0] -= 1.0f;
        break;
    }
    case MTIMBRE_FM: {   /* two operators 1:1, the index dying fast (an accent: more) */
        float idx = (0.25f + 0.9f * bass.fenv * bass.fenv) * (0.6f + 0.4f * bass.acc);
        float ph = bass.ph[0] + idx * sine(bass.ph[1]);
        ph -= (float)(int)ph; if (ph < 0.0f) ph += 1.0f;
        s = 1.2f * sine(ph);
        bass.ph[0] += inc; if (bass.ph[0] >= 1.0f) bass.ph[0] -= 1.0f;
        bass.ph[1] += inc; if (bass.ph[1] >= 1.0f) bass.ph[1] -= 1.0f;
        filtered = 0;
        break;
    }
    case MTIMBRE_DUB:   /* a sine, warmed: round and low */
        s = 0.8f * fm_tanhf(2.0f * sine(bass.ph[0]));
        bass.ph[0] += inc; if (bass.ph[0] >= 1.0f) bass.ph[0] -= 1.0f;
        filtered = 0;
        break;
    default:   /* the reese: two saws drifting apart through the filter; its sub under it, clean (F6b) */
        s = 0.5f * (saw(&bass.ph[0], inc * 1.004f * DRIFT(0)) + saw(&bass.ph[1], inc * 0.996f * DRIFT(1)));
        sub = 0.6f * sine(bass.ph[2]);
        bass.ph[2] += inc * 0.5f;
        if (bass.ph[2] >= 1.0f) bass.ph[2] -= 1.0f;
        break;
    }
    if (filtered) {
        if (--bass.kc <= 0) {   /* the filter's coefficients: every 16 samples (F6a: the CPU) */
            int acid = bass.mode == MTIMBRE_ACID;
            float cut = acid ? 220.0f + 3200.0f * bass.fenv * bass.acc * bass.acc :
                        bass.mode == MTIMBRE_UPRIGHT ? 500.0f + 1600.0f * bass.fenv : 140.0f + 700.0f * bass.fenv;
            svf_set(&bass.f, g_of(cut * bass_cut), (acid ? 1.0f : 1.2f) * (acid ? 1.0f : bass_k));
            if (acid) svf_set(&bass.f2, g_of(cut * bass_cut), 0.35f * bass_k);   /* the acid: 24 dB, two of them */
            bass.kc = 16;
        }
        s = svf_run(&bass.f, s);
        if (bass.mode == MTIMBRE_ACID) s = 1.3f * svf_run(&bass.f2, s);
    }
    s += sub;
    bass.fenv *= bass.mode == MTIMBRE_ACID ? 0.9993f : 0.9996f;
    if (bass.gate_left > 0) bass.gate_left -= 1.0f;
    else bass.env *= bass.mode == MTIMBRE_ACID ? 0.9975f : bass.mode == MTIMBRE_SUB || bass.mode == MTIMBRE_UPRIGHT ? 0.9985f : 0.9992f;
    if (bass.env < 1e-4f) { bass.on = 0; bass.env = 0; }
    return soft(1.6f * s) * bass.env * bass.acc * bass.vel;
}

/* ------------------------------------------------------- a 3-voice layer --- */
enum { T_STRINGS, T_ORGAN, T_STAB, T_EPIANO, T_CLAVI, T_HARPSI, T_GUITAR, T_BRASS, T_FM6 };
/* ------------------------------------------------------------ FM6 (F6d) --- */
/* Marote (19 nov): the FM-1's own sounds are a DX7's, six operators; ours were two (a Sega Genesis's). So: msfa, the
 * DX7 engine of Dexed, in Felucca's integer C port (fm6_core.h), playing M-VAVE's factory bank (fm6_factory.h), both
 * Apache-2.0. A voice renders its own 32-sample blocks (from the sample it is struck: no block's wait), each block
 * standing alone (its note, its LFO, its patch: a copy, so a sound changed mid-note never touches it) -- the shape
 * a job for the second core needs. The patch: the 128-byte packed record unpacked to the 155-byte layout (Felucca's
 * eng_fm6.c fm6_unpack, GPL-3.0-only). */
struct fm6v { fm6_note_t n; fm6_lfo_t lfo; uint8_t p[FP_SIZE + 1]; float buf[FM6_N]; int pos, done; };
static void fm6_unpack(const uint8_t *b, uint8_t *v)
{
    uint32_t k, i;
    for (k = 0; k < 6u; k++) {
        const uint8_t *o = b + k * 17u;
        uint8_t *d = v + k * FP_OP;
        for (i = 0; i < 11u; i++) d[i] = o[i] & 0x7Fu;
        d[FP_LC] = o[11] & 3u; d[FP_RC] = (o[11] >> 2) & 3u;
        d[FP_RS] = o[12] & 7u; d[FP_DET] = (o[12] >> 3) & 15u;
        d[FP_AMS] = o[13] & 3u; d[FP_KVS] = (o[13] >> 2) & 7u;
        d[FP_OL] = o[14] & 0x7Fu;
        d[FP_MODE] = o[15] & 1u; d[FP_FC] = (o[15] >> 1) & 31u;
        d[FP_FF] = o[16] & 0x7Fu;
    }
    for (i = 0; i < 9u; i++) v[FP_PR1 + i] = b[102 + i] & 0x7Fu;   /* pitch EG, algorithm */
    v[FP_ALG] &= 31u;
    v[FP_FB] = b[111] & 7u; v[FP_OKS] = (b[111] >> 3) & 1u;
    for (i = 0; i < 4u; i++) v[FP_LFS + i] = b[112 + i] & 0x7Fu;
    v[FP_LKS] = b[116] & 1u; v[FP_LFW] = (b[116] >> 1) & 7u; v[FP_LPMS] = (b[116] >> 4) & 7u;
    v[FP_TRNSP] = b[117] & 0x7Fu;
    for (i = 0; i < 10u; i++) v[FP_NAME + i] = b[118 + i] & 0x7Fu;
}
static void fm6_strike(struct fm6v *f, const uint8_t *patch, int midi, float vel)
{
    int v127 = (int)(vel * 100.0f);
    memcpy(f->p, patch, FP_SIZE);
    fm6_note_init(&f->n, f->p, midi + (int)f->p[FP_TRNSP] - 24, v127 < 1 ? 1 : v127 > 127 ? 127 : v127, 1);
    fm6_lfo_reset(&f->lfo, f->p);
    fm6_lfo_key(&f->lfo);
    f->pos = FM6_N;   /* its first block: now */
    f->done = 0;
}
static void fm6_block(struct fm6v *f, int midi)   /* the next 32 samples (floats, a carrier at full ~ 0.5) */
{
    int32_t bus[FM6_N], dt[6] = { 0, 0, 0, 0, 0, 0 };
    int32_t lv = fm6_lfo_sample(&f->lfo), ld = fm6_lfo_delay(&f->lfo);
    int32_t lf = fm6_note_logfreq((midi + (int)f->p[FP_TRNSP] - 24) * 16, 0);
    f->pos = 0;
    if (!fm6_note_compute(&f->n, f->p, bus, lv, ld, lf, f->p[FP_ALG], f->p[FP_FB], dt, 0)) {
        for (int i = 0; i < FM6_N; ++i) f->buf[i] = 0.0f;
        if (fm6_note_done(&f->n, f->p, f->p[FP_ALG])) f->done = 1;
        return;
    }
    for (int i = 0; i < FM6_N; ++i) {   /* (Felucca's DC blocker: FM puts sidebands at 0 Hz) */
        int32_t x = bus[i] - f->n.dc;
        f->n.dc += x >> 10;
        f->buf[i] = (float)x * (0.5f / 16777216.0f);
    }
}
static struct fm6v fm6_rhy[3];
static uint8_t rhy_patch[FP_SIZE + 1];
/* the harmony's sounds from the bank (after the seven of ours; EPIANO plays PIANO 1 since F6d): the pianos (1, 5, 7, 8:
 * the DX7's Rhodes), organs, guitars, brass, strings, pads */
static const uint8_t HBANK[] = {
    0, 4, 8, 12, 16, 20, 24, 28,  1, 5, 9, 13, 17, 21, 25, 29,  32, 36, 40, 44, 48, 52, 56, 60,
    33, 37, 41, 45, 49, 53, 57, 61,  64, 68, 72, 76, 80, 84, 88, 92,  66, 70, 74, 78, 82, 86, 90, 94,
    3, 7, 11, 15, 19, 23, 27, 31 };
enum { HS_OURS = MHSOUND_BANK };
static int hbank_of(int snd) { return snd == MHSOUND_EPIANO ? 0 : snd >= HS_OURS && snd < HS_OURS + (int)sizeof HBANK ? HBANK[snd - HS_OURS] : -1; }
static char hs_name[12];
const char *music_harm_sound_name(unsigned snd)
{
    static const char *const OURS[HS_OURS] = { "ORGAN", "SAW", "EPIANO", "CLAVI", "HARPSI", "GUITAR", "BRASS" };
    if (snd < HS_OURS) return OURS[snd];
    int b = hbank_of((int)snd);
    if (b < 0) return "?";
    int n = 0;
    for (int i = 0; i < 10; ++i) hs_name[n++] = (char)(FM6_BANK[b][118 + i] & 0x7F);
    while (n > 0 && hs_name[n - 1] == ' ') --n;
    hs_name[n] = 0;
    for (int i = 0; i < n; ++i) if (hs_name[i] >= 'a' && hs_name[i] <= 'z') hs_name[i] = (char)(hs_name[i] - 32);
    return hs_name;
}
#define T_PLUCKED(t) ((t) == T_CLAVI || (t) == T_HARPSI || (t) == T_GUITAR)
struct fm6v;   /* F6d: a voice's FM6 note (below) */
struct voice { float ph[4], freq, env, fenv, vel, wait; int stage, gate, kc, kind, midi; struct svf f; struct ks str; struct fm6v *fm; };
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
static void voice_strike(struct voice *h, int kind);
static const int TRIAD[3] = { 0, 1, 2 }, COLOUR[3] = { 1, 2, 3 };
static void layer_chord(struct layer *L, const struct chord *c, const int idx[3], int octave_up, float vel,
                        int gate_steps, int legato, float strum)   /* strum: samples between voices, up or down */
{
    int pcs[3] = { chord_pc(c, idx[0]), chord_pc(c, idx[1]), chord_pc(c, idx[2]) };
    static int down;
    voice_lead(L, pcs);
    down ^= 1;
    for (int v = 0; v < 3; ++v) {
        struct voice *h = &L->v[v];
        h->midi = L->voicing[v] + 12 * octave_up;
        h->freq = mtof((float)h->midi);
        if (!legato || h->stage == 0 || h->stage == 3) { h->stage = 1; if (!legato) h->env = 0; }
        h->fenv = 1.0f;
        h->kc = 0;
        h->vel = vel;
        h->gate = gate_steps;
        h->wait = 0.0f;
        if (!legato || !h->kind) voice_strike(h, L->timbre);
        if (strum > 0.0f && !legato) {   /* in pitch order, up or down; each voice a little apart in loudness too */
            int rank = 0;
            for (int o = 0; o < 3; ++o) if (L->voicing[o] < L->voicing[v]) ++rank;
            h->wait = strum * (float)(down ? 2 - rank : rank);
            h->vel = vel * (0.9f + 0.2f * hrand());
        }
    }
}
static void layer_release(struct layer *L)
{
    for (int v = 0; v < 3; ++v) if (L->v[v].stage && L->v[v].stage != 3) L->v[v].stage = 3;
}
static void voice_strike(struct voice *h, int kind)   /* F6b: a voice sounds as it was struck (a sound changed: no old line) */
{
    h->kind = kind;
    float hz = h->freq, v = h->vel;
    switch (kind) {   /* the pluck: brightness, loss, how long it rings, where */
    case T_CLAVI:  ks_pluck(&h->str, hz, 0.75f, 0.15f, 0.9f, v, 0.12f); break;    /* bright, near the pickup: nasal */
    case T_HARPSI: ks_pluck(&h->str, hz, 0.9f, 0.08f, 2.2f, v, 0.07f); break;     /* brighter still, the quill */
    case T_GUITAR: ks_pluck(&h->str, hz, 0.45f, 0.3f, 0.35f, v, 0.2f); break;     /* the palm on it: short */
    case T_EPIANO: case T_BRASS: h->ph[0] = h->ph[1] = h->ph[2] = 0.0f; break;
    case T_FM6: if (h->fm) fm6_strike(h->fm, rhy_patch, h->midi, v); else h->kind = T_STAB; break;
    default: break;
    }
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
        if (h->wait > 0.0f) { h->wait -= 1.0f; continue; }   /* a strummed voice not yet struck */
        if (h->stage == 1) { h->env += (1.05f - h->env) * L->ca; if (h->env >= 1.0f) { h->env = 1.0f; h->stage = 2; } }
        else if (h->stage == 2) h->env += (L->sus - h->env) * L->cd;
        else { h->env -= h->env * L->cr; if (h->env < 1e-4f) { h->stage = 0; continue; } }
        float inc = h->freq * (1.0f / SR) * DRIFT(2 + v), s;
        switch (h->kind) {
        case T_ORGAN:   /* drawbars 8' 4' 2 2/3' 2', a percussion click */
            s = 0.55f * sine(h->ph[0]) + 0.4f * sine(h->ph[1]) + 0.3f * sine(h->ph[2]) + 0.25f * sine(h->ph[3]);
            h->ph[0] += inc; h->ph[1] += inc * 2.0f; h->ph[2] += inc * 3.0f; h->ph[3] += inc * 4.0f;
            for (int k = 0; k < 4; ++k) if (h->ph[k] >= 1.0f) h->ph[k] -= 1.0f;
            s += 0.35f * h->fenv * h->vel * sine(h->ph[3] * 2.0f);   /* the click: harder on an accent */
            h->fenv *= 0.9985f;
            break;
        case T_EPIANO: {   /* a Rhodes: two operators 1:1, the bark dying away; the tine (14:1) a moment */
            float idx = (0.12f + 0.5f * h->fenv * h->vel) * h_bright;
            float ph = h->ph[0] + idx * sine(h->ph[1]);
            ph -= (float)(int)ph; if (ph < 0.0f) ph += 1.0f;
            float tine = h->fenv * h->fenv; tine *= tine; tine *= tine;
            s = sine(ph) + 0.18f * tine * h->vel * sine(h->ph[2]);
            h->ph[0] += inc; if (h->ph[0] >= 1.0f) h->ph[0] -= 1.0f;
            h->ph[1] += inc; if (h->ph[1] >= 1.0f) h->ph[1] -= 1.0f;
            h->ph[2] += inc * 14.0f; if (h->ph[2] >= 1.0f) h->ph[2] -= (float)(int)h->ph[2];
            h->fenv *= 0.99988f;
            break;
        }
        case T_CLAVI: case T_HARPSI: case T_GUITAR:
            if (h->stage == 3 && h->str.n && h->fenv > 0.0f) { ks_mute(&h->str, h->kind == T_HARPSI ? 0.12f : 0.04f); h->fenv = 0.0f; }
            s = ks_run(&h->str);
            if (h->kind == T_CLAVI) {   /* its pickup: the bright string, a bite */
                if (--h->kc <= 0) { svf_set(&h->f, g_of(3500.0f * h_bright * fbright), 1.4f); h->kc = 16; }
                s = 1.4f * s - 0.6f * svf_run(&h->f, s);
            }
            break;
        case T_FM6: {   /* F6d: its own blocks; the gate's end lifts the keys; done when its carriers are */
            struct fm6v *f = h->fm;
            if (h->stage == 3 && f->n.down) fm6_note_key(&f->n, f->p, 0);
            if (f->pos >= FM6_N) fm6_block(f, h->midi);
            s = f->buf[f->pos++];
            if (f->done) h->stage = 0;
            break;
        }
        case T_BRASS: {   /* saws through a filter that swells open and falls back (a section's stab) */
            s = 0.5f * (saw(&h->ph[0], inc * 1.0035f) + saw(&h->ph[1], inc * 0.9965f));
            if (--h->kc <= 0) {
                float sw = 0.3f + (1.0f - h->fenv) * h->fenv * 4.0f;   /* the swell, over what stays open */
                svf_set(&h->f, g_of((L->cut + L->famt * sw * h->vel) * h_bright * fbright), L->k);
                h->kc = 16;
            }
            s = svf_run(&h->f, s);
            h->fenv *= 0.9996f;
            break;
        }
        default:
            s = 0.5f * (saw(&h->ph[0], inc * 1.0035f) + saw(&h->ph[1], inc * 0.9965f));
            if (--h->kc <= 0) {
                svf_set(&h->f, h->kind == T_STRINGS ? g_moving : g_of((L->cut + L->famt * h->fenv * h->vel) * h_bright * fbright), L->k);
                h->kc = 16;
            }
            s = svf_run(&h->f, s);
            h->fenv *= 0.9992f;
            break;
        }
        s *= h->env * L->gain * (h->kind == T_FM6 ? 1.0f : h->vel);   /* (FM6: the velocity is in its operators) */
        float pan = L->pan0 + 0.2f * (float)v;
        *l += s * (1.0f - pan);
        *r += s * pan;
    }
}

/* ------------------------------------------- the rhythm layer's hits --- */
enum { R_REST, R_SKANK, R_BUBBLE, R_STABS };
static struct { float wait; int octave; float vel; int gate; int live; const struct chord *c; } pend[8];
static int pend_n;   /* how many wait (F6a: no looking each sample when none) */
static void schedule(float delay, int octave, float vel, int gate, const struct chord *c)   /* c: a chord ahead, or 0 */
{
    vel *= note_vel(TRK_HARMONY);
    for (int i = 0; i < 8; ++i) if (!pend[i].live) {
        pend[i].wait = delay; pend[i].octave = octave; pend[i].vel = vel; pend[i].gate = gate;
        pend[i].live = 1; pend[i].c = c;
        ++pend_n;
        return;
    }
}
static void pend_tick(void)
{
    if (!pend_n) return;
    for (int i = 0; i < 8; ++i) if (pend[i].live && (pend[i].wait -= 1.0f) <= 0.0f) {
        pend[i].live = 0;
        --pend_n;
        float strum = GROOVE[track_style(TRK_HARMONY)].strum_ms * human_of(TRK_HARMONY) * (0.6f + 0.4f * hrand()) * (SR * 0.001f);
        layer_chord(&rhy, pend[i].c ? pend[i].c : cur_chord, TRIAD, pend[i].octave, pend[i].vel, pend[i].gate, 0, strum);
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
    {   /* the sound the figure is played on (F6b); dub in AUTO: the guitar chops the skank, the organ bubbles */
        int snd = harm_sound_now;
        if (c_hsound == MAUTO && snd == MHSOUND_GUITAR && md == MD_C) snd = MHSOUND_ORGAN;
        int bk = hbank_of(snd);
        if (bk >= 0) {   /* F6d: a voice of the bank; its own envelopes shape it (the layer's: open, a long release) */
            fm6_unpack(FM6_BANK[bk], rhy_patch);
            layer_shape(&rhy, T_FM6, 0.3f, 50, 1.0f, 6000, 0, 0, 1.0f, 0.5f);
            snd = -1;
        }
        switch (snd) {
        case MHSOUND_SAW:
            if (md != MD_STABS) { rhy.timbre = T_STAB; rhy.cut = 1100.0f; rhy.famt = 2600.0f; rhy.k = 0.9f; rhy.gain *= 0.8f; }
            break;
        case MHSOUND_EPIANO: layer_shape(&rhy, T_EPIANO, 1, 1100, 0.0f, 140, 0, 0, 1.0f, 0.54f); break;
        case MHSOUND_CLAVI:  layer_shape(&rhy, T_CLAVI, 1, 3000, 1.0f, 30, 0, 0, 1.0f, 0.48f); break;
        case MHSOUND_HARPSI: layer_shape(&rhy, T_HARPSI, 1, 3000, 1.0f, 120, 0, 0, 1.0f, 0.8f); break;
        case MHSOUND_GUITAR: layer_shape(&rhy, T_GUITAR, 1, 3000, 1.0f, 30, 0, 0, 1.0f, 0.92f); break;
        case MHSOUND_BRASS:  layer_shape(&rhy, T_BRASS, 12, 300, 0.6f, 90, 600, 3000, 0.8f, 0.75f); break;
        default: break;
        }
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
        fm6_unpack(FM6_BANK[1], rhy_patch);   /* F6d: the dearest: ORGAN 1, six carriers, held */
        layer_shape(&rhy, T_FM6, 0.3f, 50, 1.0f, 6000, 0, 0, 1.0f, 0.5f);
        for (int v = 0; v < 3; ++v) rhy.v[v].kind = 0;
        layer_chord(&rhy, cur_chord, TRIAD, 1, 0.6f, 0, 1, 0.0f);
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
    if (pad_on) layer_chord(&pad, cur_chord, COLOUR, 0, 1.0f, 0, 1, 0.0f);
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
        const struct chord *nx = chord_ahead(ahead);
        if (track_style(TRK_BASS) != MSTYLE_JAZZ)   /* F6c: chromatic only in the walk; elsewhere a step of its scale */
            return scale_note(nx, e->deg == 5 ? -1 : 1);
        return root_midi(nx) + (e->deg == 5 ? -1 : 1);
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
    float vel = note_vel(TRK_BASS);
    bass_play(bev_note(e, v), (float)len * vsps(TRK_BASS) * (len == 1 ? 0.55f : 0.92f) * gate_mul(TRK_BASS, vel), slide, e->acc, vel);
}

/* ------------------------------------------------------------- the melody --- */
/* Seeds: short motifs from public-domain works (the composer dead more than 70 years and the work published before
 * 1929), in steps of the key's scale (0 the tonic, 7 its octave in a 7-note scale), a semitone's alteration, length
 * in 16ths; a rest is step R. Prototype set (F4, 18 nov): to be checked against public-domain scores before it is
 * final. Each in the mode it was written (minor or major); the key's scale maps it. */
#define MR 99   /* a rest */
struct mnote { int8_t deg, alt, dur; };
struct seed { const char *name; uint8_t n; struct mnote m[20]; };
static const struct seed SEEDS[] = {
    { "DIES IRAE", 8, { {2,0,4}, {1,0,4}, {2,0,4}, {0,0,4}, {1,0,4}, {-1,0,4}, {0,0,4}, {0,0,4} } },   /* Gregorian chant */
    { "TOCCATA", 11, { {4,0,1}, {3,0,1}, {4,0,6}, {MR,0,4}, {3,0,1}, {2,0,1}, {1,0,1}, {0,0,1}, {-1,1,4}, {0,0,8}, {MR,0,4} } },   /* Bach, BWV 565 */
    { "FIFTH", 10, { {MR,0,2}, {4,0,2}, {4,0,2}, {4,0,2}, {2,0,8}, {MR,0,2}, {3,0,2}, {3,0,2}, {3,0,2}, {1,0,8} } },   /* Beethoven, Symphony 5 */
    { "MOUNTAIN KING", 13, { {0,0,2}, {1,0,2}, {2,0,2}, {3,0,2}, {4,0,2}, {2,0,2}, {4,0,4},
                             {3,1,2}, {1,0,2}, {3,1,4}, {3,0,2}, {1,-1,2}, {3,0,4} } },   /* Grieg, Peer Gynt */
    { "FUR ELISE", 18, { {4,0,1}, {3,1,1}, {4,0,1}, {3,1,1}, {4,0,1}, {1,0,1}, {3,0,1}, {2,0,1}, {0,0,3}, {MR,0,1},
                         {-5,0,1}, {-3,0,1}, {0,0,1}, {1,0,3}, {MR,0,1}, {-3,0,1}, {-1,1,1}, {1,0,7} } },   /* Beethoven, WoO 59 */
    { "MINUET", 11, { {4,0,4}, {0,0,2}, {1,0,2}, {2,0,2}, {3,0,2}, {4,0,4}, {0,0,4}, {0,0,4}, {5,0,4}, {3,0,2}, {4,0,2} } },   /* Petzold, BWV Anh. 114 */
    { "GREENSLEEVES", 13, { {0,0,2}, {2,0,4}, {3,0,2}, {4,0,3}, {5,0,1}, {4,0,2}, {3,0,4}, {1,0,2}, {-1,0,3}, {0,0,1},
                            {1,0,2}, {2,0,4}, {0,0,2} } },   /* traditional */
    { "GYMNOPEDIE", 10, { {2,0,3}, {4,0,3}, {3,0,3}, {2,0,3}, {-1,0,3}, {-2,0,3}, {-1,0,3}, {0,0,3}, {-3,0,3}, {-5,0,5} } },   /* Satie, 1 */
    { "BADINERIE", 10, { {0,0,2}, {2,0,1}, {0,0,1}, {-3,0,2}, {0,0,1}, {-3,0,1}, {-5,0,2}, {-3,0,1}, {-5,0,1}, {-7,0,4} } },   /* Bach, BWV 1067 */
    { "ENTERTAINER", 17, { {1,0,1}, {1,1,1}, {2,0,1}, {7,0,3}, {2,0,1}, {7,0,3}, {2,0,1}, {7,0,7}, {7,0,1}, {8,0,1},
                           {8,1,1}, {9,0,1}, {7,0,1}, {8,0,1}, {9,0,2}, {6,0,1}, {8,0,2} } },   /* Joplin */
};
enum { NSEEDS = sizeof SEEDS / sizeof SEEDS[0] };
unsigned music_seeds(void) { return NSEEDS; }
const char *music_seed_name(unsigned i) { return i < NSEEDS ? SEEDS[i].name : "AUTO"; }
void music_mel_seed(unsigned i) { c_mseed = (uint8_t)(i < NSEEDS ? i : MAUTO); }
/* each style: its seeds (AUTO), its modes' weights (SEED RIFF LINE ANSWER), its sound */
static const struct { uint8_t n, seeds[5], w[MMEL_MODES], sound; } MSTY[MSTYLES] = {
    { 4, { 0, 1, 2, 3 }, { 60, 0, 40, 0 }, MMSOUND_STRING },          /* breakcore */
    { 3, { 9, 8, 5 }, { 20, 0, 50, 30 }, MMSOUND_VIBES },             /* jazz */
    { 3, { 3, 1, 8 }, { 20, 80, 0, 0 }, MMSOUND_SQUARE },             /* acid */
    { 4, { 4, 2, 5, 9 }, { 20, 50, 0, 30 }, MMSOUND_SAW },            /* hip hop */
    { 3, { 7, 6, 4 }, { 30, 0, 0, 70 }, MMSOUND_REED },               /* dub */
};

/* the lead: one voice, five sounds */
static struct {
    int on, stage, sound, kc; float ph[2], freq, target, env, fenv, vel, gate_left, vib_ph, age, ca, cd, cr; struct svf f;
    struct ks str; float scoop, swell;   /* F6b: the plucked line, a reed's or a brass's pitch coming up into the note, the swell */
} lead;
static void lead_note(int midi, float gate, float vel)
{
    lead.target = mtof((float)midi);
    if (!lead.on || m_glide <= 0.0f || lead.stage == 3) lead.freq = lead.target;
    if (!lead.on || lead.stage == 3) {
        lead.ph[0] = 0.0f;
        lead.ph[1] = lead.sound == MMSOUND_SQUARE ? 0.5f : lead.sound == MMSOUND_REED ? 0.25f : 0.0f;
        lead.age = 0.0f;
        lead.stage = 1;
        if (lead.sound != MMSOUND_STRING) lead.env = 0.0f;
        lead.scoop = lead.sound == MMSOUND_BRASS ? 0.017f : lead.sound == MMSOUND_REED ? 0.011f : 0.0f;   /* -30 / -20 cents */
        lead.swell = 0.0f;
    }
    if (lead.sound == MMSOUND_PLUCK) ks_pluck(&lead.str, lead.target, 0.5f + 0.3f * vel, 0.3f, 1.2f, vel, 0.15f);
    if (lead.sound == MMSOUND_VIBES) lead.ph[0] = lead.ph[1] = 0.0f, lead.stage = 1, lead.env = 0.0f;
    lead.fenv = 1.0f;
    lead.kc = 0;
    lead.vel = vel;
    lead.on = 1;
    lead.gate_left = gate;
}
static float lead_render(void)
{
    /*                                 SAW    SQUARE  FM     REED   STRING  FLUTE  VIBES  PLUCK  BRASS */
    static const float ATK[MMSOUNDS] = { 3.0f, 2.0f, 1.0f, 20.0f, 150.0f, 45.0f, 1.0f, 1.0f, 25.0f };
    static const float REL[MMSOUNDS] = { 80.0f, 40.0f, 400.0f, 90.0f, 250.0f, 120.0f, 900.0f, 60.0f, 90.0f };
    static const float SUS[MMSOUNDS] = { 0.8f, 0.75f, 0.0f, 0.85f, 0.9f, 0.8f, 0.0f, 1.0f, 0.8f };
    if (!lead.on) return 0.0f;
    int snd = lead.sound;
    float s;
    int kr = --lead.kc <= 0;   /* the coefficients (filter, envelope): every 16 samples (F6a) */
    if (kr) {
        lead.kc = 16;
        lead.ca = ms_coef(ATK[snd]);
        lead.cd = ms_coef(snd == MMSOUND_FM ? 600.0f : snd == MMSOUND_VIBES ? 1400.0f : 200.0f);
        lead.cr = ms_coef(REL[snd]);
        float hz = snd == MMSOUND_SAW ? (700.0f + 3500.0f * lead.fenv * lead.vel) * m_bright :
                   snd == MMSOUND_SQUARE ? (500.0f + 4000.0f * lead.fenv * lead.vel) * m_bright :
                   snd == MMSOUND_REED ? 1600.0f * m_bright :
                   snd == MMSOUND_FLUTE ? 2600.0f * m_bright :                                  /* (its breath) */
                   snd == MMSOUND_BRASS ? (450.0f + 3600.0f * lead.swell * lead.vel) * m_bright : 2200.0f * m_bright;
        float k = snd == MMSOUND_SAW ? 0.8f : snd == MMSOUND_SQUARE ? 0.45f : snd == MMSOUND_REED ? 0.5f :
                  snd == MMSOUND_FLUTE ? 1.6f : 0.9f;
        svf_set(&lead.f, g_of(hz), k);
    }
    if (m_glide > 0.0f) lead.freq += (lead.target - lead.freq) * m_glide; else lead.freq = lead.target;
    lead.age += 1.0f / SR;
    {   /* vibrato: the knob's; the string's grows in after 0.3 s */
        float depth = m_vib + (snd == MMSOUND_STRING && lead.age > 0.3f ? 0.25f : 0.0f);
        lead.vib_ph += 5.5f / SR;
        if (lead.vib_ph >= 1.0f) lead.vib_ph -= 1.0f;
        float inc = lead.freq * (1.0f + depth * 0.0578f * sine(lead.vib_ph)) * (1.0f / SR) * DRIFT(5) * (1.0f - lead.scoop);
        lead.scoop *= 0.9985f;                                    /* the scoop: up into the note in ~15 ms */
        lead.swell += (1.0f - lead.swell) * 0.0012f;               /* the brass's swell: ~20 ms */
        switch (snd) {
        case MMSOUND_SAW:
            s = 0.5f * (saw(&lead.ph[0], inc * 1.003f) + saw(&lead.ph[1], inc * 0.997f));
            s = svf_run(&lead.f, s);
            break;
        case MMSOUND_SQUARE: case MMSOUND_REED:   /* a pulse: two saws apart (square: half, reed: a quarter) */
            s = saw(&lead.ph[0], inc) - saw(&lead.ph[1], inc);
            s = snd == MMSOUND_SQUARE ? svf_run(&lead.f, s) : svf_run(&lead.f, s + 0.15f * lead.fenv * lead.fenv * nrand()) * 0.8f;
            break;
        case MMSOUND_FM: {   /* two operators: a bell (3.5:1), the index dying away */
            lead.ph[1] += inc * 3.5f; if (lead.ph[1] >= 1.0f) lead.ph[1] -= 1.0f;
            float ph = lead.ph[0] + 0.35f * m_bright * 2.5f * lead.fenv * (0.5f + 0.5f * lead.vel) * sine(lead.ph[1]);
            ph -= (float)(int)ph; if (ph < 0.0f) ph += 1.0f;
            s = sine(ph);
            lead.ph[0] += inc; if (lead.ph[0] >= 1.0f) lead.ph[0] -= 1.0f;
            break;
        }
        case MMSOUND_FLUTE: {   /* a sine and a little of its octave; breath through the filter, more at the start */
            s = sine(lead.ph[0]) + 0.12f * sine(lead.ph[0] * 2.0f >= 1.0f ? lead.ph[0] * 2.0f - 1.0f : lead.ph[0] * 2.0f);
            lead.ph[0] += inc; if (lead.ph[0] >= 1.0f) lead.ph[0] -= 1.0f;
            s += (0.06f + 0.25f * lead.fenv) * svf_run(&lead.f, nrand());
            break;
        }
        case MMSOUND_VIBES: {   /* the bar: 1:4 FM, the index a moment; the motor's tremolo */
            lead.ph[1] += inc * 4.0f; if (lead.ph[1] >= 1.0f) lead.ph[1] -= 1.0f;
            float ph = lead.ph[0] + 0.25f * lead.fenv * lead.fenv * lead.vel * sine(lead.ph[1]);
            ph -= (float)(int)ph; if (ph < 0.0f) ph += 1.0f;
            s = sine(ph) * (1.0f - 0.25f * (0.5f + 0.5f * sine(lead.vib_ph)));
            lead.ph[0] += inc; if (lead.ph[0] >= 1.0f) lead.ph[0] -= 1.0f;
            break;
        }
        case MMSOUND_PLUCK:
            if (lead.stage == 3 && lead.str.n && lead.swell > 0.5f) { ks_mute(&lead.str, 0.08f); lead.swell = 0.0f; }
            s = 2.5f * ks_run(&lead.str);
            break;
        case MMSOUND_BRASS:
            s = 0.5f * (saw(&lead.ph[0], inc * 1.003f) + saw(&lead.ph[1], inc * 0.997f));
            s = 2.0f * svf_run(&lead.f, s);
            break;
        default:   /* the string: a saw, slow, through a filter */
            s = saw(&lead.ph[0], inc);
            s = svf_run(&lead.f, s);
            break;
        }
    }
    lead.fenv *= snd == MMSOUND_FM || snd == MMSOUND_VIBES ? 0.99985f : 0.9994f;
    if (lead.stage == 1) { lead.env += (1.05f - lead.env) * lead.ca; if (lead.env >= 1.0f) { lead.env = 1.0f; lead.stage = 2; } }
    else if (lead.stage == 2) lead.env += (SUS[snd] - lead.env) * lead.cd;
    else { lead.env -= lead.env * lead.cr; if (lead.env < 1e-4f) { lead.on = 0; lead.env = 0.0f; } }
    if (lead.stage != 3 && lead.gate_left > 0.0f && (lead.gate_left -= 1.0f) <= 0.0f) lead.stage = 3;
    return soft(1.3f * s) * lead.env * lead.vel;
}

/* the line: a phrase of the track's own 32 16ths, notes as steps of the key's scale (the chord's tones on the
 * strong beats), made when the phrase starts */
struct mev { int8_t on, deg, alt, len; float vel; };
static struct mev mseq[32];
static uint8_t mel_mode_now = MMEL_SEED, mel_sound_now = MMSOUND_STRING, mel_seed_now, m_left, m_phrase;
static int8_t riff_deg[16], riff_len[16];
static struct jungle_step pat[JUNGLE_STEPS];
static int mel_tonic(void) { int t = 60 + key_root; return t < 62 ? t + 12 : t; }
static uint16_t key_scale(void)   /* the scale of the progression's home chord (its root on the key), over the key */
{
    const struct prog *p = P();
    for (int i = 0; i < p->n; ++i) if (p->c[i].root == 0) return SCALE[p->c[i].sc];
    return SCALE[p->c[0].sc];
}
static int deg_pitch(int deg, int alt)
{
    uint16_t m = key_scale();
    int n = 0, steps[12], oct = 0;
    for (int i = 0; i < 12; ++i) if (m >> i & 1u) steps[n++] = i;
    while (deg < 0) { deg += n; --oct; }
    while (deg >= n) { deg -= n; ++oct; }
    return mel_tonic() + steps[deg] + 12 * oct + alt;
}
static int snap_to_chord(int midi)   /* a strong beat's "avoid note" (a half step over a chord tone) down onto it */
{
    for (int i = 0; i < 4; ++i)
        if ((((midi - chord_pc(cur_chord, i)) % 12) + 12) % 12 == 1) return midi - 1;
    return midi;
}
static void put(int at, int deg, int alt, int len, float vel)
{
    if (at < 0 || at >= 32 || deg == MR) return;
    mseq[at].on = 1; mseq[at].deg = (int8_t)deg; mseq[at].alt = (int8_t)alt; mseq[at].len = (int8_t)len; mseq[at].vel = vel;
}
/* a seed laid from `at`, transformed: shifted (a sequence), inverted, its length times `aug` (2: half its notes),
 * the first `upto` notes only, the last one landing on `end` (MR: as written) */
static int lay_seed(const struct seed *sd, int at, int shift, int inv, int aug, int upto, int end)
{
    int d0 = sd->m[0].deg == MR ? 0 : sd->m[0].deg, n = upto < sd->n ? upto : sd->n;
    {   /* F6c: what fits the phrase, cut where the motif breathes (a note of 2 or more), not where it falls */
        int room = 32 - at, sum = 0, fit = 0;
        for (int i = 0; i < n; ++i) { sum += sd->m[i].dur * aug; if (sum > room) break; fit = i + 1; }
        if (fit < n) {
            int k = fit;
            while (k > 2 && fit - k < 4 && (sd->m[k - 1].dur < 2 || sd->m[k - 1].deg == MR)) --k;
            n = k > 2 && sd->m[k - 1].dur >= 2 ? k : fit;
        }
    }
    for (int i = 0; i < n && at < 32; ++i) {
        const struct mnote *m = &sd->m[i];
        int len = m->dur * aug, deg = m->deg;
        if (deg != MR) {
            if (inv) deg = d0 - (deg - d0);
            deg += shift;
            if (i == n - 1 && end != MR) deg = end;
        }
        put(at, deg, deg == m->deg + shift ? m->alt : 0, len, i == 0 ? 0.95f : 0.8f);
        at += len;
    }
    return at;
}
static void make_riff(const struct seed *sd)   /* a bar's hook from a seed's start: its notes, quick */
{
    float e = energy();
    int at = 0, i = 0;
    memset(riff_len, 0, sizeof riff_len);
    for (int k = 0; k < 16; ++k) riff_deg[k] = MR;
    while (at < 16 && i < sd->n) {
        const struct mnote *m = &sd->m[i++];
        int len = m->dur > 2 ? 2 : m->dur;
        if (m->deg == MR) { at += len; continue; }
        riff_deg[at] = m->deg; riff_len[at] = (int8_t)len;
        at += len + (frnd() > 0.4f + 0.5f * e ? 1 : 0);
    }
}
static void mel_plan(void)   /* the melody track's own phrase starts */
{
    const int st = track_style(TRK_MELODY), sect_k = form_on ? FORM[base_style][song_var].s[sec_i][0] : S_CHORUS;
    float e = energy();
    memset(mseq, 0, sizeof mseq);
    /* the mode (AUTO: the style's, a few phrases), the sound, the seed */
    if (c_mmode != MAUTO) mel_mode_now = c_mmode;
    else if (form_on && (sect_k == S_CHORUS || sect_k == S_DROP || sect_k == S_BREAKDOWN) && MSTY[st].w[MMEL_SEED]) mel_mode_now = MMEL_SEED;
    else if (m_left == 0 || --m_left == 0) {
        unsigned tot = 0, x;
        for (int m = 0; m < MMEL_MODES; ++m) tot += MSTY[st].w[m];
        x = rnd(tot ? tot : 1);
        for (int m = 0; m < MMEL_MODES; ++m) { if (x < MSTY[st].w[m]) { mel_mode_now = (uint8_t)m; break; } x -= MSTY[st].w[m]; }
        m_left = (uint8_t)(2 + rnd(3));
    }
    mel_sound_now = c_msound == MAUTO ? MSTY[st].sound : c_msound;
    lead.sound = mel_sound_now;
    if (c_mseed != MAUTO) mel_seed_now = c_mseed;
    else if (m_phrase % 4u == 0) mel_seed_now = MSTY[st].seeds[rnd(MSTY[st].n)];
    const struct seed *sd = &SEEDS[mel_seed_now];
    int answer = m_phrase & 1u;
    ++m_phrase;
    if (form_on && sect_k == S_INTRO) { put(0, sd->m[0].deg == MR ? 0 : sd->m[0].deg, 0, 28, 0.6f); return; }
    switch (mel_mode_now) {
    case MMEL_SEED:
        if (form_on && sect_k == S_BREAKDOWN) lay_seed(sd, 0, 0, 0, 2, answer ? sd->n : (sd->n + 1) / 2, answer ? 0 : MR);   /* slow */
        else if (form_on && sect_k == S_BRIDGE) lay_seed(sd, 0, answer ? -1 : 1, 1, 1, sd->n, MR);                          /* developed */
        else if (form_on && sect_k == S_PRE) lay_seed(sd, 0, (int)(m_phrase % 4u), 0, 1, sd->n, MR);                      /* rising */
        else if (form_on && sect_k == S_VERSE) { lay_seed(sd, 4, 0, 0, 1, 3, MR); if (answer) lay_seed(sd, 20, -1, 0, 1, 3, 0); }
        else if (!answer) lay_seed(sd, 0, 0, 0, 1, sd->n, 4);   /* the statement, open on the fifth */
        else {   /* the answer: the seed again, a step down or turned over, home on the tonic */
            int how = (int)rnd(3);
            lay_seed(sd, 0, how == 0 ? -1 : 0, how == 1, 1, how == 2 ? (sd->n + 1) / 2 : sd->n, 0);
            if (how == 2) put(16, 0, 0, 8, 0.85f);
        }
        break;
    case MMEL_RIFF:
        if (m_phrase % 4u == 1 || !riff_len[0]) make_riff(sd);
        else if (rnd(3) == 0) { int k = (int)rnd(16); if (riff_deg[k] != MR) riff_deg[k] = (int8_t)(riff_deg[k] + (rnd(2) ? 1 : -1)); }
        for (int bar = 0; bar < 2; ++bar) for (int k = 0; k < 16; ++k) if (riff_deg[k] != MR) put(bar * 16 + k, riff_deg[k], 0, riff_len[k], k == 0 ? 0.95f : 0.8f);
        break;
    case MMEL_LINE: {   /* steps mostly, a leap answered the other way, long notes on the way home */
        int at = (int)rnd(2) * 2, deg = (int)rnd(3) * 2, dir = rnd(2) ? 1 : -1;
        while (at < 30) {
            static const int8_t LEN[4] = { 2, 2, 4, 1 };
            int len = LEN[rnd(4)];
            if (frnd() > 0.3f + 0.6f * e && len < 4) len *= 2;
            put(at, deg, 0, len, 0.8f);
            at += len;
            if (rnd(5) == 0) { deg += dir * (3 + (int)rnd(2)); dir = -dir; } else deg += dir;
            if (deg > 9) { deg = 8; dir = -1; } if (deg < -4) { deg = -3; dir = 1; }
            if (rnd(6) == 0) dir = -dir;
        }
        put(28, 0, 0, 4, 0.85f);
        break;
    }
    default: {   /* ANSWER: fragments in each bar's second half, after the break has spoken */
        for (int bar = 0; bar < 2; ++bar) if (bar == 1 || frnd() < 0.5f + 0.5f * e)
            lay_seed(sd, bar * 16 + 8 + (int)rnd(2) * 2, bar ? -1 : 0, 0, 1, 2 + (int)rnd(2), bar ? 0 : MR);
        break;
    }
    }
}
/* F5b, the melody's expression (Marote, 18 nov): not noise but the phrase, as a player shapes it. From KTH's
 * performance rules (Friberg: melodic and harmonic charge, phrase arch, duration contrast, high loud, faster uphill,
 * leap articulation, punctuation) and the jazz studies (a soloist's downbeats late, its offbeats with the band, its
 * swing less than the band's and evener as the notes get quick: Friberg & Sundstrom, Nelias et al. 2022). Random
 * timing is left out (Datseris et al. 2019: it adds no swing; too much takes it away); a hand's drift in loudness,
 * slow, stays. Acid and breakcore: on the grid, the phrase's dynamics only (Marote). A note on the break's snare is
 * not moved: it is played softer (the jungle producers' ducking). */
static const struct { float solo_sw, down_ms, end_ms; } MEXP[MSTYLES] = {
    { 0.0f, 0.0f, 0.0f },     /* breakcore: the grid */
    { 0.6f, 15.0f, 15.0f },   /* jazz: downbeats 15 ms late (the cautious value, Marote) */
    { 1.0f, 0.0f, 0.0f },     /* acid: with the machine */
    { 0.5f, 8.0f, 10.0f },    /* hip hop */
    { 0.7f, 10.0f, 15.0f },   /* dub */
};
static float mx_vel[32], mx_gate[32], m_drift;
static int8_t mx_end[32];
static int m_tension;   /* the last note was a tension: a step to a chord tone resolves it */
static float mel_expr(void)   /* how much expression: the knob (SEL + K2), HUMAN against the style's own */
{
    float base = (float)GROOVE[track_style(TRK_MELODY)].human * 0.01f;
    float e = c_acc[TRK_MELODY] * (base > 0.0f ? human_of(TRK_MELODY) / base : 1.0f);
    return e > 2.0f ? 2.0f : e;
}
static void mel_shape(void)   /* the phrase planned: each note's loudness and length, from the phrase */
{
    int vs[32], pc[32], n = 0, top = 0, lo = 999, hi = -999;
    float E = mel_expr();
    for (int v = 0; v < 32; ++v) { mx_vel[v] = mx_gate[v] = 1.0f; mx_end[v] = 0; }
    for (int v = 0; v < 32; ++v) if (mseq[v].on) { vs[n] = v; pc[n] = deg_pitch(mseq[v].deg, mseq[v].alt); ++n; }
    if (!n) return;
    for (int i = 0; i < n; ++i) { if (pc[i] > pc[top]) top = i; if (pc[i] < lo) lo = pc[i]; if (pc[i] > hi) hi = pc[i]; }
    float mid = 0.5f * (float)(lo + hi);
    for (int i = 0; i < n; ++i) {
        int v = vs[i];
        float vel = 1.0f, gate = 1.0f;
        /* the arch: up to the phrase's highest note, then down to its end */
        if (i <= top) vel *= 0.9f + 0.15f * (top ? (float)i / (float)top : 1.0f);
        else vel *= 1.05f - 0.2f * (float)(i - top) / (float)(n - top);
        vel *= 1.0f + 0.008f * ((float)pc[i] - mid);                                     /* high loud */
        if (mseq[v].len <= 1) { vel *= 0.92f; gate *= 0.9f; } else if (mseq[v].len >= 4) vel *= 1.03f;   /* duration contrast */
        if (i > 0 && i + 1 < n && pc[i - 1] < pc[i] && pc[i] < pc[i + 1]) gate *= 0.92f;  /* faster uphill */
        if (i + 1 < n && iabs(pc[i + 1] - pc[i]) >= 5) gate *= 0.85f;                    /* a leap: a breath before */
        if (i + 1 < n && vs[i + 1] > v + mseq[v].len) gate *= 0.9f;                       /* a motif's end */
        m_drift = 0.8f * m_drift + 0.2f * (2.0f * hrand() - 1.0f);                       /* a hand: slow */
        vel *= 1.0f + 0.04f * m_drift;
        mx_vel[v] = 1.0f + E * (vel - 1.0f);
        mx_gate[v] = 1.0f + E * (gate - 1.0f);
    }
    mx_end[vs[n - 1]] = 1;
}
static float mel_late(int v)   /* how late a note sounds after its 16th: the soloist's swing, the downbeat, the close */
{
    const int st = track_style(TRK_MELODY);
    float d = 0.0f, ms16 = vsps(TRK_MELODY) * (1000.0f / SR), E = mel_expr();
    if (v & 1) {
        float sw = swing_of(TRK_MELODY);
        if (c_tswing[TRK_MELODY] == MAUTO) sw *= MEXP[st].solo_sw;            /* a soloist swings less than the band */
        float quick = (ms16 - 55.0f) * (1.0f / 45.0f);                        /* and evener as the notes get quick */
        sw *= quick < 0.0f ? 0.0f : quick > 1.0f ? 1.0f : quick;
        if (mseq[v].len <= 1 && v > 0 && mseq[v - 1].on && mseq[v - 1].len <= 1) sw *= 0.5f;   /* a run: evener still */
        d += sw * vsps(TRK_MELODY);
    }
    if (v % 4 == 0) d += MEXP[st].down_ms * E * (SR * 0.001f);               /* the downbeat, laid back */
    if (mx_end[v]) d += MEXP[st].end_ms * E * (SR * 0.001f);                 /* the close, a breath late */
    return d;
}
static struct { struct mev e; int v, duck; float wait; } mpend = { { 0, 0, 0, 0, 0.0f }, -1, 0, 0.0f };

/* F6c, the melody hears the harmony (Marote, 19 nov: the phrases' and bars' ends sounded wrong). Planned in the
 * key's degrees, each note now asks the chord that will sound under it (chord_ahead: the conductor knows):
 * - a strong note (a beat, or held) is a chord tone or a tension the chord takes (in its scale, not a half step over
 *   a chord tone); else it moves to the nearest that is, the way the line goes;
 * - a seed's alteration (a leading tone) stays only on a dominant or where it is a chord tone;
 * - a note held into a chord it clashes with stops at the change, struck again on the new chord's nearest tone;
 * - the phrase's last note is a cadence on the chord really sounding: before the home chord, its 2nd, 7th or 5th
 *   (the next phrase resolves it); elsewhere the chord's root or 3rd; held to the phrase's end. */
static const struct chord *mel_chord_at(int v)   /* the chord under the melody's 16th v of this phrase */
{
    return chord_ahead((uint32_t)((float)v * 2.0f / (float)(feel[TRK_MELODY] ? feel[TRK_MELODY] : 2u)));
}
static int mf_class(const struct chord *c, int midi)   /* 0 a chord tone, 1 a tension, 2 an avoid note, 3 out */
{
    int pc = ((midi % 12) + 12) % 12, root = (key_root + c->root) % 12, ct = 0, avoid = 0;
    for (int i = 0; i < 4; ++i) { int t = chord_pc(c, i); if (t == pc) ct = 1; if ((t + 1) % 12 == pc) avoid = 1; }
    if (ct) return 0;
    if (!(SCALE[c->sc] >> ((pc - root + 12) % 12) & 1u)) return 3;
    return avoid ? 2 : 1;
}
static void mf_set(struct mev *e, int midi)   /* a pitch back into degrees (+ an alteration) */
{
    int best = 0, bp = -999;
    for (int d = -16; d <= 24; ++d) { int p = deg_pitch(d, 0); if (p <= midi && p > bp) { bp = p; best = d; } }
    e->deg = (int8_t)best; e->alt = (int8_t)(midi - bp);
}
static int mf_nearest(const struct chord *c, int midi, int dir, int worst)   /* the nearest pitch of class <= worst */
{
    for (int pass = 0; pass < 2; ++pass)   /* in the key's scale first, then any */
        for (int d = 0; d <= 6; ++d)
            for (int sgn = 0; sgn < 2; ++sgn) {
                int m = midi + (sgn == 0 ? (dir >= 0 ? d : -d) : (dir >= 0 ? -d : d));
                if (mf_class(c, m) > worst) continue;
                if (pass == 0) { int ok = 0; uint16_t ks = key_scale(); if (ks >> ((((m - mel_tonic()) % 12) + 12) % 12) & 1u) ok = 1; if (!ok) continue; }
                return m;
            }
    return midi;
}
static void mel_fit(void)
{
    int on[32], n = 0;
    for (int v = 0; v < 32; ++v) if (mseq[v].on) on[n++] = v;
    for (int i = 0; i < n; ++i) {
        int v = on[i];
        struct mev *e = &mseq[v];
        const struct chord *c = mel_chord_at(v);
        int midi = deg_pitch(e->deg, e->alt);
        int next = i + 1 < n ? deg_pitch(mseq[on[i + 1]].deg, mseq[on[i + 1]].alt) : midi;
        if (e->alt && c->q != Q_DOM && mf_class(c, midi) != 0) midi -= e->alt;   /* a leading tone off its dominant */
        if ((v % 4 == 0 || e->len >= 2 || i == 0) && mf_class(c, midi) >= 2) midi = mf_nearest(c, midi, next - midi, 1);
        mf_set(e, midi);
        /* held into a chord it clashes with: it stops there, struck again on the new chord */
        for (int w = v + 1; w < v + e->len && w < 32; ++w) {
            const struct chord *cw = mel_chord_at(w);
            if (cw == mel_chord_at(w - 1)) continue;
            if (mf_class(cw, midi) >= 2) {
                int rest = v + e->len - w;
                e->len = (int8_t)(w - v);
                if (rest >= 2 && !mseq[w].on) {
                    mseq[w].on = 1; mseq[w].len = (int8_t)rest; mseq[w].vel = 0.75f;
                    mf_set(&mseq[w], mf_nearest(cw, midi, 0, 0));
                }
            }
            break;
        }
    }
    /* the cadence: the phrase's last note */
    n = 0;
    for (int v = 0; v < 32; ++v) if (mseq[v].on) on[n++] = v;
    if (!n) return;
    {
        int v = on[n - 1];
        struct mev *e = &mseq[v];
        const struct chord *c = mel_chord_at(v), *nx = mel_chord_at(32);
        int midi = deg_pitch(e->deg, e->alt), prev = n > 1 ? deg_pitch(mseq[on[n - 2]].deg, mseq[on[n - 2]].alt) : midi;
        int best = midi, bd = 99;
        for (int m = prev - 7; m <= prev + 7; ++m) {
            int pc = ((m % 12) + 12) % 12, k = key_root % 12, rel = (pc - k + 12) % 12, ok;
            if (mf_class(c, m) != 0) continue;
            if (nx->root == 0 && c->root != 0) ok = rel == 2 || rel == 10 || rel == 11 || rel == 7;   /* before home */
            else ok = pc == chord_pc(c, 0) || pc == chord_pc(c, 1);                                   /* root or 3rd */
            if (ok && iabs(m - prev) < bd) { bd = iabs(m - prev); best = m; }
        }
        if (bd == 99) best = mf_nearest(c, midi, prev - midi, 0);
        mf_set(e, best);
        if (32 - (v + e->len) > 0 && 32 - (v + e->len) <= 6) e->len = (int8_t)(32 - v);   /* held to the end */
    }
}
static void mel_sound(const struct mev *e, int v, int duck)
{
    float E = mel_expr();
    int midi = deg_pitch(e->deg, e->alt);
    if (v % 8 == 0 && e->len >= 2) midi = snap_to_chord(midi);
    while (midi > 86) midi -= 12;
    while (midi < 55) midi += 12;
    float vel = e->vel * mx_vel[v], gate = mx_gate[v];
    /* the charge: a note off the chord (on a beat, or held) a tension, louder and longer; the step after it onto a
     * chord tone its resolution, softer; a chord away from home, a little more */
    int chord_tone = 0;
    for (int i = 0; i < 4; ++i) if ((((midi - chord_pc(cur_chord, i)) % 12) + 12) % 12 == 0) chord_tone = 1;
    if (!chord_tone && (v % 4 == 0 || e->len >= 2)) { vel *= 1.0f + 0.12f * E; gate *= 1.0f + 0.08f * E; m_tension = midi; }
    else {
        if (m_tension && chord_tone && iabs(midi - m_tension) <= 2) vel *= 1.0f - 0.12f * E;
        m_tension = 0;
    }
    if (cur_chord->root != 0) vel *= 1.0f + (cur_chord->q == Q_DOM ? 0.08f : 0.04f) * E;
    if (duck) vel *= 0.63f;                                                   /* under the snare: -4 dB */
    vel = vel < 0.2f ? 0.2f : vel > 1.25f ? 1.25f : vel;
    lead_note(midi, (float)e->len * vsps(TRK_MELODY) * 0.9f * gate, vel);
}
static void mel_step(int v)
{
    if (v == 0) { mel_plan(); mel_fit(); mel_shape(); }
    if (form_hole(TRK_MELODY)) { if (lead.stage != 3) lead.stage = 3; mpend.v = -1; return; }
    if (!mseq[v].on) return;
    uint32_t step = g % 32u;
    int duck = mseq[v].len <= 2 && pat[step].active && (c_snares >> pat[step].slice & 1u);
    if (mpend.v >= 0) { mel_sound(&mpend.e, mpend.v, mpend.duck); mpend.v = -1; }   /* one still waiting: now */
    float late = mel_late(v);
    if (late >= 1.0f) { mpend.e = mseq[v]; mpend.v = v; mpend.duck = duck; mpend.wait = late; return; }
    mel_sound(&mseq[v], v, duck);
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
    song_seed = rng;   /* (a slot's seed: the same song again from it) */
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
        bass_play(root_midi(chord_ahead(ahead)) - 1, 2.0f * sps, 0, 1, 1.0f);
    }
}
static int form_hole(int t)   /* the harmony's hole before a rise (half a bar), everything's last bar before a drop */
{
    uint32_t left;
    if (!form_on || !(trans_up || trans_drop)) return 0;
    left = (sec_start + sec_len - bpos) / feel[TRK_BREAK];
    if (trans_drop) return left <= 16u;
    return (t == TRK_HARMONY || t == TRK_MELODY) && left <= 8u;
}

/* ------------------------------------------------------ the tracks' clocks --- */
/* A track's 16th v (0..31, its phrase) from the break's 16th g: half speed on even g (v = g/2), double twice (2g now,
 * 2g+1 half a 16th later). Aligned on g, so every speed meets on the break's phrases. */
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
    else if (t == TRK_MELODY) mel_step(v);
}
/* F5, the groove: a track's odd 16th comes late by its swing (of its own 16th: at half speed the swing of the real
 * 8ths); then the hands: the harmony a little behind the beat, the melody loose, the bass tight. Each 16th waits
 * in its track's queue (a double speed's second one too), its accent read as it sounds. */
static const float ACC_K[TRACKS] = { 0.0f, 0.5f, 1.0f, 0.0f };          /* how much of the accents each track takes (the melody: its phrase's, F5b) */
static const float LATE_MS[TRACKS] = { 0.0f, 0.0f, 2.5f, 0.0f }, LOOSE_MS[TRACKS] = { 0.0f, 1.5f, 4.5f, 5.0f };
static struct { float wait; int8_t v; } tq[TRACKS][3];
static int tq_n;
static void vstep_go(int t, int v)
{
    acc_now[t] = 1.0f + c_acc[t] * ACC_K[t] * (GROOVE[track_style(t)].acc[v % 16] - 1.0f);
    track_vstep(t, v);
}
static void vstep_at(int t, int v, float wait)
{
    if (t != TRK_MELODY) {   /* (the melody's timing: its notes', mel_late) */
        if (v & 1) wait += swing_of(t) * vsps(t);
        wait += human_of(t) * (LATE_MS[t] + LOOSE_MS[t] * hrand()) * (SR * 0.001f);
    }
    if (wait >= 1.0f) for (int i = 0; i < 3; ++i) if (tq[t][i].v < 0) { tq[t][i].v = (int8_t)v; tq[t][i].wait = wait; ++tq_n; return; }
    vstep_go(t, v);
}
static void track_tick(int t)
{
    uint8_t f = feel[t];
    if (f == 2) vstep_at(t, (int)(g % 32u), 0.0f);
    else if (f == 1) { if (!(g & 1u)) vstep_at(t, (int)(g / 2u % 32u), 0.0f); }
    else {
        vstep_at(t, (int)(2u * g % 32u), 0.0f);
        vstep_at(t, (int)((2u * g + 1u) % 32u), sps * 0.5f);
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
    if (step == 0) jungle_pattern(pat);   /* the phrase's hits: the melody keeps off its snares */
    block_left = (uint8_t)(64u - g % 64u);
    if (g % 64u == 0) {   /* every 4 bars (a phrase of every speed starts): a slot's seed, the styles and their speeds */
        ++blocks;
        if (c_seed_set) { c_seed_set = 0; rng = c_seed ? c_seed : 1u; song_req = 1; }
        if (!form_on) song_seed = rng;
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
        if (c != cur_chord) { cur_chord = c; if (pad_on && !form_hole(TRK_HARMONY)) layer_chord(&pad, c, COLOUR, 0, 1.0f, 0, 1, 0.0f); }
    }
    if (g % 16u == 0) autodub_bar();
    jungle_swing((unsigned)(swing_of(TRK_BREAK) * (float)JUNGLE_TICKS + 0.5f));   /* the break's own swing, from its next 16th */
    track_tick(TRK_BASS);
    track_tick(TRK_HARMONY);
    track_tick(TRK_MELODY);
    if (jungle_roll_active() == 1) build_was_on = 1;
    bpos += feel[TRK_BREAK];   /* the BASE's time: its own speed */
}
static void fx_throw(int slice);
static void on_hit(int slice, int pitch)
{
    static const uint16_t kick_mask = (1u << 0) | (1u << 1) | (1u << 5) | (1u << 9) | (1u << 13);   /* Amen */
    (void)pitch;
    fx_throw(slice);
    if (bmode != MBASS_KICK || jungle_roll_active() == 1 || !cur_chord) return;
    if (!(kick_mask >> slice & 1u)) return;
    int note = root_midi(cur_chord);
    bass_play(note < 36 ? note + 12 : note, SR * 0.12f, jungle_roll_active() == 2, 1, 1.0f + human_of(TRK_BASS) * 0.12f * (hrand() - 0.5f));
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
    bass.vel = 1.0f;
    for (int v = 0; v < 3; ++v) { rhy.v[v].str.b = ks_rhy_buf[v]; rhy.v[v].str.cap = 512; rhy.v[v].fm = &fm6_rhy[v]; }
    lead.str.b = ks_lead_buf; lead.str.cap = 512;
    for (int t = 0; t < TRACKS; ++t) for (int i = 0; i < 3; ++i) tq[t][i].v = -1;
    bass_slide = ms_coef(25.0f);
    next_prog_ix = 0;
    cycle_start();
    cur_chord = chord_in(P(), 0, cyc_speed);
}

/* ============================================================ the effects (F6a) === */
/* Marote, 18 nov: an ECHO (a tape's: at half the rate, darker and dirtier each time round, its time gliding when
 * changed, up to running away past 100), a REVERB (ROOM: Freeverb's 4 combs and 2 allpasses, at half the rate, the
 * allpasses apart left and right; SPRING: a loop through a chain of stretched allpasses, the chirp, after Felucca's
 * (GPL-3, Leo Kuroshita), in floats), a CHORUS (two taps swaying, left and right); each track sends to them (the
 * break too: and its throws, its phrase's last snare into the echo now and then, dub's move), pans, and the
 * returns come back over the dry. What a style starts with: FXS. The break's own sends start at 0: Amenity alone
 * sounds as it always did. */
enum { FX_ROOM, FX_SPRING, FX_RTYPES };
#define ECHO_N 32768u                      /* 1.49 s at 22.05 kHz, int16 */
static int16_t echo_buf[ECHO_N] PLAT_POOL;
static const uint16_t RC_LEN[4] = { 557, 593, 641, 677 }, RA_LEN[4] = { 277, 211, 263, 233 };   /* combs; allpasses L, L, R, R */
#define SP_N 1024u                         /* the spring's loop line (floats, a power of two) */
#define SP_STAGES 10
static float rv_comb[557 + 593 + 641 + 677] PLAT_POOL, rv_ap[277 + 211 + 263 + 233] PLAT_POOL, sp_line[SP_N] PLAT_POOL;
#define CH_N 1024u
static float ch_buf[CH_N] PLAT_POOL;
static const struct {
    uint8_t div, fb, color, rtype, decay, ret, thr, se[TRACKS], sr[TRACKS], ch[TRACKS];
} FXS[MSTYLES] = {   /* div: 1/8 3/16 1/4 3/8 1/2 (0..4); the sends 0..127 (the break's: by hand, 0) */
    { 1, 40, 70, FX_ROOM, 50, 90, 0,  { 0, 0, 10, 20 }, { 0, 0, 40, 35 }, { 0, 0, 30, 0 } },    /* breakcore */
    { 2, 30, 60, FX_ROOM, 60, 90, 0,  { 0, 0, 0, 15 },  { 0, 10, 40, 45 }, { 0, 0, 20, 10 } },  /* jazz */
    { 1, 60, 90, FX_ROOM, 40, 90, 0,  { 0, 35, 15, 30 }, { 0, 5, 25, 25 }, { 0, 0, 30, 0 } },   /* acid: the line's 3/16 */
    { 0, 35, 50, FX_ROOM, 35, 90, 15, { 0, 0, 10, 25 }, { 0, 0, 30, 30 }, { 0, 0, 30, 0 } },   /* hip hop */
    { 3, 75, 35, FX_SPRING, 70, 100, 60, { 0, 0, 55, 40 }, { 0, 0, 45, 35 }, { 0, 0, 15, 0 } }, /* dub */
};
static const uint8_t DIV16[5] = { 2, 3, 4, 6, 8 };   /* the divisions, in the BASE's 16ths */
/* the controls (main loop): MAUTO = the style's; the echo's time: 0..4 a division, 10.. free (100 ms + 50 a step) */
static volatile uint8_t c_etime = MAUTO, c_efb = MAUTO, c_ecolor = MAUTO, c_rtype = MAUTO, c_rdecay = MAUTO, c_fxret = MAUTO,
    c_throw = MAUTO, c_se[TRACKS] = { 0, MAUTO, MAUTO, MAUTO }, c_sr[TRACKS] = { 0, MAUTO, MAUTO, MAUTO },
    c_ch[TRACKS] = { 0, MAUTO, MAUTO, MAUTO }, c_pan[TRACKS] = { 64, 64, 64, 64 };
void music_fx(unsigned what, unsigned v)
{
    uint8_t x = (uint8_t)(v > 127u && v != MAUTO ? 127u : v);
    switch (what) {
    case MFX_ETIME: c_etime = (uint8_t)v; break;
    case MFX_EFB: c_efb = x; break;
    case MFX_ECOLOR: c_ecolor = x; break;
    case MFX_RTYPE: c_rtype = (uint8_t)(v < FX_RTYPES ? v : MAUTO); break;
    case MFX_RDECAY: c_rdecay = x; break;
    case MFX_RETURN: c_fxret = x; break;
    case MFX_THROW: c_throw = x; break;
    }
}
void music_send(unsigned track, unsigned what, unsigned v)
{
    uint8_t x = (uint8_t)(v > 127u && v != MAUTO ? 127u : v);
    if (track >= TRACKS) return;
    if (what == MSEND_ECHO) c_se[track] = x;
    else if (what == MSEND_REVERB) c_sr[track] = x;
    else if (what == MSEND_CHORUS) c_ch[track] = x;
    else if (what == MSEND_PAN) c_pan[track] = (uint8_t)(v > 127u ? 64u : v);
}
static uint8_t fx_of(uint8_t c, uint8_t style_v) { return c == MAUTO ? style_v : c; }
/* the state */
static struct {
    float e_time, e_target, e_lp, e_hp, e_wow, e_out0, e_out1;   /* the echo's time (half-rate samples), its loop's filters */
    uint32_t e_w;
    float rc_lp[4]; uint16_t rc_i[4], ra_i[4];
    float sp_lp, sp_hp, sp_x[SP_STAGES][2], sp_y[SP_STAGES][2]; uint32_t sp_w; uint8_t sp_k;
    float r_l0, r_l1, r_r0, r_r1;                                 /* the reverb's last two outputs (back to 44.1 kHz) */
    uint32_t ch_w; float ch_ph;
    float in_e, in_r;                                             /* the sends, summed over the two samples */
    float throw_env;
    int idle;                                                     /* samples since the echo or the reverb last had sound */
} fx;
/* the echo, one half-rate sample: in -> out (its wet) */
static float echo_tick(float in, float fb, float lp_k, float wobble)
{
    fx.e_time += (fx.e_target - fx.e_time) * 0.0006f;          /* the time glides (a tape sped up or slowed: the pitch bends) */
    float d = fx.e_time * (1.0f + wobble);
    if (d < 2.0f) d = 2.0f; else if (d > (float)(ECHO_N - 4u)) d = (float)(ECHO_N - 4u);
    int di = (int)d; float fr = d - (float)di;
    uint32_t r0 = (fx.e_w - (uint32_t)di) & (ECHO_N - 1u), r1 = (r0 - 1u) & (ECHO_N - 1u);
    float y = ((float)echo_buf[r0] + fr * (float)(echo_buf[r1] - echo_buf[r0])) * (1.0f / 8192.0f);
    /* round the loop: darker (the colour), no rumble (~120 Hz), the tape's saturation */
    fx.e_lp += (y - fx.e_lp) * lp_k;
    fx.e_hp += (fx.e_lp - fx.e_hp) * 0.034f;
    float w = in + fm_tanhf(1.2f * fb * (fx.e_lp - fx.e_hp)) * (1.0f / 1.2f);
    w = w > 3.9f ? 3.9f : w < -3.9f ? -3.9f : w;
    echo_buf[fx.e_w & (ECHO_N - 1u)] = (int16_t)(w * 8192.0f);
    ++fx.e_w;
    return y;
}
static void room_tick(float in, float g, float damp, float *l, float *r)
{
    float a = 0.0f, *c = rv_comb;
    in *= 0.18f;
    for (int k = 0; k < 4; ++k) {
        float o = c[fx.rc_i[k]];
        fx.rc_lp[k] = o + (fx.rc_lp[k] - o) * damp;
        c[fx.rc_i[k]] = in + fx.rc_lp[k] * g;
        if (++fx.rc_i[k] >= RC_LEN[k]) fx.rc_i[k] = 0;
        a += o;
        c += RC_LEN[k];
    }
    float out[2], *p = rv_ap;
    for (int ch = 0; ch < 2; ++ch) {   /* the same combs, two allpass pairs: left and right apart */
        float x = a;
        for (int k = 2 * ch; k < 2 * ch + 2; ++k) {
            static const uint16_t RA_OFF[4] = { 0, 277, 277 + 211, 277 + 211 + 263 };
            float *q = p + RA_OFF[k];
            float o = q[fx.ra_i[k]];
            q[fx.ra_i[k]] = x + o * 0.5f;
            x = o - x;
            if (++fx.ra_i[k] >= RA_LEN[k]) fx.ra_i[k] = 0;
        }
        out[ch] = x;
    }
    *l = out[0]; *r = out[1];
}
static void spring_tick(float in, float g, float *l, float *r)
{
    const uint32_t L = 990u;                                    /* the loop: 45 ms */
    float x = in * 0.25f + fx.sp_lp * g;
    float back = sp_line[(fx.sp_w - L) & (SP_N - 1u)];
    fx.sp_lp += (back - fx.sp_lp) * 0.45f;                       /* the loop's low-pass (~4 kHz) */
    fx.sp_hp += (x - fx.sp_hp) * 0.03f;                          /* its low cut (~100 Hz) */
    x -= fx.sp_hp;
    int k2 = fx.sp_k, k0 = k2 ^ 1;                               /* the chain: allpasses stretched to 2 samples, 0.7 */
    for (int s = 0; s < SP_STAGES; ++s) {
        float y = -0.7f * x + fx.sp_x[s][k2] + 0.7f * fx.sp_y[s][k2];
        fx.sp_x[s][k2] = x; fx.sp_y[s][k2] = y;
        x = y;
    }
    (void)k0;
    fx.sp_k ^= 1u;
    sp_line[fx.sp_w & (SP_N - 1u)] = x;
    ++fx.sp_w;
    *l = 2.0f * sp_line[(fx.sp_w - L / 2u) & (SP_N - 1u)];
    *r = 2.0f * sp_line[(fx.sp_w - (3u * L) / 4u) & (SP_N - 1u)];
}
static void chorus_tick(float in, float *l, float *r)
{
    ch_buf[fx.ch_w & (CH_N - 1u)] = in;
    fx.ch_ph += 0.45f / SR;
    if (fx.ch_ph >= 1.0f) fx.ch_ph -= 1.0f;
    float m1 = sine(fx.ch_ph), m2 = sine(fx.ch_ph + 0.25f >= 1.0f ? fx.ch_ph - 0.75f : fx.ch_ph + 0.25f);
    float d1 = 300.0f + 110.0f * m1, d2 = 300.0f + 110.0f * m2;  /* ~7 ms, +-2.5 */
    int i1 = (int)d1, i2 = (int)d2; float f1 = d1 - (float)i1, f2 = d2 - (float)i2;
    uint32_t w = fx.ch_w;
    float a0 = ch_buf[(w - (uint32_t)i1) & (CH_N - 1u)], a1 = ch_buf[(w - (uint32_t)i1 - 1u) & (CH_N - 1u)];
    float b0 = ch_buf[(w - (uint32_t)i2) & (CH_N - 1u)], b1 = ch_buf[(w - (uint32_t)i2 - 1u) & (CH_N - 1u)];
    *l = a0 + f1 * (a1 - a0);
    *r = b0 + f2 * (b1 - b0);
    ++fx.ch_w;
}
static void fx_throw(int slice)   /* a break's hit: its phrase's last snare, now and then, into the echo */
{
    uint8_t thr = fx_of(c_throw, FXS[base_style].thr);
    if (!thr || !(c_snares >> slice & 1u) || g % 32u < 28u) return;
    if (hrand() * 127.0f < (float)thr) fx.throw_env = 1.0f;
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
    v->mel_mode = mel_mode_now;
    v->mel_sound = mel_sound_now;
    v->mel_seed = mel_seed_now;
    v->form = c_form;
    for (int t = 0; t < TRACKS; ++t) v->swing[t] = (uint8_t)swing_pct(t);
    v->human = (uint8_t)(c_human == MAUTO ? GROOVE[base_style].human : c_human);
    v->etime = c_etime == MAUTO ? FXS[base_style].div : c_etime;
    v->efb = fx_of(c_efb, FXS[base_style].fb);
    v->ecolor = fx_of(c_ecolor, FXS[base_style].color);
    v->rtype = fx_of(c_rtype, FXS[base_style].rtype);
    v->rdecay = fx_of(c_rdecay, FXS[base_style].decay);
    v->fxret = fx_of(c_fxret, FXS[base_style].ret);
    v->thr = fx_of(c_throw, FXS[base_style].thr);
    for (int t = 0; t < TRACKS; ++t) {
        v->se[t] = fx_of(c_se[t], FXS[track_style(t)].se[t]);
        v->sr[t] = fx_of(c_sr[t], FXS[track_style(t)].sr[t]);
        v->ch[t] = fx_of(c_ch[t], FXS[track_style(t)].ch[t]);
    }
    v->section = form_on && !song_req ? sect()->name : "";
    v->sect_bar = (uint8_t)(form_on && !song_req ? (bpos - sec_start) / 32u + 1u : 0u);
    v->sect_bars = (uint8_t)(form_on && !song_req ? sec_len / 32u : 0u);
}


enum { CHUNK = 256 };
static float buf_bass[CHUNK], buf_harm[2 * CHUNK], buf_mel[CHUNK];
static float gain[TRACKS] = { 1.0f, 0.0f, 0.0f, 0.0f };
static void render_chunk(int32_t *lr, unsigned n)
{
    unsigned e = 0;
    drift_chunk(n);
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
            pend_n = 0;
            for (int t = 0; t < TRACKS; ++t) for (int i = 0; i < 3; ++i) tq[t][i].v = -1;
            tq_n = 0;
            if (lead.stage) lead.stage = 3;
            mpend.v = -1;
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
        if (mpend.v >= 0 && (mpend.wait -= 1.0f) <= 0.0f) { int pv = mpend.v; mpend.v = -1; mel_sound(&mpend.e, pv, mpend.duck); }
        if (tq_n) for (int t = TRK_BASS; t <= TRK_MELODY; ++t)   /* the 16ths waiting: swung, late, a double speed's second */
            for (int q = 0; q < 3; ++q)
                if (tq[t][q].v >= 0 && (tq[t][q].wait -= 1.0f) <= 0.0f) { int v = tq[t][q].v; tq[t][q].v = -1; --tq_n; vstep_go(t, v); }
        pend_tick();
        float l = 0.0f, r = 0.0f;
        buf_bass[i] = bass_render() * 0.55f;
        buf_mel[i] = lead_render() * 0.32f;
        layer_render(&pad, &l, &r, g_pad);
        layer_render(&rhy, &l, &r, g_pad);
        buf_harm[2 * i] = l;
        buf_harm[2 * i + 1] = r;
    }
    for (; e < nev; ++e) if (ev[e].type == EV_STEP) on_step(ev[e].a, frame_base + n);
    nev = 0;
    frame_base += n;
    /* the effects' settings, this chunk (F6a): the BASE's style for the busses, each track's for its sends */
    const int bs = base_style;
    float efb = (float)fx_of(c_efb, FXS[bs].fb) * (1.0f / 100.0f);                 /* 1.27 at the top: it runs away */
    if (!jungle_playing() && efb > 0.85f) efb = 0.85f;   /* stopped: a runaway lets go, its tail dies away */
    float col = (float)fx_of(c_ecolor, FXS[bs].color) * (1.0f / 127.0f);
    float e_lpk = 1.0f - fm_expf(-6.2831853f * (900.0f + 6500.0f * col * col) * (2.0f / SR));
    uint8_t et = c_etime == MAUTO ? FXS[bs].div : c_etime;
    fx.e_target = et < 5u ? (float)DIV16[et] * sps * 0.5f * (2.0f / (float)(feel[TRK_BREAK] ? feel[TRK_BREAK] : 2u))
                          : (float)(((int)et - 10) * 50 + 100) * (SR * 0.0005f);
    if (fx.e_time <= 0.0f) fx.e_time = fx.e_target;
    int spring = fx_of(c_rtype, FXS[bs].rtype) == FX_SPRING || c_stress;   /* (the CPU test: the dearer one) */
    float dec = (float)fx_of(c_rdecay, FXS[bs].decay) * (1.0f / 127.0f);
    float rg = spring ? 0.5f + 0.42f * dec : 0.70f + 0.27f * dec;
    float ret = (float)fx_of(c_fxret, FXS[bs].ret) * (1.0f / 96.0f);
    float se[TRACKS], sr_[TRACKS], ch[TRACKS], pl[TRACKS], pr[TRACKS];
    for (int t = 0; t < TRACKS; ++t) {
        int ts = track_style(t);
        se[t] = (float)fx_of(c_se[t], FXS[ts].se[t]) * (1.0f / 127.0f);
        sr_[t] = (float)fx_of(c_sr[t], FXS[ts].sr[t]) * (1.0f / 127.0f);
        ch[t] = (float)fx_of(c_ch[t], FXS[ts].ch[t]) * (1.0f / 127.0f);
        float p = (float)c_pan[t] * (1.0f / 127.0f);                                 /* the balance: centre 1 / 1 */
        pl[t] = p < 0.5f ? 1.0f : 2.0f * (1.0f - p);
        pr[t] = p > 0.5f ? 1.0f : 2.0f * p;
    }
    if (c_stress) for (int t = TRK_BASS; t < TRACKS; ++t) { if (ch[t] <= 0.0f) ch[t] = 0.25f; if (se[t] <= 0.0f) se[t] = 0.2f; if (sr_[t] <= 0.0f) sr_[t] = 0.2f; }
    int ch_on = ch[TRK_BASS] + ch[TRK_HARMONY] + ch[TRK_MELODY] > 0.0f;
    static float wow_ph;
    wow_ph += (float)n * (0.55f / SR);
    if (wow_ph >= 1.0f) wow_ph -= 1.0f;
    float wob = 0.0012f * sine(wow_ph);
    static unsigned par;
    /* the mix: each track's gain glides to its target in ~5 ms (no click); the break's own gain on what jungle.c made */
    const float glide = 1.0f / 220.0f, thr_dec = 1.0f - 1.0f / (0.18f * SR);
    for (unsigned i = 0; i < n; ++i) {
        for (int t = 0; t < TRACKS; ++t) {
            float d = tgt[t] - gain[t];
            gain[t] = d > glide ? gain[t] + glide : d < -glide ? gain[t] - glide : tgt[t];
        }
        int32_t a = lr[2 * i], c = lr[2 * i + 1];
        if (gain[TRK_BREAK] != 1.0f) { a = (int32_t)((float)a * gain[TRK_BREAK]); c = (int32_t)((float)c * gain[TRK_BREAK]); }
        float bv = buf_bass[i] * gain[TRK_BASS], mv = buf_mel[i] * gain[TRK_MELODY];
        float hl = buf_harm[2 * i] * gain[TRK_HARMONY], hr = buf_harm[2 * i + 1] * gain[TRK_HARMONY], hm = 0.5f * (hl + hr);
        float dl = bv * pl[TRK_BASS] + mv * pl[TRK_MELODY] + hl * pl[TRK_HARMONY];
        float dr = bv * pr[TRK_BASS] + mv * pr[TRK_MELODY] + hr * pr[TRK_HARMONY];
        /* the sends */
        float sb = se[TRK_BREAK] > fx.throw_env ? se[TRK_BREAK] : fx.throw_env, bm = 0.0f;
        if (sb > 0.0f || sr_[TRK_BREAK] > 0.0f) bm = (float)(a + c) * (0.5f / 8388607.0f);
        fx.throw_env *= thr_dec;
        if (fx.throw_env < 1e-3f) fx.throw_env = 0.0f;
        float es = bv * se[TRK_BASS] + mv * se[TRK_MELODY] + hm * se[TRK_HARMONY] + bm * sb;
        float rs = bv * sr_[TRK_BASS] + mv * sr_[TRK_MELODY] + hm * sr_[TRK_HARMONY] + bm * sr_[TRK_BREAK];
        if (ch_on) {   /* the chorus: its wet beside the dry (the width) */
            float cl, cr;
            chorus_tick(bv * ch[TRK_BASS] + mv * ch[TRK_MELODY] + hm * ch[TRK_HARMONY], &cl, &cr);
            dl += 0.8f * cl; dr += 0.8f * cr;
        }
        /* the echo and the reverb, at half the rate (every other sample, the pair's mean in), unless all is silent */
        fx.in_e += es; fx.in_r += rs;
        if (es != 0.0f || rs != 0.0f) fx.idle = 0;
        if (par++ & 1u) {
            if (fx.idle < (int)(0.6f * SR) || efb >= 1.0f) {
                float eo = echo_tick(0.5f * fx.in_e, efb, e_lpk, wob), l, r;
                if (spring) spring_tick(0.5f * (fx.in_r + 0.5f * eo), rg, &l, &r);
                else room_tick(0.5f * (fx.in_r + 0.5f * eo), rg, 0.3f, &l, &r);
                fx.e_out0 = fx.e_out1; fx.e_out1 = eo;
                fx.r_l0 = fx.r_l1; fx.r_l1 = l; fx.r_r0 = fx.r_r1; fx.r_r1 = r;
                if (fm_fabsf(eo) + fm_fabsf(l) > 1e-4f) fx.idle = 0;
            } else fx.e_out0 = fx.e_out1 = fx.r_l0 = fx.r_l1 = fx.r_r0 = fx.r_r1 = 0.0f;
            fx.in_e = fx.in_r = 0.0f;
            dl += ret * (0.5f * (fx.e_out0 + fx.e_out1) + 0.5f * (fx.r_l0 + fx.r_l1));
            dr += ret * (0.5f * (fx.e_out0 + fx.e_out1) + 0.5f * (fx.r_r0 + fx.r_r1));
        } else {
            dl += ret * (fx.e_out1 + fx.r_l1);
            dr += ret * (fx.e_out1 + fx.r_r1);
        }
        if (fx.idle < 0x7fffffff) ++fx.idle;
        if (dl != 0.0f || dr != 0.0f) {   /* the tracks, softly; then, past -1 dB, the sum bends (only with the tracks in it:
                                           * Amenity alone stays bit for bit) */
            float L = 0.75f * soft(1.2f * dl), R = 0.75f * soft(1.2f * dr);
            float tl = (float)a * (1.0f / 8388607.0f) + L, tr = (float)c * (1.0f / 8388607.0f) + R;
            float al = fm_fabsf(tl), ar = fm_fabsf(tr);
            if (al > 0.89f) { float y = 0.89f + 0.11f * soft((al - 0.89f) * (1.0f / 0.11f)); tl = tl < 0.0f ? -y : y; }
            if (ar > 0.89f) { float y = 0.89f + 0.11f * soft((ar - 0.89f) * (1.0f / 0.11f)); tr = tr < 0.0f ? -y : y; }
            a = (int32_t)(tl * 8388607.0f);
            c = (int32_t)(tr * 8388607.0f);
        }
        lr[2 * i] = a > 8388607 ? 8388607 : a < -8388607 ? -8388607 : a;          /* 24 bits: never wrap */
        lr[2 * i + 1] = c > 8388607 ? 8388607 : c < -8388607 ? -8388607 : c;
    }
}
void music_render(int32_t *lr, unsigned n)
{
    while (n > CHUNK) { render_chunk(lr, CHUNK); lr += 2 * CHUNK; n -= CHUNK; }   /* (the FM-1's block is 256) */
    render_chunk(lr, n);
}
