/* SPDX-License-Identifier: GPL-3.0-only */
/* Facility: the four tracks beside the break (music.c). Track 1 is Amenity itself (jungle.c); 2 the bass, 3 the
 * harmony, 4 the melody (to come). The break engine calls music_step and music_hit from the audio interrupt, at the
 * frame of the block they fall on; app_render calls music_render after jungle_render, so the tracks land on the
 * same samples as the break, and the break's own mute and level are applied there too. */
#ifndef MUSIC_H
#define MUSIC_H
#include <stdint.h>

/* the engine's hooks (jungle.c): a 16th of the 2-bar phrase (0..31), and a slice the break plays */
void music_step(unsigned step, unsigned frame);
void music_hit(unsigned slice, int pitch, unsigned frame);
/* the audio: the break's gain, then the tracks added, n stereo frames, 24-bit */
void music_render(int32_t *lr, unsigned n);

enum { TRK_BREAK, TRK_BASS, TRK_HARMONY, TRK_MELODY, TRACKS };
enum { MBASS_KICK, MBASS_LONG, MBASS_RAGGA, MBASS_ACID, MBASS_MODES };            /* the bass track's modes */
enum { MTIMBRE_SUB, MTIMBRE_ACID, MTIMBRE_REESE, MTIMBRES };                      /* and sounds */
enum { MHARM_SKANK, MHARM_JAZZ, MHARM_MIX, MHARM_PAD, MHARM_MIX_PAD, MHARM_MODES }; /* the harmony's modes */
enum { MHSOUND_ORGAN, MHSOUND_SAW, MHSOUNDS };                                     /* and sounds */

/* the controls (main loop) */
void music_init(void);
void music_mute(unsigned track, int muted, int at_phrase);   /* at_phrase: from the next phrase (the 2 bars' end) */
int music_muted(unsigned track);                             /* as it sounds now */
int music_mute_pending(unsigned track);                      /* 1: it changes at the next phrase */
void music_track_level(unsigned track, unsigned v127);       /* 0..127, 100 = unity */
void music_param(unsigned track, unsigned k, unsigned v127); /* K2..K4 of the bass and the harmony (k 0..2), 64 = as built */
void music_bass_mode(unsigned mode);
void music_bass_timbre(unsigned timbre);
void music_harm_mode(unsigned mode);
void music_harm_sound(unsigned sound);
void music_root(unsigned pc);       /* the key: the feedback's root (FX + key), from the next phrase */
void music_stress(int on);          /* CPU test: every voice held, every track heard */
#endif
