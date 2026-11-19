/* SPDX-License-Identifier: GPL-3.0-only AND MPL-2.0
 * Copyright (C) 2026 Presethead. text_cat, int_text and app_say are from fm1-base's template (template/app.c),
 * Copyright (C) 2026 Charles Vestal, MPL-2.0 (base/LICENSE); the rest is GPL-3.0-only (LICENSE.md). */
/* AMENITY on fm1-base: the panel, the screen, MIDI clock in and out, around the break engine (jungle/).
 *
 *   white keys (hold)  rolls and effects, last held wins     black keys 1-8  one-shot hits
 *   top three black keys (hold)  C# feedback at 75%, D# LO-FI at its most, F# an octave down: the knobs'
 *                                values again on release; the SEQ fill picks these too
 *   K1 FEEDBACK  K2 VARIATION  K3 WARP  K4 FILTER            FX held + key   the feedback's root
 *   PRESET the break (the built-in ones, then your own: user_breaks.c)  ALGORITHM transpose (down)
 *   SELECT tempo (1 BPM; SEL held: 0.1)  SEL + OCT- / OCT+ 0.1 BPM  OCT- / OCT+ halve / double tempo
 *   PLAY start/stop  REC freeze  SEQ fill  HOME straight break
 *   EDIT held: the audio's CPU load in the header, and its parts (B the break, T the tracks, F the effects; FM6
 *              voices sounding / allowed, - those the governor let go)
 *   ENV: EQ page (K1-K4: 100 Hz, 500 Hz, 2.5 kHz, 8 kHz)  FX tap: FX page (K1 saturation, K2 compression, K3 LO-FI)
 *   MIDI clock: a Start or Continue from an input leads (EXT; PLAY and tempo rest); on its own clock AMENITY
 *   sends Start, clock and Stop.
 *
 * Part of fm1-base's unity build, after the platform: gfx.c is in scope. */
#include "jungle.h"
#include "jungle_mul.h"
#include "music.h"                      /* Facility: the tracks beside the break */
#include <stddef.h>                     /* offsetof: the slots of older kinds */

/* ------------------------------------------------------------------ audio --- */
/* What the render makes is heard one block later (the I2S buffer's other half): the engine works on that time, so
 * the break and the clock it sends line up with what comes out. */
#define HEARD_TICKS ((uint32_t)((uint64_t)APP_BLOCK * PLAT_TICKS_PER_US * 1000000u / 44100u))
static volatile uint8_t muted;
/* The time stamps' clock: the 24 MHz timer. The host simulator's plat_ticks24 is the computer's real time (for
 * measuring the render), which runs far behind the simulation: there the simulated milliseconds stand in. */
#ifdef PLAT_HOST
static uint32_t now_ticks(void) { return plat_ms() * (PLAT_TICKS_PER_US * 1000u); }
#else
static uint32_t now_ticks(void) { return plat_ticks24(); }
#endif

/* the meter (EDIT held, Amenity+ OPT): the break's, the tracks' and the mix's share of the block's time, smoothed as
 * the platform's CPU figure (about 16 blocks), in 1/4096 */
static uint32_t meter_q[3];
static void meter_add(int k, uint32_t ticks, uint32_t n)
{
    uint32_t budget = n * (PLAT_TICKS_PER_US * 1000000u / 1000u) / 44u;   /* (24 MHz / 44.1 kHz ~ 544.2 ticks a frame) */
    uint32_t x = ticks > budget * 4u ? 16384u : ticks * 4096u / (budget ? budget : 1u);
    meter_q[k] = (meter_q[k] * 15u + x + 8u) / 16u;
}
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
    if (muted)
        return;
    uint32_t t0 = plat_ticks24();
    jungle_render(lr, n, now_ticks() + HEARD_TICKS);
    meter_add(0, plat_ticks24() - t0, n);
    music_render(lr, n);                     /* Facility: the break's mute and level, the tracks beside it */
    {
        struct music_meter m;
        music_meter(&m);
        meter_add(1, m.vox, n);
        meter_add(2, m.fx, n);
    }
    for (i = 0; i < 2u * n; i++)
        lr[i] = mul_shr(lr[i], (int32_t)master_q12, 12);
}

void app_panic(void) { muted = 1; }
void app_overload(void) { music_overload(); }   /* (Amenity+ OPT: the governor) */

/* ------------------------------------------------------------- MIDI clock --- */
/* Clock out: the engine passes each message with its time (audio interrupt), the tick sends it then. */
enum { CLK_Q = 32 };
static struct { uint32_t at; uint8_t status; } clk_q[CLK_Q];
static volatile uint32_t clk_w, clk_r;
void jungle_midi_out(unsigned status, uint32_t at)
{
    uint32_t w = clk_w;
    if (w - clk_r >= CLK_Q)
        return;
    clk_q[w % CLK_Q].at = at;
    clk_q[w % CLK_Q].status = (uint8_t)status;
    __asm__ volatile("" ::: "memory");
    clk_w = w + 1u;
}

/* Clock in: every realtime message, stamped as the tick reads it (the tick runs right after the USB poll). */
static volatile uint8_t clock_ext;
static volatile uint16_t clock_bpm;
void app_tick(void)
{
    uint32_t pkt, now = now_ticks();
    unsigned bpm;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu;
        if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu)
            jungle_midi_realtime(st, (unsigned)port, now);
    }
    clock_ext = (uint8_t)jungle_clock(&bpm, now);
    clock_bpm = (uint16_t)bpm;
    while (clk_r != clk_w) {
        uint32_t r = clk_r;
        if ((int32_t)(now - clk_q[r % CLK_Q].at) < 0)
            break;
        plat_midi_send(0x0Fu | (uint32_t)clk_q[r % CLK_Q].status << 8, TO_USB | TO_BLE);   /* CIN F: one byte */
        clk_r = r + 1u;
    }
}

/* ------------------------------------------------------------------ state --- */
enum { PAGE_MAIN, PAGE_EQ, PAGE_FX, PAGE_FACILITY };
enum { Q_SWEEP = 14, Q_FLAT = 8 };       /* the DJ filter rings only while K4 turns */
static struct {
    unsigned tempo, rhythm, filter, warp, feedback, root, lofi, saturation, compression, brk;
    int transpose, eq_db[4];
    uint8_t playing, frozen, auto_roll, page, fx_used, env_used, external, filter_q;
    uint32_t filter_turned, filter_q_step, clock_near;
} st = {.tempo = 1600, .filter = 64, .root = 4, .filter_q = Q_FLAT};

/* Facility: the BASE (the style, its progression) and four tracks, the break the first. The focus picks what
 * ALGO (style or mode), PRESET (progression or sound) and K1..K4 turn; SEL + ALGO a track's own style */
enum { FOC_BASE, FOCUSES = TRACKS + 1 };
static const char *const TRACK_NAME[TRACKS] = {"BREAK", "BASS", "HARMONY", "MELODY"};
static const char *const BASS_MODE[MBASS_MODES] = {"KICK", "LONG", "RAGGA", "ACID", "WALK", "1 DROP", "RIFF"};
static const char *const HARM_MODE[MHARM_MODES] = {"SKANK", "JAZZ", "MIX", "STABS", "PAD", "MIX+PAD"};
static const char *const STYLE_ABBR[MSTYLES] = {"BRK", "JAZ", "ACD", "HIP", "DUB"};
static const char *const MEL_MODE[MMEL_MODES] = {"SEED", "RIFF", "LINE", "ANSWER"};
static struct {
    uint8_t focus, stress;
    uint8_t level[TRACKS];
    uint8_t style, prog, energy, chords, autodub, form;   /* the BASE (prog: MAUTO or 0..3) */
    uint8_t tstyle[TRACKS];                          /* MSTYLE_BASE or a style */
    uint8_t bass_mode, bass_sound, harm_mode, harm_sound;   /* MAUTO or one */
    uint8_t mel_mode, mel_sound, mel_seed;
    uint8_t param[TRACKS][3];                /* K2..K4 of the bass and the harmony */
    uint8_t swing, human;                    /* F5, the BASE's groove (SEL + K1, K2): MAUTO or 50..75 %, 0..100 */
    uint8_t tswing[TRACKS], accent[TRACKS];  /* a track's (SEL + K1, K2): MAUTO (= BASE) or 50..75; 0..127 */
    /* F6a, Facility's FX view (FX on its page): the BASE's busses, a track's sends (MAUTO: the style's) */
    uint8_t fx, etime, efb, ecolor, rtype, rdecay, fxret, thr;
    uint8_t se[TRACKS], sr[TRACKS], ch[TRACKS], pan[TRACKS];
} fac = {.focus = 1 + TRK_BASS, .level = {100, 100, 100, 100}, .style = MSTYLE_BREAKCORE, .prog = MAUTO,
         .energy = 64, .chords = 64, .autodub = 0,
         .tstyle = {MSTYLE_BASE, MSTYLE_BASE, MSTYLE_BASE, MSTYLE_BASE},
         .bass_mode = MAUTO, .bass_sound = MAUTO, .harm_mode = MAUTO, .harm_sound = MAUTO,
         .mel_mode = MAUTO, .mel_sound = MAUTO, .mel_seed = MAUTO,
         .param = {{64, 64, 64}, {64, 64, 64}, {64, 64, 64}, {64, 0, 0}},
         .swing = MAUTO, .human = MAUTO, .tswing = {50, MAUTO, MAUTO, MAUTO}, .accent = {64, 64, 64, 64},
         .etime = MAUTO, .efb = MAUTO, .ecolor = MAUTO, .rtype = MAUTO, .rdecay = MAUTO, .fxret = MAUTO, .thr = MAUTO,
         .se = {0, MAUTO, MAUTO, MAUTO}, .sr = {0, MAUTO, MAUTO, MAUTO}, .ch = {0, MAUTO, MAUTO, MAUTO}, .pan = {64, 64, 64, 64}};
/* F6a: an effect's knob turned: from AUTO it starts at what the style gives */
static uint8_t fx_turn(uint8_t v, int32_t d, unsigned resolved)
{
    return (uint8_t)clamp((int32_t)(v == MAUTO ? resolved : v) + 2 * d, 0, 127);
}
/* the echo's time (PRESET): AUTO, 1/8 3/16 1/4 3/8 1/2 (0..4), then free 100 .. 1450 ms (10..37) */
static uint8_t etime_turn(uint8_t v, int32_t d)
{
    int i = v == MAUTO ? 0 : v < 5u ? (int)v + 1 : (int)v - 4;
    i = clamp(i + (int)d, 0, 33);
    return (uint8_t)(i == 0 ? (int)MAUTO : i <= 5 ? i - 1 : i + 4);
}
static void text_cat(char *d, const char *s);
static void int_text(int32_t v, char *t);
static unsigned text_len(const char *s) { unsigned n = 0; while (s[n]) n++; return n; }
static void etime_text(uint8_t v, char *t)
{
    static const char *const DIV[5] = {"1/8", "3/16", "1/4", "3/8", "1/2"};
    t[0] = 0;
    if (v < 5u)
        text_cat(t, DIV[v]);
    else {
        int_text(((int)v - 10) * 50 + 100, t);
        text_cat(t, "MS");
    }
}
/* F5: a swing turned (AUTO, 50 .. 75 %) and HUMAN (AUTO, 0 .. 100 by 5): a knob, so no wrap */
static uint8_t swing_turn(uint8_t v, int32_t d)
{
    int i = v == MAUTO ? 0 : (int)v - 49;
    i = clamp(i + (int)d, 0, 26);
    return (uint8_t)(i == 0 ? (int)MAUTO : i + 49);
}
static uint8_t human_turn(uint8_t v, int32_t d)
{
    int i = v == MAUTO ? 0 : (int)v / 5 + 1;
    i = clamp(i + (int)d, 0, 21);
    return (uint8_t)(i == 0 ? (int)MAUTO : (i - 1) * 5);
}
/* a choice with AUTO first: MAUTO, 0, 1 .. n-1, turned d */
static uint8_t auto_turn(uint8_t v, int32_t d, unsigned n)
{
    int i = v == MAUTO ? 0 : v + 1;
    i = (i + (d > 0 ? 1 : -1) + (int)n + 1) % ((int)n + 1);
    return (uint8_t)(i == 0 ? (int)MAUTO : i - 1);
}

static const uint8_t WHITE[16] = PLAT_WHITE_KEYS, BLACK[11] = PLAT_BLACK_KEYS;
static const char *const ROLL_NAME[16] = {
    "1/4 REPEAT", "BUILD", "STRETCH", "ECHO", "1/8 REPEAT", "1/8 DOWN", "1/8 UP", "1/8 SHRINK",
    "1/16 REPEAT", "1/16 DOWN", "1/16 UP", "1/16 SHRINK", "1/32 REPEAT", "1/32 DOWN", "1/32 UP", "1/32 SHRINK"};
static const char *const INSTANT_NAME[4] = {"FEEDBACK 75%", "LO-FI MAX", "OCTAVE DOWN", "FILTER SWEEP"};
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}
static void int_text(int32_t v, char *b)     /* signed, no padding */
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}
static void tenths_text(unsigned v, char *b)  /* 1725 -> "172.5" */
{
    char t[8];
    int_text((int32_t)(v / 10u), b);
    t[0] = '.';
    t[1] = (char)('0' + v % 10u);
    t[2] = 0;
    text_cat(b, t);
}

/* ---------------------------------------------------------------- messages --- */
static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    uint8_t held[16], held_n;
    uint8_t redraw;
    /* the little flashes when something starts (ANIM_MS each): the screen glowing in its colour, the state line's
     * name popping out of a block, a flip's ripple, a roll's shake, a warped step's move */
    uint32_t spark_t, pop_t, flip_t, glitch_t, warp_t;
    uint16_t spark_c, spark_lv;              /* spark_lv: how strong the screen's glow, /256 */
    uint8_t flip_bar, warp_kind, warp_step;  /* warp_kind: 1 a stretch step stretching, 2 an octave-down step sinking */
} ui;
#define ANIM_MS 260u
#define POP_MS 160u

void app_say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
}

/* ------------------------------------------------------------------- init --- */
static unsigned breaks_count(void);
static const struct jungle_break *break_at(unsigned i);
/* the break PRESET has picked: it plays from the next hit */
static void select_break(unsigned i)
{
    const struct jungle_break *b = break_at(i);
    if (!b)
        return;
    st.brk = i;
    (void)jungle_bind(b->data, b->bytes, b->frames);
    music_snares((1u << b->shots[1]) | (1u << b->shots[3]) | (1u << b->shots[4]) | (1u << b->shots[5]));   /* (kick snare kick snare snare snare hat ride) */
    ui.redraw = 1;
}

/* ------------------------------------------------------------------ slots --- */
/* F3: 16 slots, a whole scene each: Amenity's settings, the BASE, the four tracks (style, mode, sound, level,
 * knobs), the mutes, the tempo and the seed (the same music again). In the store object OBJ_SETTINGS with the
 * breaks page's order (user_breaks.c): one write for both. A save while playing waits for the next STOP (a flash
 * write silences the FM-1 for a moment); a load from the next 4-bar block (Marote, 18 nov). */
enum { SLOTS = 16, SLOT_VERSION = 4 };   /* 2: the melody (F4); 3: the groove (F5); 4: the effects (F6a) */
#define SLOTS_MAGIC 0x31534C46u                /* "FLS1" */
struct slot {
    uint8_t used, ver;
    uint16_t tempo;
    uint8_t brk, rhythm, warp, filter, feedback, root, lofi, saturation, compression, auto_roll, filter_q;
    int8_t transpose, eq_db[4];
    uint8_t style, prog, energy, chords, autodub, form;
    uint8_t tstyle[TRACKS], bass_mode, bass_sound, harm_mode, harm_sound;
    uint8_t level[TRACKS], param[TRACKS][3], mutes;
    uint32_t seed;
    uint8_t mel_mode, mel_sound, mel_seed, rsv;   /* v2 */
    uint8_t swing, human, tswing[TRACKS], accent[TRACKS];   /* v3 */
    uint8_t etime, efb, ecolor, rtype, rdecay, fxret, thr, rsv4;   /* v4 */
    uint8_t se[TRACKS], sr[TRACKS], ch[TRACKS], pan[TRACKS];
};
static struct slot slots[SLOTS];
static struct {
    int8_t pending, current;                 /* a load waiting for its block, the last one loaded or saved */
    uint8_t vary, dirty, stage;              /* stage 1: the music asked, 2: the rest at the block */
    uint32_t block;
} sl = {-1, -1, 0, 0, 0, 0};
static int settings_save(void);
static void settings_load(void);
#include "user_breaks.c"

/* the store: the breaks page's order, then the slots (a store from before them: the order alone) */
static struct {
    uint32_t magic;
    uint16_t ver, slot_size;
    struct slot s[SLOTS];
} slot_store;
static int settings_save(void)
{
    uint8_t buf[sizeof user_order + sizeof slot_store];
    slot_store.magic = SLOTS_MAGIC;
    slot_store.ver = SLOT_VERSION;
    slot_store.slot_size = (uint16_t)sizeof(struct slot);
    memcpy(slot_store.s, slots, sizeof slots);
    memcpy(buf, &user_order, sizeof user_order);
    memcpy(buf + sizeof user_order, &slot_store, sizeof slot_store);
    sl.dirty = 0;
    return plat_store_save(OBJ_SETTINGS, buf, sizeof buf);
}
static void settings_load(void)
{
    static uint8_t buf[sizeof user_order + sizeof slot_store];
    int n = plat_store_load(OBJ_SETTINGS, buf, sizeof buf);
    memset(slots, 0, sizeof slots);
    if (n < (int)sizeof user_order)
        return;
    memcpy(&user_order, buf, sizeof user_order);
    if (n >= (int)(sizeof user_order + 8u)) {
        const uint8_t *in = buf + sizeof user_order;
        uint32_t magic;
        uint16_t size;
        memcpy(&magic, in, 4);
        memcpy(&size, in + 6, 2);
        if (magic == SLOTS_MAGIC && size && size <= sizeof(struct slot) &&
            (unsigned)n >= sizeof user_order + 8u + SLOTS * (unsigned)size) {
            unsigned i;
            for (i = 0; i < SLOTS; i++) {        /* a slot of an older, shorter kind: what it has, the rest AUTO */
                memcpy(&slots[i], in + 8 + i * size, size);
                if (size <= offsetof(struct slot, mel_mode) && slots[i].used) {
                    slots[i].mel_mode = slots[i].mel_sound = slots[i].mel_seed = MAUTO;
                    slots[i].param[TRK_MELODY][0] = 64, slots[i].param[TRK_MELODY][1] = 0, slots[i].param[TRK_MELODY][2] = 0;
                }
                if (size <= offsetof(struct slot, swing) && slots[i].used) {   /* before F5: the styles' groove */
                    unsigned t;
                    slots[i].swing = slots[i].human = MAUTO;
                    for (t = 0; t < TRACKS; t++)
                        slots[i].tswing[t] = t == TRK_BREAK ? 50 : MAUTO, slots[i].accent[t] = 64;
                }
                if (size <= offsetof(struct slot, etime) && slots[i].used) {   /* before F6a: the styles' effects */
                    unsigned t;
                    slots[i].etime = slots[i].efb = slots[i].ecolor = slots[i].rtype = MAUTO;
                    slots[i].rdecay = slots[i].fxret = slots[i].thr = MAUTO;
                    for (t = 0; t < TRACKS; t++) {
                        slots[i].se[t] = slots[i].sr[t] = slots[i].ch[t] = t == TRK_BREAK ? 0 : MAUTO;
                        slots[i].pan[t] = 64;
                    }
                }
            }
        }
    }
}

void app_init(void)
{
    jungle_init();
    music_init();
    music_clock(plat_ticks24);               /* the meter's clock (Amenity+ OPT) */
    for (unsigned t = 0; t < TRACKS; t++)
        for (unsigned k = 0; k < 3u; k++)
            music_param(t, k, fac.param[t][k]);
    order_load();
    user_scan();
    select_break(0);
    jungle_tempo(st.tempo);
    jungle_variation(st.rhythm);
    jungle_filter(st.filter);
    jungle_filter_resonance(st.filter_q);
    jungle_warp(st.warp);
    jungle_feedback(st.feedback);
    jungle_feedback_note(st.root);
    music_root(st.root);
    jungle_lofi(st.lofi);
    jungle_pitch(st.transpose);
}

static void set_tempo(int bpm_x10);
static void slot_capture(struct slot *x)
{
    unsigned t, k;
    memset(x, 0, sizeof *x);
    x->used = 1;
    x->ver = SLOT_VERSION;
    x->tempo = (uint16_t)st.tempo;
    x->brk = (uint8_t)st.brk;
    x->rhythm = (uint8_t)st.rhythm;
    x->warp = (uint8_t)st.warp;
    x->filter = (uint8_t)st.filter;
    x->feedback = (uint8_t)st.feedback;
    x->root = (uint8_t)st.root;
    x->lofi = (uint8_t)st.lofi;
    x->saturation = (uint8_t)st.saturation;
    x->compression = (uint8_t)st.compression;
    x->auto_roll = st.auto_roll;
    x->filter_q = st.filter_q;
    x->transpose = (int8_t)st.transpose;
    for (k = 0; k < 4u; k++)
        x->eq_db[k] = (int8_t)st.eq_db[k];
    x->style = fac.style;
    x->prog = fac.prog;
    x->energy = fac.energy;
    x->chords = fac.chords;
    x->autodub = fac.autodub;
    x->form = fac.form;
    x->bass_mode = fac.bass_mode;
    x->bass_sound = fac.bass_sound;
    x->harm_mode = fac.harm_mode;
    x->harm_sound = fac.harm_sound;
    for (t = 0; t < TRACKS; t++) {
        int muted = music_mute_pending(t) ? !music_muted(t) : music_muted(t);   /* as asked */
        x->tstyle[t] = fac.tstyle[t];
        x->level[t] = fac.level[t];
        for (k = 0; k < 3u; k++)
            x->param[t][k] = fac.param[t][k];
        if (muted)
            x->mutes |= (uint8_t)(1u << t);
    }
    x->seed = music_song_seed();
    x->mel_mode = fac.mel_mode;
    x->mel_sound = fac.mel_sound;
    x->mel_seed = fac.mel_seed;
    x->swing = fac.swing;
    x->human = fac.human;
    for (t = 0; t < TRACKS; t++)
        x->tswing[t] = fac.tswing[t], x->accent[t] = fac.accent[t];
    x->etime = fac.etime, x->efb = fac.efb, x->ecolor = fac.ecolor, x->rtype = fac.rtype;
    x->rdecay = fac.rdecay, x->fxret = fac.fxret, x->thr = fac.thr;
    for (t = 0; t < TRACKS; t++)
        x->se[t] = fac.se[t], x->sr[t] = fac.sr[t], x->ch[t] = fac.ch[t], x->pan[t] = fac.pan[t];
}
/* a slot in two steps, so it all lands on the block: the music's part (its style, its tracks, the mutes at the
 * phrase, the seed) a 16th before, the break's (the tempo, the break, its knobs) as the block starts */
static void slot_apply_music(const struct slot *x, int vary)
{
    unsigned t, k;
    music_style(fac.style = x->style);
    music_prog(fac.prog = x->prog);
    music_energy(fac.energy = x->energy);
    music_chord_speed(fac.chords = x->chords);
    music_autodub(fac.autodub = x->autodub);
    music_form(fac.form = x->form);
    music_bass_mode(fac.bass_mode = x->bass_mode);
    music_bass_timbre(fac.bass_sound = x->bass_sound);
    music_harm_mode(fac.harm_mode = x->harm_mode);
    music_harm_sound(fac.harm_sound = x->harm_sound);
    music_mel_mode(fac.mel_mode = x->mel_mode);
    music_mel_sound(fac.mel_sound = x->mel_sound);
    music_mel_seed(fac.mel_seed = x->mel_seed);
    for (t = 0; t < TRACKS; t++) {
        music_track_style(t, fac.tstyle[t] = x->tstyle[t]);
        music_track_level(t, fac.level[t] = x->level[t]);
        for (k = 0; k < 3u; k++)
            music_param(t, k, fac.param[t][k] = x->param[t][k]);
        music_mute(t, (int)(x->mutes >> t & 1u), jungle_playing());
        music_track_swing(t, fac.tswing[t] = x->tswing[t]);
        music_accent(t, fac.accent[t] = x->accent[t]);
    }
    music_swing(fac.swing = x->swing);
    music_human(fac.human = x->human);
    music_fx(MFX_ETIME, fac.etime = x->etime);
    music_fx(MFX_EFB, fac.efb = x->efb);
    music_fx(MFX_ECOLOR, fac.ecolor = x->ecolor);
    music_fx(MFX_RTYPE, fac.rtype = x->rtype);
    music_fx(MFX_RDECAY, fac.rdecay = x->rdecay);
    music_fx(MFX_RETURN, fac.fxret = x->fxret);
    music_fx(MFX_THROW, fac.thr = x->thr);
    for (t = 0; t < TRACKS; t++) {
        music_send(t, MSEND_ECHO, fac.se[t] = x->se[t]);
        music_send(t, MSEND_REVERB, fac.sr[t] = x->sr[t]);
        music_send(t, MSEND_CHORUS, fac.ch[t] = x->ch[t]);
        music_send(t, MSEND_PAN, fac.pan[t] = x->pan[t]);
    }
    {
        uint32_t seed = vary ? (x->seed * 2654435761u) ^ plat_ms() : x->seed;
        music_seed(seed);
        jungle_seed(seed);
    }
}
static void slot_apply_break(const struct slot *x)
{
    unsigned k;
    set_tempo((int)x->tempo);
    if ((unsigned)x->brk < breaks_count())
        select_break(x->brk);
    jungle_variation(st.rhythm = x->rhythm);
    jungle_warp(st.warp = x->warp);
    jungle_filter(st.filter = x->filter);
    jungle_filter_resonance(st.filter_q = x->filter_q);
    jungle_feedback(st.feedback = x->feedback);
    st.root = x->root % 12u;
    jungle_feedback_note(st.root);
    music_root(st.root);
    jungle_lofi(st.lofi = x->lofi);
    jungle_saturation(st.saturation = x->saturation);
    jungle_compression(st.compression = x->compression);
    jungle_pitch(st.transpose = x->transpose);
    for (k = 0; k < 4u; k++)
        jungle_eq(k, st.eq_db[k] = x->eq_db[k]);
    jungle_auto_roll(st.auto_roll = x->auto_roll);
    ui.redraw = 1;
}
static void slot_key(unsigned i, uint32_t btn)   /* ENV held + white key i: load; + SEL: vary; + EDIT: save */
{
    char n[4];
    int_text((int32_t)i + 1, n);
    if (btn >> B_EDIT & 1u) {
        slot_capture(&slots[i]);
        sl.current = (int8_t)i;
        sl.dirty = 1;
        if (!jungle_playing())
            settings_save();
        app_say(jungle_playing() ? "SAVED AT STOP" : "SAVED", n);
        return;
    }
    if (!slots[i].used) {
        app_say("EMPTY SLOT", n);
        return;
    }
    sl.vary = (uint8_t)(btn >> B_SEL & 1u);
    if (!jungle_playing()) {                 /* stopped: all of it now */
        slot_apply_music(&slots[i], sl.vary);
        slot_apply_break(&slots[i]);
        sl.current = (int8_t)i;
        sl.pending = -1;
    } else {
        sl.pending = (int8_t)i;
        sl.stage = 0;
    }
    app_say(sl.vary ? "LOAD VARIED" : "LOAD", n);
}
static void slots_frame(void)   /* the main loop: a load's two steps, a save's write at STOP */
{
    if (sl.pending >= 0) {
        const struct slot *x = &slots[sl.pending];
        if (sl.stage == 0 && music_block_left() <= 1u) {
            slot_apply_music(x, sl.vary);
            sl.stage = 1;
            sl.block = music_blocks();
        } else if (sl.stage == 1 && music_blocks() != sl.block) {
            slot_apply_break(x);
            sl.current = sl.pending;
            sl.pending = -1;
        }
    }
    if (sl.dirty && !jungle_playing())
        settings_save();
}

/* ------------------------------------------------------------------ input --- */
static void set_tempo(int bpm_x10)
{
    st.tempo = (unsigned)clamp(bpm_x10, JUNGLE_BPM_MIN_X10, JUNGLE_BPM_MAX_X10);
    jungle_tempo(st.tempo);
    ui.redraw = 1;
}

/* halve / double: from the next beat (shown amber until then) */
static void tempo_next(int bpm_x10)
{
    st.tempo = (unsigned)clamp(bpm_x10, JUNGLE_BPM_MIN_X10, JUNGLE_BPM_MAX_X10);
    jungle_tempo_next(st.tempo);
    ui.redraw = 1;
}

static void keys_input(uint32_t keys, uint32_t btn)
{
    uint32_t down = keys & ~ui.keys, up = ui.keys & ~keys, i;
    if (btn >> B_FX & 1u) {                  /* FX held: a key sets the feedback's root */
        for (i = 0; i < NKEYS; i++)
            if (down >> i & 1u) {
                st.root = (5u + i) % 12u;    /* key 0 is F3 */
                jungle_feedback_note(st.root);
                music_root(st.root);                 /* Amenity+: the music's key, from the next phrase */
                app_say("FEEDBACK ROOT", NOTE_NAME[st.root]);
            }
        if (down)
            st.fx_used = 1;                  /* FX was a modifier, not a tap */
        down = 0;
    }
    if (btn >> B_ENV & 1u) {                 /* ENV held: a white key loads its slot (SEL: varied; EDIT: saves) */
        for (i = 0; i < 16u; i++)
            if (down >> WHITE[i] & 1u)
                slot_key(i, btn);
        if (down)
            st.env_used = 1;
        down = 0;
    }
    for (i = 0; i < 16u; i++) {              /* rolls: the last white key held wins, a release hands back */
        uint32_t bit = 1u << WHITE[i], k, n = 0;
        if (up & bit) {
            for (k = 0; k < ui.held_n; k++)
                if (ui.held[k] != i)
                    ui.held[n++] = ui.held[k];
            ui.held_n = (uint8_t)n;
        }
        if ((down & bit) && ui.held_n < 16u)
            ui.held[ui.held_n++] = (uint8_t)i;
    }
    if (down | up) {
        if (ui.held_n)
            jungle_roll(ui.held[ui.held_n - 1u]);
        else
            jungle_roll_off();
    }
    {                                        /* one-shots on the first eight black keys: the break's own */
        const struct jungle_break *b = break_at(st.brk);
        for (i = 0; b && i < 8u; i++)
            if (down >> BLACK[i] & 1u)
                jungle_one_shot(b->shots[i]);
    }
    {                                        /* the top three black keys: instant effects while held */
        uint32_t mask = 0;
        if (!(btn >> B_FX & 1u) && !(btn >> B_ENV & 1u))
            for (i = 0; i < 3u; i++)
                if (keys >> BLACK[8u + i] & 1u)
                    mask |= 1u << i;
        jungle_instant(mask);
    }
}

static void mute_toggle(unsigned t, uint32_t btn)
{
    int quant = (int)(btn >> B_SEL & 1u), now_muted = music_mute_pending(t) ? !music_muted(t) : music_muted(t);
    music_mute(t, !now_muted, quant);
    app_say(TRACK_NAME[t], now_muted ? (quant ? "IN NEXT PHRASE" : "IN") : (quant ? "OUT NEXT PHRASE" : "OUT"));
}

static void buttons_input(uint32_t btn, uint32_t now)
{
    uint32_t pressed = btn & ~ui.btn, released = ui.btn & ~btn;
#define PRESSED(b) (pressed >> (b) & 1u)
    if (PRESSED(B_FX))
        st.fx_used = 0;
    if ((released >> B_FX & 1u) && !st.fx_used) {   /* on Facility's page: its sends (F6a); else the break's effects */
        if (st.page == PAGE_FACILITY)
            fac.fx = !fac.fx;
        else
            st.page = st.page == PAGE_FX ? PAGE_MAIN : PAGE_FX;
    }
    if (PRESSED(B_ENV))
        st.env_used = 0;
    if ((released >> B_ENV & 1u) && !st.env_used)   /* a tap: the EQ page; held with a white key: the slots */
        st.page = st.page == PAGE_EQ ? PAGE_MAIN : PAGE_EQ;
    if (PRESSED(B_PLAY)) {
        if (st.external)
            app_say("EXTERNAL", "MIDI CLOCK");
        else {
            st.playing = !st.playing;
            if (st.playing)
                jungle_seed(now);
            jungle_play(st.playing);
        }
    }
    if (PRESSED(B_REC))
        jungle_freeze(st.frozen = !st.frozen);
    /* Facility: GLO between the break's page and Facility's (with EDIT held: the CPU test, every voice held);
     * the four tracks' mutes on either page, in their order: HOME the break, SAVE the bass, ARP the harmony, SEQ the
     * melody; a tap at once, with SEL held from the next phrase (the 2 bars' end). LFO: the automatic fills */
    if (PRESSED(B_GLO)) {
        if (btn >> B_EDIT & 1u) {
            music_stress(fac.stress = !fac.stress);
            app_say("CPU TEST", fac.stress ? "ON" : "OFF");
        } else {
            st.page = st.page == PAGE_FACILITY ? PAGE_MAIN : PAGE_FACILITY;   /* the header says which (red: FACILITY) */
            ui.msg_until = 0;
        }
    }
    {
        static const uint8_t MUTE_BTN[TRACKS] = {B_HOME, B_SAVE, B_ARP, B_SEQ};
        unsigned t;
        for (t = 0; t < TRACKS; t++)
            if (PRESSED(MUTE_BTN[t]) && !(t == TRK_BREAK && (btn >> B_EDIT & 1u)))
                mute_toggle(t, btn);
    }
    if (PRESSED(B_LFO))
        jungle_auto_roll(st.auto_roll = !st.auto_roll);
    if (PRESSED(B_HOME) && (btn >> B_EDIT & 1u)) {   /* EDIT + HOME, the plain break: no variation or warp until K2 / K3 turn again */
        st.rhythm = st.warp = 0;
        st.page = PAGE_MAIN;
        jungle_variation(0);
        jungle_warp(0);
        jungle_straight();
        app_say("STRAIGHT", "BREAK");
    }
    /* halve / double from the next beat, only when it stays in range (else it would not be half or double);
     * with SEL held, 0.1 BPM down / up */
    if (PRESSED(B_OCTDN) && !st.external) {
        if (btn >> B_SEL & 1u)
            set_tempo((int)st.tempo - 1);
        else if (st.tempo / 2u >= JUNGLE_BPM_MIN_X10)
            tempo_next((int)st.tempo / 2);
        else
            app_say("MIN", "40 BPM");
    }
    if (PRESSED(B_OCTUP) && !st.external) {
        if (btn >> B_SEL & 1u)
            set_tempo((int)st.tempo + 1);
        else if (st.tempo * 2u <= JUNGLE_BPM_MAX_X10)
            tempo_next((int)st.tempo * 2);
        else
            app_say("MAX", "320 BPM");
    }
#undef PRESSED
    if (pressed | released)
        ui.redraw = 1;
}


static unsigned turn(unsigned v, int32_t detents) { return (unsigned)clamp((int32_t)v + 2 * detents, 0, 127); }
static int wrap(int v, int d, int n) { v = (v + d) % n; return v < 0 ? v + n : v; }

/* F6a, Facility's FX view: on the BASE, ALGO the reverb's type, PRESET the echo's time, K1 the echo's feedback, K2
 * its colour, K3 the reverb's decay, K4 the returns; on a track, K1 its echo send, K2 its reverb send, K3 its chorus
 * (the break: its throws), K4 its pan. SEL + a knob: back to AUTO */
static void facility_fx_encoders(const int32_t *e, int sel)
{
    struct music_view mv;
    unsigned f;
    music_view(&mv);
    if (fac.focus == FOC_BASE) {
        if (e[EN_ALGO]) {
            static const char *const RT[2] = {"ROOM", "SPRING"};
            music_fx(MFX_RTYPE, fac.rtype = auto_turn(fac.rtype, e[EN_ALGO], 2));
            app_say("REVERB", fac.rtype == MAUTO ? "AUTO" : RT[fac.rtype]);
        }
        if (e[EN_PRESET]) {
            char t[12];
            music_fx(MFX_ETIME, fac.etime = sel ? MAUTO : etime_turn(fac.etime, e[EN_PRESET]));
            etime_text(fac.etime == MAUTO ? mv.etime : fac.etime, t);
            app_say(fac.etime == MAUTO ? "ECHO TIME AUTO" : "ECHO TIME", t);
        }
        if (e[EN_K1])
            music_fx(MFX_EFB, fac.efb = sel ? MAUTO : fx_turn(fac.efb, e[EN_K1], mv.efb));
        if (e[EN_K2])
            music_fx(MFX_ECOLOR, fac.ecolor = sel ? MAUTO : fx_turn(fac.ecolor, e[EN_K2], mv.ecolor));
        if (e[EN_K3])
            music_fx(MFX_RDECAY, fac.rdecay = sel ? MAUTO : fx_turn(fac.rdecay, e[EN_K3], mv.rdecay));
        if (e[EN_K4])
            music_fx(MFX_RETURN, fac.fxret = sel ? MAUTO : fx_turn(fac.fxret, e[EN_K4], mv.fxret));
        return;
    }
    f = fac.focus - 1u;
    if (e[EN_K1])
        music_send(f, MSEND_ECHO, fac.se[f] = sel ? (f == TRK_BREAK ? 0 : MAUTO) : fx_turn(fac.se[f], e[EN_K1], mv.se[f]));
    if (e[EN_K2])
        music_send(f, MSEND_REVERB, fac.sr[f] = sel ? (f == TRK_BREAK ? 0 : MAUTO) : fx_turn(fac.sr[f], e[EN_K2], mv.sr[f]));
    if (e[EN_K3]) {
        if (f == TRK_BREAK)
            music_fx(MFX_THROW, fac.thr = sel ? MAUTO : fx_turn(fac.thr, e[EN_K3], mv.thr));
        else
            music_send(f, MSEND_CHORUS, fac.ch[f] = sel ? MAUTO : fx_turn(fac.ch[f], e[EN_K3], mv.ch[f]));
    }
    if (e[EN_K4] && f != TRK_BREAK)
        music_send(f, MSEND_PAN, fac.pan[f] = sel ? 64 : (uint8_t)turn(fac.pan[f], e[EN_K4]));
}

/* Facility's page: SELECT the focus, ALGO its mode, PRESET its sound, K1 its level; K2..K4 the break's variation,
 * warp and filter (as on the break's page), or the bass's / the harmony's sound */
static void facility_encoders(const int32_t *e, uint32_t now, uint32_t btn)
{
    unsigned k;
    int f, sel = (int)(btn >> B_SEL & 1u);
    if (e[EN_SELECT]) {
        fac.focus = (uint8_t)wrap(fac.focus, e[EN_SELECT] > 0 ? 1 : -1, FOCUSES);
        app_say("FOCUS", fac.focus == FOC_BASE ? "BASE" : TRACK_NAME[fac.focus - 1]);
    }
    if (fac.fx) {
        facility_fx_encoders(e, sel);
        return;
    }
    if (sel && (e[EN_K1] || e[EN_K2])) {    /* SEL + K1 / K2: the groove (F5); the knobs' own jobs wait */
        if (fac.focus == FOC_BASE) {
            if (e[EN_K1])
                music_swing(fac.swing = swing_turn(fac.swing, e[EN_K1]));
            if (e[EN_K2])
                music_human(fac.human = human_turn(fac.human, e[EN_K2]));
        } else {
            f = fac.focus - 1;
            if (e[EN_K1])
                music_track_swing((unsigned)f, fac.tswing[f] = swing_turn(fac.tswing[f], e[EN_K1]));
            if (e[EN_K2] && f != TRK_BREAK)
                music_accent((unsigned)f, fac.accent[f] = (uint8_t)turn(fac.accent[f], e[EN_K2]));
        }
        return;
    }
    if (fac.focus == FOC_BASE) {             /* the BASE: ALGO the style, PRESET the progression, K1..K3 */
        if (e[EN_ALGO]) {
            music_style(fac.style = (uint8_t)wrap(fac.style, e[EN_ALGO] > 0 ? 1 : -1, MSTYLES));
            app_say("STYLE", music_style_name(fac.style));
        }
        if (e[EN_PRESET]) {
            music_prog(fac.prog = auto_turn(fac.prog, e[EN_PRESET], 4));
            app_say("PROGRESSION", fac.prog == MAUTO ? "AUTO" : music_prog_name(fac.style, fac.prog));
        }
        if (e[EN_K1])
            music_energy(fac.energy = (uint8_t)turn(fac.energy, e[EN_K1]));
        if (e[EN_K2])
            music_chord_speed(fac.chords = (uint8_t)turn(fac.chords, e[EN_K2]));
        if (e[EN_K3])
            music_autodub(fac.autodub = (uint8_t)turn(fac.autodub, e[EN_K3]));
        if (e[EN_K4])
            music_form(fac.form = (uint8_t)turn(fac.form, e[EN_K4]));
        return;
    }
    f = fac.focus - 1;
    if (e[EN_ALGO] && sel && f != TRK_BREAK) {   /* SEL + ALGO: the track's own style */
        uint8_t ts = fac.tstyle[f];
        ts = auto_turn(ts, e[EN_ALGO], MSTYLES);  /* (MSTYLE_BASE == MAUTO: BASE first) */
        music_track_style((unsigned)f, fac.tstyle[f] = ts);
        app_say("TRACK STYLE", ts == MSTYLE_BASE ? "= BASE" : music_style_name(ts));
    } else if (e[EN_ALGO]) {
        if (f == TRK_BASS) {
            music_bass_mode(fac.bass_mode = auto_turn(fac.bass_mode, e[EN_ALGO], MBASS_MODES));
            app_say("BASS MODE", fac.bass_mode == MAUTO ? "AUTO" : BASS_MODE[fac.bass_mode]);
        } else if (f == TRK_HARMONY) {
            music_harm_mode(fac.harm_mode = auto_turn(fac.harm_mode, e[EN_ALGO], MHARM_MODES));
            app_say("HARMONY MODE", fac.harm_mode == MAUTO ? "AUTO" : HARM_MODE[fac.harm_mode]);
        } else if (f == TRK_MELODY) {
            music_mel_mode(fac.mel_mode = auto_turn(fac.mel_mode, e[EN_ALGO], MMEL_MODES));
            app_say("MELODY MODE", fac.mel_mode == MAUTO ? "AUTO" : MEL_MODE[fac.mel_mode]);
        }
    }
    if (e[EN_PRESET] && sel && f == TRK_MELODY) {   /* SEL + PRESET: the melody's seed */
        music_mel_seed(fac.mel_seed = auto_turn(fac.mel_seed, e[EN_PRESET], music_seeds()));
        app_say("SEED", fac.mel_seed == MAUTO ? "AUTO" : music_seed_name(fac.mel_seed));
    } else if (e[EN_PRESET]) {
        if (f == TRK_BASS) {
            music_bass_timbre(fac.bass_sound = auto_turn(fac.bass_sound, e[EN_PRESET], MTIMBRES));
            app_say("BASS SOUND", fac.bass_sound == MAUTO ? "AUTO" : music_bass_sound_name(fac.bass_sound));
        } else if (f == TRK_HARMONY) {
            music_harm_sound(fac.harm_sound = auto_turn(fac.harm_sound, e[EN_PRESET], MHSOUNDS));
            app_say("HARMONY SOUND", fac.harm_sound == MAUTO ? "AUTO" : music_harm_sound_name(fac.harm_sound));
        } else if (f == TRK_MELODY) {
            music_mel_sound(fac.mel_sound = auto_turn(fac.mel_sound, e[EN_PRESET], MMSOUNDS));
            app_say("MELODY SOUND", fac.mel_sound == MAUTO ? "AUTO" : music_mel_sound_name(fac.mel_sound));
        } else if (f == TRK_BREAK && breaks_count()) {
            int n = (int)breaks_count();
            select_break((unsigned)wrap((int)st.brk, e[EN_PRESET] > 0 ? 1 : -1, n));
            app_say("BREAK", break_at(st.brk)->name);
        }
    }
    if (e[EN_K1])
        music_track_level((unsigned)f, fac.level[f] = (uint8_t)clamp(fac.level[f] + 2 * e[EN_K1], 0, 127));
    if (f == TRK_BREAK) {                    /* the break's knobs, as on its page */
        if (e[EN_K2])
            jungle_variation(st.rhythm = turn(st.rhythm, e[EN_K2]));
        if (e[EN_K3])
            jungle_warp(st.warp = turn(st.warp, e[EN_K3]));
        if (e[EN_K4]) {
            int next = (int)turn(st.filter, e[EN_K4]);
            if (((int)st.filter < 64 && next > 64) || ((int)st.filter > 64 && next < 64))
                next = 64;
            jungle_filter(st.filter = (unsigned)next);
            st.filter_turned = now;
            if (st.filter_q != Q_SWEEP)
                jungle_filter_resonance(st.filter_q = Q_SWEEP);
        }
    } else
        for (k = 0; k < 3u; k++)
            if (e[EN_K2 + k])
                music_param((unsigned)f, k, fac.param[f][k] = (uint8_t)turn(fac.param[f][k], e[EN_K2 + k]));
}

static void encoders_input(uint32_t btn, uint32_t now)
{
    int32_t e[NE], k;
    for (k = 0; k < NE; k++)
        if ((e[k] = plat_enc(k)) != 0)
            ui.redraw = 1;
    if (st.page == PAGE_FACILITY) {
        facility_encoders(e, now, btn);
        return;
    }
    if (e[EN_SELECT] && !st.external)        /* SELECT: the tempo, 1 BPM a detent (2 turned fast), 0.1 with SEL */
        set_tempo((int)st.tempo + e[EN_SELECT] * ((btn >> B_SEL & 1u) ? 1 : e[EN_SELECT] > 2 || e[EN_SELECT] < -2 ? 20 : 10));
    if (e[EN_PRESET] && breaks_count()) {        /* PRESET: the next or the previous break, round */
        int n = (int)breaks_count(), next = ((int)st.brk + e[EN_PRESET]) % n;
        select_break((unsigned)(next < 0 ? next + n : next));
        app_say("BREAK", break_at(st.brk)->name);
    }
    if (st.page == PAGE_EQ) {
        for (k = 0; k < 4; k++)
            if (e[EN_K1 + k]) {
                st.eq_db[k] = clamp(st.eq_db[k] + e[EN_K1 + k], -12, 12);
                jungle_eq((unsigned)k, st.eq_db[k]);
            }
    } else if (st.page == PAGE_FX) {
        if (e[EN_K1])
            jungle_saturation(st.saturation = turn(st.saturation, e[EN_K1]));
        if (e[EN_K2])
            jungle_compression(st.compression = turn(st.compression, e[EN_K2]));
        if (e[EN_K3])
            jungle_lofi(st.lofi = turn(st.lofi, e[EN_K3]));
    } else {
        if (e[EN_K1])
            jungle_feedback(st.feedback = turn(st.feedback, e[EN_K1]));
        if (e[EN_K2])
            jungle_variation(st.rhythm = turn(st.rhythm, e[EN_K2]));
        if (e[EN_K3])
            jungle_warp(st.warp = turn(st.warp, e[EN_K3]));
        if (e[EN_K4]) {                      /* crossing the middle stops at open first */
            int next = (int)turn(st.filter, e[EN_K4]);
            if (((int)st.filter < 64 && next > 64) || ((int)st.filter > 64 && next < 64))
                next = 64;
            jungle_filter(st.filter = (unsigned)next);
            st.filter_turned = now;
            if (st.filter_q != Q_SWEEP)
                jungle_filter_resonance(st.filter_q = Q_SWEEP);
        }
    }
    if (e[EN_ALGO]) {                        /* down only: pitched up sounded thin */
        st.transpose = clamp(st.transpose + e[EN_ALGO], -12, 0);
        jungle_pitch(st.transpose);
    }
}

/* MIDI clock: while it leads, PLAY rests and the tempo shown is the clock's (a 0.1 BPM difference only once it has
 * lasted a second, so it does not flicker) */
static void clock_follow(uint32_t now)
{
    unsigned locked = clock_bpm;
    if (clock_ext != st.external) {
        st.external = clock_ext;
        ui.redraw = 1;
    }
    if (!st.external)
        return;
    if (st.playing)
        jungle_play(st.playing = 0);
    if (locked + 1u >= st.tempo && locked <= st.tempo + 1u) {
        if (locked == st.tempo)
            st.clock_near = now;
        else if (now - st.clock_near >= 1000u)
            set_tempo((int)locked);
    } else {
        set_tempo((int)locked);
        st.clock_near = now;
    }
}

/* Lights: lit (100%), what you hold and the running light (one white key an 8th, where the pattern is); at
 * felucca-bsp's mid level (about 12%), what plays by itself: the slice sounding on its white key and on the black
 * keys whose one-shot it is, a roll and the instant effects from a fill. (The panel has these two levels and a glow: no 75%.) */
static void lights(void)
{
    uint32_t btn = 0, keys = ui.keys, mid = 0, i;
    int step = jungle_current_step(), slice = jungle_current_slice(), roll = jungle_roll_active();
    if (jungle_playing())
        btn |= 1u << B_PLAY;
    if (st.frozen)
        btn |= 1u << B_REC;
    if (st.auto_roll)
        btn |= 1u << B_LFO;
    if (st.page == PAGE_FX || (st.page == PAGE_FACILITY && fac.fx))
        btn |= 1u << B_FX;
    if (st.page == PAGE_EQ)
        btn |= 1u << B_ENV;
    if (st.page == PAGE_FACILITY)
        btn |= 1u << B_GLO;
    {                                        /* Facility: a track's button lit while it sounds, blinking while it waits */
        static const uint8_t MUTE_BTN[TRACKS] = {B_HOME, B_SAVE, B_ARP, B_SEQ};
        unsigned t, blink = plat_ms() / 150u & 1u;
        for (t = 0; t < TRACKS; t++) {
            if (music_mute_pending(t) ? blink : !music_muted(t))
                btn |= 1u << MUTE_BTN[t];
        }
    }
    if (step >= 0)
        keys |= 1u << WHITE[(unsigned)step / 2u];
    if (slice >= 0) {
        const struct jungle_break *b = break_at(st.brk);
        mid |= 1u << WHITE[slice];
        for (i = 0; b && i < 8u; i++)        /* and the black one-shot keys that play this slice */
            if (b->shots[i] == (uint8_t)slice)
                mid |= 1u << BLACK[i];
    }
    if (roll >= 0)
        mid |= 1u << WHITE[roll];
    for (i = 0; i < 3u; i++)
        if (jungle_instant_active() >> i & 1u)
            mid |= 1u << BLACK[8u + i];
    if (ui.btn >> B_FX & 1u)                 /* FX held: the keys of the feedback's root */
        for (i = 0; i < NKEYS; i++)
            if ((5u + i) % 12u == st.root)
                keys |= 1u << i;
    plat_play_red(roll >= 0 || jungle_instant_active());   /* red while an effect sounds, held or from a fill */
    plat_glow(1);
    plat_leds(btn, keys);
    plat_lights_mid(0, mid & ~keys);
}

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), now = plat_ms();
    clock_follow(now);
    keys_input(keys, btn);
    buttons_input(btn, now);
    encoders_input(btn, now);
    if (st.filter_q > Q_FLAT && now - st.filter_turned >= 500u && now - st.filter_q_step >= 30u) {
        st.filter_q_step = now;
        jungle_filter_resonance(--st.filter_q);
    }
    if (keys != ui.keys)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    lights();
}

void app_input_only(void) { input(); }

/* ----------------------------------------------------------------- screen --- */
#define C_CYAN RGB(60, 200, 235)
#define C_GREEN RGB(80, 220, 120)
#define C_ORANGE RGB(240, 150, 40)
#define C_PURPLE RGB(170, 90, 230)
#define C_BLUE RGB(70, 120, 235)
#define C_RED RGB(235, 60, 50)

/* a knob's label, value and bar: filled from the left, or (centred) from the middle, as v1 drew them; with a
 * note (amber), the value waits for something: "NEXT" pattern, or "FROZEN" */
static void value_note(int32_t x, int32_t y, const char *label, const char *v, unsigned of127, int centred,
                       const char *note)
{
    int32_t w = 50, at = (int32_t)(of127 > 127u ? 127u : of127) * w / 127, mid = w / 2;
    cv_text(x, y, &FONT_XS, label, note ? C_ORANGE : C_GRAY);
    cv_text(x, y + 13, &FONT_M, v, note ? C_ORANGE : C_WHITE);
    if (note)
        cv_text(x, y + 45, &FONT_T, note, C_ORANGE);
    cv_rect(x, y + 40, w, 3, C_LINE);
    if (!centred)
        cv_rect(x, y + 40, at, 3, C_CYAN);
    else if (at == mid)
        cv_rect(x + mid - 1, y + 39, 2, 5, C_CYAN);   /* at the middle: a mark */
    else
        cv_rect(x + (at < mid ? at : mid), y + 40, at < mid ? mid - at : at - mid, 3, C_CYAN);
}
static void value(int32_t x, int32_t y, const char *label, const char *v, unsigned of127, int centred)
{
    value_note(x, y, label, v, of127, centred, 0);
}

/* between two colours: a at 0, b at 256 */
static uint16_t mix(uint16_t a, uint16_t b, uint32_t t)
{
    uint32_t u = 256u - t;
    return (uint16_t)((((a >> 11) * u + (b >> 11) * t) >> 8) << 11 | (((a >> 5 & 63u) * u + (b >> 5 & 63u) * t) >> 8) << 5 |
                      (((a & 31u) * u + (b & 31u) * t) >> 8));
}
/* a colour at level/256 of itself */
static uint16_t fade(uint16_t c, uint32_t level)
{
    uint32_t r = (c >> 11) * level >> 8, g = (c >> 5 & 63u) * level >> 8, b = (c & 31u) * level >> 8;
    return (uint16_t)(r << 11 | g << 5 | b);
}
/* the state line's red name (rolls, effects): for POP_MS after something started by itself, black on a block */
static void state_name(const char *name, const char *more, uint32_t now)
{
    if (ui.pop_t && now - ui.pop_t < POP_MS) {
        int32_t w = text_w(&FONT_M, name) + 8;
        cv_rect(2, 0, w, 26, C_RED);
        cv_text(6, 0, &FONT_M, name, C_BLACK);
        cv_text(w + 6, 0, &FONT_M, more, C_RED);
    } else {
        int32_t x = cv_text(6, 0, &FONT_M, name, C_RED);
        cv_text(x + 4, 0, &FONT_M, more, C_RED);
    }
}

/* Facility's page: the four tracks where the pattern is (a row each: the focus marked, the name, the mode and the
 * sound, the level, ON / OFF, amber while a mute waits for the phrase's end), and the focus's knobs below */
/* ENV held: the 16 slots where the pattern is, as the white keys (a cell each: empty dark, full white, the last
 * loaded or saved cyan, one waiting for its block amber) */
static void slots_grid(void)
{
    unsigned i;
    for (i = 0; i < SLOTS; i++) {
        int32_t x = (int32_t)(i % 8u) * 30, y = (int32_t)(i / 8u) * 38;
        uint16_t bgc = sl.pending == (int)i ? C_ORANGE : sl.current == (int)i ? C_CYAN : slots[i].used ? RGB(40, 40, 48)
                                                                                                   : C_BLACK;
        char n[4];
        cv_rect(x + 1, y + 1, 28, 36, bgc);
        if (!slots[i].used && sl.pending != (int)i)
            cv_rect(x + 1, y + 1, 28, 1, C_LINE), cv_rect(x + 1, y + 36, 28, 1, C_LINE);
        int_text((int32_t)i + 1, n);
        cv_text(x + 15 - text_w(&FONT_S, n) / 2, y + 4, &FONT_S, n,
                bgc == C_ORANGE || bgc == C_CYAN ? C_BLACK : slots[i].used ? C_WHITE : C_GRAY);
        if (slots[i].used) {
            const char *a = STYLE_ABBR[slots[i].style % MSTYLES];
            cv_text(x + 15 - text_w(&FONT_XS, a) / 2, y + 22, &FONT_XS, a,
                    bgc == C_ORANGE || bgc == C_CYAN ? C_BLACK : C_GRAY);
        }
    }
}
static void facility_tracks(uint32_t now)
{
    unsigned t;
    struct music_view mv;
    (void)now;
    music_view(&mv);
    for (t = 0; t < TRACKS; t++) {
        int32_t y = (int32_t)t * 19;
        int muted = music_muted(t), wait = music_mute_pending(t), focused = fac.focus == t + 1u;
        uint16_t c = wait ? C_ORANGE : muted ? C_GRAY : C_WHITE;
        char what[32];
        what[0] = 0;
        if (t != TRK_BREAK && fac.tstyle[t] != MSTYLE_BASE) {   /* a style of its own */
            text_cat(what, STYLE_ABBR[fac.tstyle[t]]);
            text_cat(what, " ");
        }
        if (t == TRK_BREAK) {
            const struct jungle_break *b = break_at(st.brk);
            text_cat(what, b ? b->name : "NO BREAK");
        } else if (t == TRK_BASS) {
            if (fac.bass_mode == MAUTO)
                text_cat(what, "A:");
            text_cat(what, BASS_MODE[mv.bass_mode]);
            text_cat(what, "/");
            text_cat(what, music_bass_sound_name(mv.bass_sound));
        } else if (t == TRK_HARMONY) {
            if (fac.harm_mode == MAUTO)
                text_cat(what, "A:");
            text_cat(what, mv.harm_detail[0] ? mv.harm_detail : "PAD");
            if (mv.harm_pad && mv.harm_detail[0])
                text_cat(what, "+P");
            text_cat(what, "/");
            text_cat(what, music_harm_sound_name(mv.harm_sound));
        } else {
            if (fac.mel_mode == MAUTO)
                text_cat(what, "A:");
            text_cat(what, MEL_MODE[mv.mel_mode % MMEL_MODES]);
            text_cat(what, "/");
            text_cat(what, music_mel_sound_name(mv.mel_sound));
        }
        if (focused)
            cv_rect(0, y, 240, 18, RGB(20, 60, 75));
        cv_rect(0, y, 3, 18, focused ? C_CYAN : C_BLACK);
        {
            char num[2] = {(char)('1' + t), 0};
            cv_text(7, y + 1, &FONT_S, num, C_CYAN);
        }
        cv_text(20, y + 1, &FONT_S, TRACK_NAME[t], c);
        cv_text(86, y + 3, &FONT_XS, what, muted && !wait ? C_GRAY : C_HI);
        cv_rect(194, y + 8, 12, 3, C_LINE);
        cv_rect(194, y + 8, fac.level[t] * 12 / 127, 3, muted ? C_GRAY : C_CYAN);
        cv_text(210, y + 3, &FONT_XS, wait ? "WAIT" : muted ? "OFF" : "ON", wait ? C_ORANGE : muted ? C_GRAY : C_GREEN);
    }
}
/* the BASE's row, where the state line is: the style, the progression (A: AUTO), the chord sounding */
static void facility_base_row(void)
{
    struct music_view mv;
    char t[24];
    music_view(&mv);
    if (fac.focus == FOC_BASE)
        cv_rect(0, 0, 240, 26, RGB(20, 60, 75));
    cv_rect(0, 0, 3, 26, fac.focus == FOC_BASE ? C_CYAN : C_BLACK);
    /* a style or a progression asked for and not yet sounding (from the next 4 bars): amber */
    cv_text(7, 0, &FONT_M, music_style_name(fac.style), fac.style != mv.style ? C_ORANGE : C_RED);
    t[0] = 0;
    if (fac.prog == MAUTO)
        text_cat(t, "A:");
    text_cat(t, fac.prog != MAUTO && (fac.prog != mv.prog || fac.style != mv.style) ? music_prog_name(fac.style, fac.prog)
                                                                                  : mv.prog_name);
    {
        int32_t w = text_w(&FONT_B, mv.chord);
        cv_text(234 - w, 4, &FONT_B, mv.chord, C_WHITE);
        if (mv.section[0]) {                 /* the form: the section and its bar, the progression below */
            char sct[24];
            sct[0] = 0;
            text_cat(sct, mv.section);
            text_cat(sct, " ");
            {
                char nb[8];
                int_text(mv.sect_bar, nb);
                text_cat(sct, nb);
                text_cat(sct, "/");
                int_text(mv.sect_bars, nb);
                text_cat(sct, nb);
            }
            cv_text(228 - w - text_w(&FONT_XS, sct), 1, &FONT_XS, sct, C_ORANGE);
            cv_text(228 - w - text_w(&FONT_XS, t), 13, &FONT_XS, t, C_GRAY);
        } else
            cv_text(228 - w - text_w(&FONT_XS, t), 9, &FONT_XS, t, C_GRAY);
    }
}
/* F5: SEL held, the groove's two knobs where K1 and K2 are (AUTO: the value the style gives, "A:") */
static void groove_knobs(void)
{
    struct music_view mv;
    char t[16], n[8];
    unsigned f = fac.focus == FOC_BASE ? TRK_BASS : fac.focus - 1u;
    uint8_t sw = fac.focus == FOC_BASE ? fac.swing : fac.tswing[f];
    music_view(&mv);
    t[0] = 0;
    if (sw == MAUTO)
        text_cat(t, fac.focus == FOC_BASE ? "A:" : "=");
    if (fac.focus != FOC_BASE && sw == MAUTO && fac.swing == MAUTO)
        t[0] = 0, text_cat(t, "A:");
    if (mv.swing[f] <= 50u)
        text_cat(t, "OFF");   /* straight */
    else {
        int_text(mv.swing[f], n);
        text_cat(t, n);
        text_cat(t, "%");
    }
    value(6, 46, "SWING", t, sw == MAUTO ? 0u : (unsigned)(sw - 50u) * 127u / 25u, 0);
    if (fac.focus == FOC_BASE) {
        t[0] = 0;
        if (fac.human == MAUTO)
            text_cat(t, "A:");
        int_text(mv.human, n);
        text_cat(t, n);
        value(64, 46, "HUMAN", t, (unsigned)mv.human * 127u / 100u, 0);
    } else if (f != TRK_BREAK) {
        int_text((int32_t)fac.accent[f] - 64, t);
        value(64, 46, f == TRK_MELODY ? "EXPRESSION" : "ACCENTS", t, fac.accent[f], 1);   /* F5b: the melody's phrase */
    }
}
/* F6a: the FX view's knobs ("A:": the style's) */
static void fx_knob(int32_t x, const char *label, uint8_t v, uint8_t resolved)
{
    char t[12];
    t[0] = 0;
    if (v == MAUTO)
        text_cat(t, "A:");
    int_text(resolved, t + (v == MAUTO ? 2 : 0));
    value(x, 46, label, t, resolved, 0);
}
static void fx_knobs(void)
{
    struct music_view mv;
    unsigned f;
    music_view(&mv);
    if (fac.focus == FOC_BASE) {
        fx_knob(6, "FEEDBACK", fac.efb, mv.efb);
        fx_knob(64, "COLOR", fac.ecolor, mv.ecolor);
        fx_knob(122, "DECAY", fac.rdecay, mv.rdecay);
        fx_knob(180, "RETURN", fac.fxret, mv.fxret);
        return;
    }
    f = fac.focus - 1u;
    fx_knob(6, "ECHO", fac.se[f], mv.se[f]);
    fx_knob(64, "REVERB", fac.sr[f], mv.sr[f]);
    if (f == TRK_BREAK) {
        fx_knob(122, "THROWS", fac.thr, mv.thr);
        return;
    }
    fx_knob(122, "CHORUS", fac.ch[f], mv.ch[f]);
    {
        char t[8];
        int p = (int)fac.pan[f] - 64;
        t[0] = 0;
        if (p == 0)
            text_cat(t, "C");
        else {
            text_cat(t, p < 0 ? "L" : "R");
            int_text(p < 0 ? -p : p, t + 1);
        }
        value(180, 46, "PAN", t, fac.pan[f], 1);
    }
}
static void facility_knobs(void)
{
    char t[16];
    unsigned f;
    if (fac.fx) {
        fx_knobs();
        return;
    }
    if (ui.btn >> B_SEL & 1u) {
        groove_knobs();
        return;
    }
    if (fac.focus == FOC_BASE) {
        static const char *const SPEED[3] = {"SLOW", "AS IS", "FAST"};
        int_text(fac.energy, t);
        value(6, 46, "ENERGY", t, fac.energy, 0);
        value(64, 46, "CHORDS", SPEED[fac.chords < 43u ? 0 : fac.chords < 86u ? 1 : 2], fac.chords, 1);
        if (fac.autodub)
            int_text(fac.autodub, t);
        else
            t[0] = 0, text_cat(t, "OFF");
        value(122, 46, "AUTO DUB", t, fac.autodub, 0);
        {
            static const char *const FORM_V[3] = {"OFF", "ON", "+BRK"};
            value(180, 46, "FORM", FORM_V[fac.form < 43u ? 0 : fac.form < 86u ? 1 : 2], fac.form, 0);
        }
        return;
    }
    f = fac.focus - 1u;
    int_text(fac.level[f], t);
    value(6, 46, "LEVEL", t, fac.level[f], 0);
    if (f == TRK_BREAK) {
        int_text((int32_t)st.rhythm, t);
        value(64, 46, "VARIATION", t, st.rhythm, 0);
        int_text((int32_t)st.warp, t);
        value(122, 46, "WARP", t, st.warp, 0);
        if (st.filter == 64)
            t[0] = 0, text_cat(t, "OFF");
        else
            int_text((int32_t)st.filter - 64, t);
        value(180, 46, "FILTER", t, st.filter, 1);
    } else {
        static const char *const LBL[3][3] = {
            {"CUTOFF", "RESO", "SLIDE"}, {"BRIGHT", "MOVEMENT", "DECAY"}, {"BRIGHT", "GLIDE", "VIBRATO"}};
        unsigned k, row = f == TRK_BASS ? 0u : f == TRK_HARMONY ? 1u : 2u;
        for (k = 0; k < 3u; k++) {
            if (row == 2u && k > 0u)
                int_text((int32_t)fac.param[f][k], t);
            else
                int_text((int32_t)fac.param[f][k] - 64, t);
            value(64 + (int32_t)k * 58, 46, LBL[row][k], t, fac.param[f][k], row == 2u && k > 0u ? 0 : 1);
        }
    }
}
/* the line at the bottom: what the focus's encoders do, its style and speed */
static void facility_hint(char *h)
{
    struct music_view mv;
    unsigned f;
    h[0] = 0;
    if (fac.fx) {                            /* F6a: the FX view */
        if (fac.focus == FOC_BASE) {
            struct music_view mv;
            char t[12];
            static const char *const RT[2] = {"ROOM", "SPRING"};
            music_view(&mv);
            text_cat(h, "ECHO ");
            if (fac.etime == MAUTO)
                text_cat(h, "A:");
            etime_text(mv.etime, t);
            text_cat(h, t);
            text_cat(h, "  REVERB ");
            if (fac.rtype == MAUTO)
                text_cat(h, "A:");
            text_cat(h, RT[mv.rtype & 1u]);
        } else
            text_cat(h, "FX: SEL+KNOB = AUTO  FX = BACK");
        return;
    }
    if (ui.btn >> B_SEL & 1u) {              /* F5: what SEL does to K1, K2 */
        text_cat(h, fac.focus == FOC_BASE ? "SEL: K1 SWING  K2 HUMAN" :
                    fac.focus - 1u == TRK_BREAK ? "SEL: K1 SWING (BREAK)" :
                    fac.focus - 1u == TRK_MELODY ? "SEL: K1 SWING  K2 EXPRESSION" : "SEL: K1 SWING  K2 ACCENTS");
        return;
    }
    if (fac.focus == FOC_BASE) {
        text_cat(h, "ALGO STYLE  PRESET PROGRESSION");
        return;
    }
    f = fac.focus - 1u;
    if (f == TRK_BREAK) {
        text_cat(h, "PRESET BREAK  GLO = BACK");
        return;
    }
    music_view(&mv);
    if (f == TRK_MELODY) {                   /* the seed it develops */
        text_cat(h, fac.mel_seed == MAUTO ? "SEED A:" : "SEED ");
        text_cat(h, music_seed_name(mv.mel_seed));
        text_cat(h, mv.feel[f] == 1 ? " 1/2" : mv.feel[f] == 4 ? " x2" : "");
        return;
    }
    text_cat(h, "STYLE ");
    text_cat(h, fac.tstyle[f] == MSTYLE_BASE ? "= BASE" : music_style_name(fac.tstyle[f]));
    text_cat(h, mv.feel[f] == 1 ? "  HALF SPEED" : mv.feel[f] == 4 ? "  DOUBLE SPEED" : "");
}

static void draw(void)
{
    char t[24];
    uint32_t now = plat_ms(), i;
    int step = jungle_current_step(), roll = jungle_roll_active();
    struct jungle_step pat[JUNGLE_STEPS];
    /* the header: the name, EXT, the tempo; or a message for a moment */
    /* the whole screen glows in the colour of what started by itself, fading out */
    uint32_t glow_p = ui.spark_t && now - ui.spark_t < ANIM_MS ? (now - ui.spark_t) * 256u / ANIM_MS : 256u;
    uint16_t bg = glow_p < 256u ? fade(ui.spark_c, (256u - glow_p) * ui.spark_lv / 256u) : C_BLACK;
    cv_begin(240, 38, bg);                   /* (down to the grid: no black seam between them) */
    if (now < ui.msg_until && st.page == PAGE_FACILITY) {   /* FACILITY stays; the message on the right, small */
        cv_text(6, 2, &FONT_M, "FACILITY", C_RED);
        cv_text(234 - text_w(&FONT_XS, ui.msg[0]), 2, &FONT_XS, ui.msg[0], C_GRAY);
        cv_text(234 - text_w(&FONT_S, ui.msg[1]), 17, &FONT_S, ui.msg[1], C_CYAN);
    } else if (now < ui.msg_until) {
        int32_t x = cv_text(6, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], C_CYAN);
    } else {
        int32_t x = st.page == PAGE_FACILITY ? cv_text(6, 2, &FONT_M, "FACILITY", C_RED)   /* Facility's page */
                                             : cv_text(6, 2, &FONT_M, APP_NAME, C_CYAN);
        if (st.external && !(ui.btn >> B_EDIT & 1u))   /* (EDIT held: the meter's place) */
            cv_text(x + 6, 4, &FONT_XS, "EXT", C_GREEN);
        if (FM1B_BLE && !(ui.btn >> B_EDIT & 1u))
            cv_text(x + 6, 17, &FONT_XS, "BLE", plat_ble_connected() ? C_HI : C_DIM);
        if (ui.btn >> B_EDIT & 1u) {         /* EDIT held: how busy the audio is */
            t[0] = 0;
            text_cat(t, "CPU ");
            int_text((int32_t)plat_cpu_pct(), t + 4);
            text_cat(t, "%");
            int32_t xc = 234 - text_w(&FONT_M, t);
            cv_text(xc, 4, &FONT_M, t, C_ORANGE);
            {   /* Amenity+ OPT: the parts (% of the block: the break, the tracks, the effects) and the FM6 voices
                 * (sounding / allowed, and how many the governor let go since power-on) */
                static const char *const NAME[3] = { "B", " T", " F" };
                char u[40];
                struct music_meter m;
                u[0] = 0;
                for (int k = 0; k < 3; k++) {
                    text_cat(u, NAME[k]);
                    int_text((int32_t)((meter_q[k] * 100u + 2048u) >> 12), u + text_len(u));
                }
                cv_text(xc - 6 - text_w(&FONT_XS, u), 4, &FONT_XS, u, C_GRAY);
                music_meter_peek(&m);
                u[0] = 0;
                text_cat(u, "FM ");
                int_text(m.fm_live, u + text_len(u));
                text_cat(u, "/");
                int_text(m.fm_cap, u + text_len(u));
                text_cat(u, " -");
                int_text((int32_t)m.shed, u + text_len(u));
                cv_text(xc - 6 - text_w(&FONT_XS, u), 17, &FONT_XS, u, m.shed ? C_ORANGE : C_GRAY);
            }
        } else {
            tenths_text(st.tempo, t);
            cv_text(234 - text_w(&FONT_L, t), 1, &FONT_L, t, jungle_tempo_pending() ? C_ORANGE : C_WHITE);
        }
    }
    cv_blit(0, 0);
    /* the pattern: 32 steps in two rows, the step playing marked below it; a hit white (stretch purple, octave
     * down blue), an empty step nothing, as in v1. What plays by itself or is held moves it about a little: a flip
     * sends a cyan ripple out from its first step, an automatic roll shakes the rows, a sweep darkens the hits as
     * the filter closes, feedback gives every hit an echo, LO-FI lays scan lines over it, an octave down sinks it */
    jungle_pattern(pat);
    cv_begin(240, 76, bg);
    if (ui.btn >> B_ENV & 1u)
        slots_grid();
    else if (st.page == PAGE_FACILITY)
        facility_tracks(now);
    else {
        unsigned fx = jungle_instant_active();
        uint32_t ripple = ui.flip_t && now - ui.flip_t < ANIM_MS ? (now - ui.flip_t) * 24u / ANIM_MS + 1u : 0u;
        int shake = ui.glitch_t && now - ui.glitch_t < ANIM_MS ? ((now / 33u) & 1u ? 3 : -3) : 0;
        uint32_t closed = (fx & JUNGLE_FX_SWEEP) && jungle_filter_now() < 64u ? (64u - jungle_filter_now()) * 200u / 64u : 0u;
        int32_t sink = (fx & JUNGLE_FX_OCTAVE) ? 3 : 0;
        /* a warped step as it plays: a stretch one stretches out and back, an octave-down one sinks the grid */
        uint32_t wp = ui.warp_t && now - ui.warp_t < ANIM_MS ? (now - ui.warp_t) * 256u / ANIM_MS : 256u;
        uint32_t swell = wp < 256u ? (wp < 64u ? wp * 4u : (256u - wp) * 256u / 192u) : 0u;   /* 0..256 and back */
        int32_t wx = -99, wy = 0;
        if (ui.warp_kind == 2u)
            sink += (int32_t)(swell * 7u / 256u);
        for (i = 0; i < JUNGLE_STEPS; i++) {
            int32_t x = 4 + (int32_t)(i % 16u) * 14 + (int32_t)(i % 16u / 4u) * 2 + (i / 16u ? -shake : shake);
            int32_t y = (int32_t)(i / 16u) * 36 + sink;
            int32_t d = ripple ? (int32_t)i - (int32_t)(ui.flip_bar * 16u + 10u) : 99;
            int lit = ripple && (d < 0 ? -d : d) + 1 >= (int32_t)ripple - 1 && (d < 0 ? -d : d) + 1 <= (int32_t)ripple;
            uint16_t c = pat[i].stretch ? C_PURPLE : pat[i].pitch < 0 || sink ? C_BLUE : C_WHITE;
            if (closed)
                c = mix(c, RGB(20, 70, 90), closed);
            if (pat[i].active && (fx & JUNGLE_FX_FEEDBACK))
                cv_rect(x + 3, y + 3, 12, 26, RGB(40, 90, 110));   /* the echo, behind */
            if (pat[i].active && ui.warp_kind == 1u && swell && i == ui.warp_step) {   /* (drawn last, on top) */
                wx = x;
                wy = y;
            }
            if (pat[i].active)
                cv_rect(x, y, 12, 26, lit ? C_CYAN : c);
            else if (lit)
                cv_rect(x, y + 8, 12, 10, fade(C_CYAN, 140u));
            if (pat[i].active && pat[i].flip && !lit) {   /* a flip's steps: a thin cyan rim */
                cv_rect(x, y, 12, 1, C_CYAN);
                cv_rect(x, y + 25, 12, 1, C_CYAN);
                cv_rect(x, y, 1, 26, C_CYAN);
                cv_rect(x + 11, y, 1, 26, C_CYAN);
            }
            if (pat[i].active) {
                char h[2] = {"0123456789ABCDEF"[pat[i].slice & 15u], 0};
                cv_text(x + 3, y + 6, &FONT_XS, h, C_BLACK);
            }
            if ((int)i == step)
                cv_rect(x, y + 28, 12, 3, C_GREEN);
        }
        if (wx > -99) {                      /* the stretch step stretching out and back */
            int32_t grow = (int32_t)(swell * 28u / 256u);
            char h[2] = {"0123456789ABCDEF"[pat[ui.warp_step].slice & 15u], 0};
            cv_rect(wx - grow, wy, 12 + 2 * grow, 26, C_PURPLE);
            cv_text(wx + 3, wy + 6, &FONT_XS, h, C_BLACK);
        }
        if (fx & JUNGLE_FX_LOFI)             /* scan lines */
            for (i = 1; i < 76u; i += 4u)
                cv_rect(0, (int32_t)i, 240, 1, bg);
    }
    cv_blit(0, 38);
    (void)step;
    /* what is happening, the modes, the knobs of the page */
    cv_begin(240, 126, bg);
    if (roll >= 0)
        state_name(ROLL_NAME[roll], "", now);
    else if (st.page == PAGE_FACILITY)
        facility_base_row();
    else if (jungle_instant_active()) {      /* the first of them by name, + if there are more */
        unsigned fx = jungle_instant_active(), k = fx & 1u ? 0u : fx & 2u ? 1u : fx & 4u ? 2u : 3u;
        state_name(INSTANT_NAME[k], fx & ~(1u << k) ? "+" : "", now);
    } else if (st.external && !jungle_playing())
        cv_text(6, 0, &FONT_M, "EXT: WAITING", C_GREEN);
    else
        cv_text(6, 0, &FONT_M, jungle_playing() ? "PLAYING" : "STOPPED", jungle_playing() ? C_GREEN : C_GRAY);
    if (st.page != PAGE_FACILITY) {
        const struct jungle_break *b = break_at(st.brk);
        const char *name = b ? b->name : "NO BREAK";
        cv_text(234 - text_w(&FONT_B, name), 6, &FONT_B, name, b ? C_WHITE : C_RED);
    }
    {
        int32_t x = 6;
        if (st.frozen)
            x = cv_text(x, 28, &FONT_XS, "FREEZE", C_ORANGE) + 10;
        if (st.auto_roll)
            x = cv_text(x, 28, &FONT_XS, "AUTO FILL", C_GREEN) + 10;
        if (st.transpose) {
            int_text(st.transpose, t);
            x = cv_text(x, 28, &FONT_XS, "TRANSPOSE", C_GRAY) + 4;
            cv_text(x, 28, &FONT_XS, t, C_WHITE);
        }
    }
    if (st.page == PAGE_FACILITY)
        facility_knobs();
    else if (st.page == PAGE_EQ) {
        static const char *const BAND[4] = {"LOW", "LO-MID", "HI-MID", "HIGH"};
        for (i = 0; i < 4u; i++) {
            t[0] = 0;
            if (st.eq_db[i] > 0)
                text_cat(t, "+");
            int_text(st.eq_db[i], t + (st.eq_db[i] > 0));
            text_cat(t, "DB");
            value(6 + (int32_t)i * 58, 46, BAND[i], t, (unsigned)(st.eq_db[i] + 12) * 127u / 24u, 1);
        }
    } else if (st.page == PAGE_FX) {
        int_text((int32_t)st.saturation, t);
        value(6, 46, "SATURATE", t, st.saturation, 0);
        int_text((int32_t)st.compression, t);
        value(64, 46, "COMPRESS", t, st.compression, 0);
        if (st.lofi) {                       /* LO-FI: its rate in kHz */
            unsigned hz, bits;
            jungle_lofi_view(st.lofi, &hz, &bits);
            int_text((int32_t)((hz + 500u) / 1000u), t);
            text_cat(t, "K");
        } else
            t[0] = 0, text_cat(t, "OFF");
        value(122, 46, "LO-FI", t, st.lofi, 0);
    } else {
        int_text((int32_t)((st.feedback * 100u + 63u) / 127u), t);
        text_cat(t, "%");
        value(6, 46, "FEEDBACK", t, st.feedback, 0);
        cv_text(6 + text_w(&FONT_M, t) + 4, 59, &FONT_XS, NOTE_NAME[st.root], C_GRAY);
        /* turned since the pattern playing was made: it takes the new value from the next one */
        unsigned made_rhythm, made_warp;
        const char *wait = st.frozen ? "FROZEN" : "NEXT";
        jungle_pattern_made(&made_rhythm, &made_warp);
        int_text((int32_t)st.rhythm, t);
        value_note(64, 46, "VARIATION", t, st.rhythm, 0, jungle_playing() && st.rhythm != made_rhythm ? wait : 0);
        int_text((int32_t)st.warp, t);
        value_note(122, 46, "WARP", t, st.warp, 0, jungle_playing() && st.warp != made_warp ? wait : 0);
        unsigned flt = (jungle_instant_active() & JUNGLE_FX_SWEEP) ? jungle_filter_now() : st.filter;   /* sweeping: live */
        if (flt == 64)
            t[0] = 0, text_cat(t, "OFF");
        else {
            unsigned hz = jungle_filter_hz(flt);
            t[0] = 0;
            text_cat(t, flt < 64 ? "LP" : "HP");
            int_text((int32_t)(hz >= 1000u ? hz / 1000u : hz), t + 2);
            if (hz >= 1000u)
                text_cat(t, "K");
        }
        value(180, 46, "FILTER", t, flt, 1);
    }
    if (st.lofi) {
        unsigned hz, bits;
        jungle_lofi_view(st.lofi, &hz, &bits);
        t[0] = 0;
        text_cat(t, "LO-FI ");
        int_text((int32_t)(hz / 1000u), t + 6);
        text_cat(t, "K ");
        {
            char b[8];
            int_text((int32_t)bits, b);
            text_cat(t, b);
        }
        text_cat(t, "BIT");
        cv_text(6, 104, &FONT_XS, t, C_ORANGE);
    }
    {
        const char *hint = st.page == PAGE_EQ ? "EQ: ENV = BACK" : st.page == PAGE_FX ? "FX: FX = BACK" :
                           "HOLD WHITE = ROLL";
        char fh[40];
        if (st.page == PAGE_FACILITY) {
            facility_hint(fh);
            hint = fh;
        }
        if (ui.btn >> B_ENV & 1u)
            hint = "KEY LOAD  +SEL VARY  +EDIT SAVE";
        if (sl.dirty)
            hint = "SLOT SAVED: WRITES AT STOP";
        cv_text(234 - text_w(&FONT_XS, hint), 104, &FONT_XS, hint, C_GRAY);
    }
    cv_blit(0, 114);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = 1;
}

void app_frame(void)
{
    static int last_step = -2, last_roll = -2, last_playing = -1, last_instant = -1;
    static unsigned last_made = 0xFFFFu;
    static uint32_t last_ble, last_xruns, last_cpu;
    uint32_t now = plat_ms(), xruns = plat_xruns();
    int step = jungle_current_step(), roll = jungle_roll_active(), playing = jungle_playing(),
        instant = (int)jungle_instant_active();
    input();
    slots_frame();
    music_cpu_hint(plat_cpu_pct());          /* the governor: a voice back only when there is room (Amenity+ OPT) */
    if (xruns != last_xruns) {               /* the render ran late: say so, as v1 did */
        if (last_xruns)
            app_say("AUDIO", "OVERLOAD");
        last_xruns = xruns;
    }
    if ((ui.btn >> B_EDIT & 1u) && now - last_cpu >= 250u) {
        last_cpu = now;
        ui.redraw = 1;
    }
    {                                        /* a new pattern took the knobs' values: the notes go */
        unsigned r, w;
        jungle_pattern_made(&r, &w);
        if ((r << 8 | w) != last_made) {
            last_made = r << 8 | w;
            ui.redraw = 1;
        }
    }
    {                                        /* what started (by itself, or a white key): a spark, a pop, a flash */
        uint32_t fx = (uint32_t)instant & 7u, was = last_instant < 0 ? 0u : (uint32_t)last_instant & 7u;   /* held or not */
        ui.spark_lv = ui.spark_lv ? ui.spark_lv : 75u;
        if (roll >= 0 && roll != last_roll) {    /* a roll: by itself, or a white key (each new one) */
            ui.spark_t = ui.pop_t = ui.glitch_t = now | 1u;
            ui.spark_c = C_RED;
            ui.spark_lv = 75u;
        } else if (fx & ~was) {
            ui.spark_t = ui.pop_t = ui.glitch_t = now | 1u;
            ui.spark_c = C_ORANGE;
            ui.spark_lv = 75u;
        } else if ((uint32_t)instant & JUNGLE_FX_SWEEP & ~(uint32_t)(last_instant < 0 ? 0 : last_instant)) {
            ui.spark_t = now | 1u;
            ui.spark_c = C_CYAN;
            ui.spark_lv = 75u;
        }
        if (step != last_step && step >= 0) {
            struct jungle_step pat[JUNGLE_STEPS];
            jungle_pattern(pat);
            if (step % 16 == 10 && pat[step].flip) {
                ui.flip_t = ui.spark_t = now | 1u;
                ui.flip_bar = (uint8_t)(step / 16);
                ui.spark_c = C_WHITE;
                ui.spark_lv = 75u;
            } else if (pat[step].active && (pat[step].stretch || pat[step].pitch < 0) && roll < 0) {
                ui.warp_t = ui.spark_t = now | 1u;   /* a warped step plays: its own move, a softer glow */
                ui.warp_kind = pat[step].stretch ? 1u : 2u;
                ui.warp_step = (uint8_t)step;
                ui.spark_c = pat[step].stretch ? C_PURPLE : C_BLUE;
                ui.spark_lv = 40u;
            }
        }
        if ((ui.spark_t && now - ui.spark_t < ANIM_MS + 40u) || (ui.flip_t && now - ui.flip_t < ANIM_MS + 40u) ||
            (ui.glitch_t && now - ui.glitch_t < ANIM_MS + 40u) || (ui.warp_t && now - ui.warp_t < ANIM_MS + 40u))
            ui.redraw = 1;                   /* (and one frame after, to clear it) */
    }
    {                                        /* the sweep: the filter's bar follows it */
        static unsigned last_filter = 64u;
        unsigned f = jungle_filter_now();
        if (((uint32_t)instant & JUNGLE_FX_SWEEP) && f != last_filter)
            ui.redraw = 1;
        last_filter = f;
    }
    if (instant != last_instant) {
        last_instant = instant;
        ui.redraw = 1;
    }
    {                                        /* a halved / doubled tempo came in: white again */
        static unsigned last_pending;
        unsigned p = jungle_tempo_pending();
        if (p != last_pending) {
            last_pending = p;
            ui.redraw = 1;
        }
    }
    if (step != last_step || roll != last_roll || playing != last_playing ||
        (uint32_t)plat_ble_connected() != last_ble || (ui.msg_until && now >= ui.msg_until)) {
        last_step = step;
        last_roll = roll;
        last_playing = playing;
        last_ble = (uint32_t)plat_ble_connected();
        if (ui.msg_until && now >= ui.msg_until)
            ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    }
}
