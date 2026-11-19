/* SPDX-License-Identifier: GPL-3.0-only */
/* Facility: the four tracks beside the break (music.c). Track 1 is Amenity itself (jungle.c); 2 the bass, 3 the
 * harmony, 4 the melody (to come). The break engine calls music_step and music_hit from the audio interrupt, at the
 * frame of the block they fall on; app_render calls music_render after jungle_render, so the tracks land on the
 * same samples as the break, and the break's own mute and level are applied there too.
 *
 * Styles (F2a): the BASE style says what chords sound (its four progressions, each chord with its scale, how fast
 * they change) and is one for all; each track has a style of its own (or the BASE's) that says how it plays them:
 * its modes, its sounds, its speed (half, normal or double: the one that brings the tempo nearest its centre).
 * The form (F2b): sections (intro, verse, pre, chorus, bridge, breakdown, drop, dub version) in a form of the style's,
 * each with its tracks, its energy and its progression; the transitions in each section's last phrase. A song
 * repeats without its intro; a new one at PLAY. The hand rules: a mute of yours, a mode or a sound set by hand, a
 * roll held, the form leaves alone. */
#ifndef MUSIC_H
#define MUSIC_H
#include <stdint.h>

/* the engine's hooks (jungle.c): a 16th of the 2-bar phrase (0..31), and a slice the break plays */
void music_step(unsigned step, unsigned frame);
void music_hit(unsigned slice, int pitch, unsigned frame);
/* the audio: the break's gain, then the tracks added, n stereo frames, 24-bit */
void music_render(int32_t *lr, unsigned n);

enum { TRK_BREAK, TRK_BASS, TRK_HARMONY, TRK_MELODY, TRACKS };
enum { MSTYLE_BREAKCORE, MSTYLE_JAZZ, MSTYLE_ACID, MSTYLE_HIPHOP, MSTYLE_DUB, MSTYLES };
enum { MBASS_KICK, MBASS_LONG, MBASS_RAGGA, MBASS_ACID, MBASS_WALK, MBASS_ONEDROP, MBASS_RIFF, MBASS_MODES };
enum { MTIMBRE_SUB, MTIMBRE_ACID, MTIMBRE_REESE, MTIMBRE_UPRIGHT, MTIMBRE_FM, MTIMBRE_DUB, MTIMBRES };   /* F6b: +3 (UPRIGHT plays FINGER since F6c) */
enum { MHARM_SKANK, MHARM_JAZZ, MHARM_MIX, MHARM_STABS, MHARM_PAD, MHARM_MIX_PAD, MHARM_MODES };
enum { MHSOUND_ORGAN, MHSOUND_SAW, MHSOUND_EPIANO, MHSOUND_CLAVI, MHSOUND_HARPSI, MHSOUND_GUITAR, MHSOUND_BRASS, MHSOUNDS };
enum { MMEL_SEED, MMEL_RIFF, MMEL_LINE, MMEL_ANSWER, MMEL_MODES };                /* the melody's modes */
enum { MMSOUND_SAW, MMSOUND_SQUARE, MMSOUND_FM, MMSOUND_REED, MMSOUND_STRING, MMSOUND_FLUTE, MMSOUND_VIBES, MMSOUND_PLUCK,
       MMSOUND_BRASS, MMSOUNDS };   /* and sounds */
#define MAUTO 0xFFu          /* a mode, sound or progression the style chooses */
#define MSTYLE_BASE 0xFFu    /* a track that plays in the BASE style */

/* the controls (main loop) */
void music_init(void);
void music_mute(unsigned track, int muted, int at_phrase);   /* at_phrase: from the next phrase (the 2 bars' end) */
int music_muted(unsigned track);                             /* as it sounds now */
int music_mute_pending(unsigned track);                      /* 1: it changes at the next phrase */
void music_track_level(unsigned track, unsigned v127);       /* 0..127, 100 = unity */
void music_param(unsigned track, unsigned k, unsigned v127); /* K2..K4 of the bass and the harmony (k 0..2), 64 = as built */
void music_style(unsigned style);                            /* the BASE, from the next phrase */
void music_prog(unsigned p);                                 /* 0..3, or MAUTO */
void music_energy(unsigned v127);
void music_chord_speed(unsigned v127);                       /* <43 slower (x2), <86 as written, faster (x1/2) */
void music_autodub(unsigned v127);                           /* how often a track drops out for a bar or two */
void music_track_style(unsigned track, unsigned style);      /* MSTYLE_BASE or a style */
void music_form(unsigned v127);   /* the song's form: <43 OFF, <86 ON, else ON + BREAK (it mutes the break, asks fills) */
void music_bass_mode(unsigned mode);                         /* MAUTO or a mode */
void music_bass_timbre(unsigned timbre);
void music_harm_mode(unsigned mode);
void music_harm_sound(unsigned sound);
void music_mel_mode(unsigned mode);                          /* MAUTO or a mode */
void music_mel_sound(unsigned sound);
void music_mel_seed(unsigned seed);                          /* MAUTO (the style's) or one of music_seeds() */
unsigned music_seeds(void);
const char *music_seed_name(unsigned seed);
void music_snares(uint32_t slices);
/* the groove (F5): the swing (50..75 %, else AUTO: the style's), HUMAN (0..100, else AUTO), a track's swing (50..75,
 * else AUTO: as the BASE; the break's own, with jungle_swing), a track's accents (0..127, 64 the style's map) */
void music_swing(unsigned pct);
void music_human(unsigned v);
void music_track_swing(unsigned track, unsigned pct);
void music_accent(unsigned track, unsigned v127);
/* the effects (F6a): the BASE's busses (MAUTO: the style's; the echo's time 0..4 = 1/8 3/16 1/4 3/8 1/2, 10.. free:
 * 100 ms + 50 a step; the reverb's type 0 ROOM 1 SPRING; the rest 0..127), and each track's sends (MAUTO: its
 * style's; the break's start at 0), its chorus and its pan (64 the centre) */
enum { MFX_ETIME, MFX_EFB, MFX_ECOLOR, MFX_RTYPE, MFX_RDECAY, MFX_RETURN, MFX_THROW };
enum { MSEND_ECHO, MSEND_REVERB, MSEND_CHORUS, MSEND_PAN };
void music_fx(unsigned what, unsigned v);
void music_send(unsigned track, unsigned what, unsigned v);                          /* the break's snare slices (its one-shots'): the melody avoids them */
void music_root(unsigned pc);       /* the key: the feedback's root (FX + key), from the next progression */
void music_stress(int on);          /* CPU test: every voice held, every track heard */
/* the slots (F3): the generator's seed from the next 4-bar block (with the form: a new song from it), the seed of
 * what plays (the song's, or the block's without the form), the 16ths to the next block and the blocks so far */
void music_seed(uint32_t seed);
uint32_t music_song_seed(void);
unsigned music_block_left(void);
uint32_t music_blocks(void);

/* what plays now, for the screen */
struct music_view {
    uint8_t style, prog, prog_auto;          /* the BASE and its progression (0..3) */
    const char *prog_name, *chord;           /* the progression's name, the chord sounding (roman) */
    uint8_t bass_mode, bass_sound, harm_mode, harm_sound;   /* what the tracks play now (AUTO resolved) */
    uint8_t harm_pad;                        /* the pad sounding with it */
    const char *harm_detail;                 /* the rhythm layer's figure: "A", "J HEMIOLA", ... */
    uint8_t feel[TRACKS];                    /* 1 half, 2 normal, 4 double */
    uint8_t mel_mode, mel_sound, mel_seed;   /* the melody now (AUTO resolved) */
    uint8_t form;                            /* 0 OFF, 1 ON, 2 ON + BREAK */
    const char *section;                     /* the section sounding (form on) */
    uint8_t sect_bar, sect_bars;             /* its bar (1..) and its length, in the style's bars */
    uint8_t swing[TRACKS], human;            /* the groove playing (AUTO resolved): %, 0..100 */
    uint8_t etime, efb, ecolor, rtype, rdecay, fxret, thr;   /* the effects (AUTO resolved) */
    uint8_t se[TRACKS], sr[TRACKS], ch[TRACKS];
};
void music_view(struct music_view *v);
const char *music_style_name(unsigned style);
const char *music_prog_name(unsigned style, unsigned p);
#endif
