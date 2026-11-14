/* SPDX-License-Identifier: GPL-3.0-only */
/* Amenity+: bass, a rhythm layer and a pad that follow the break (music.c).
 * The break engine calls music_step and music_hit from the audio interrupt, at the frame of the block they fall on;
 * app_render calls music_render after jungle_render, so the music lands on the same samples as the break. */
#ifndef MUSIC_H
#define MUSIC_H
#include <stdint.h>

/* the engine's hooks (jungle.c): a 16th of the 2-bar phrase (0..31), and a slice the break plays */
void music_step(unsigned step, unsigned frame);
void music_hit(unsigned slice, int pitch, unsigned frame);
/* the audio: adds the music to the break, n stereo frames, 24-bit */
void music_render(int32_t *lr, unsigned n);

/* the controls (main loop) */
enum { MUSIC_OFF, MUSIC_BASS, MUSIC_RHYTHM, MUSIC_FULL, MUSIC_MAX, MUSIC_LEVELS };   /* MAX: everything held: CPU test */
enum { MBASS_KICK, MBASS_LONG, MBASS_RAGGA, MBASS_ACID, MBASS_MODES };
enum { MTIMBRE_SUB, MTIMBRE_ACID, MTIMBRE_REESE, MTIMBRES };
enum { MPOOL_SKANK, MPOOL_JAZZ, MPOOL_MIX, MPOOLS };
void music_init(void);
void music_level(unsigned level);
void music_bass_mode(unsigned mode);
void music_bass_timbre(unsigned timbre);
void music_pool(unsigned pool);
void music_root(unsigned pc);          /* the key: the feedback's root (FX + key), from the next phrase */
#endif
