/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef CMUS_TEMPO_H
#define CMUS_TEMPO_H

#include "sf.h"

#define TEMPO_BLOCK_FRAMES 1024
#define PLAYER_SPEED_MIN 0.25
#define PLAYER_SPEED_MAX 4.0

struct tempo;

int tempo_enabled(void);
int tempo_supported(sample_format_t sf);
struct tempo *tempo_new(sample_format_t sf, double speed);
void tempo_free(struct tempo *t);
/* Feed at most TEMPO_BLOCK_FRAMES, in the original PCM format. */
void tempo_write(struct tempo *t, const char *buf, int count);
void tempo_finish(struct tempo *t);
int tempo_finished(const struct tempo *t);
double tempo_speed(const struct tempo *t);
/* The returned buffer remains valid until it has been consumed. */
int tempo_read(struct tempo *t, char **buf);
void tempo_consume(struct tempo *t, int count);

#endif
