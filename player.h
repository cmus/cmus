/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2004 Timo Hirvonen
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _PLAYER_H
#define _PLAYER_H

#include "locking.h"
#include "track_info.h"

#include <pthread.h>

enum {
	/* no error */
	PLAYER_ERROR_SUCCESS,
	/* system error (error code in errno) */
	PLAYER_ERROR_ERRNO,
	/* function not supported */
	PLAYER_ERROR_NOT_SUPPORTED
};

extern const char * const player_status_names[];
enum player_status {
	PLAYER_STATUS_STOPPED,
	PLAYER_STATUS_PLAYING,
	PLAYER_STATUS_PAUSED,
	NR_PLAYER_STATUS
};

enum replaygain {
	RG_DISABLED,
	RG_TRACK,
	RG_ALBUM,
	RG_TRACK_PREFERRED,
	RG_ALBUM_PREFERRED
};

struct player_callbacks {
	int (*get_next)(struct track_info **ti);
};

struct player_info {
	pthread_mutex_t mutex;

	/* current track */
	struct track_info *ti;

	/* stream metadata */
	char metadata[255 * 16 + 1];

	/* status */
	enum player_status status;
	int pos;
	int current_bitrate;

	int buffer_fill;
	int buffer_size;

	/* display this if not NULL */
	char *error_msg;

	unsigned int file_changed : 1;
	unsigned int metadata_changed : 1;
	unsigned int status_changed : 1;
	unsigned int position_changed : 1;
	unsigned int buffer_fill_changed : 1;
};

extern struct player_info player_info;
extern int player_cont;
extern int player_repeat_current;
extern enum replaygain replaygain;
extern int replaygain_limit;
extern double replaygain_preamp;
extern int soft_vol;
extern int soft_vol_l;
extern int soft_vol_r;

void player_init(const struct player_callbacks *callbacks);
void player_exit(void);

/* set current file */
void player_set_file(struct track_info *ti);

/* set current file and start playing */
void player_play_file(struct track_info *ti);

/* update track info */
void player_file_changed(struct track_info *ti);

void player_play(void);
void player_stop(void);
void player_pause(void);
void player_seek(double offset, int relative, int start_playing);
void player_set_op(const char *name);
void player_set_buffer_chunks(unsigned int nr_chunks);
int player_get_buffer_chunks(void);

void player_set_soft_volume(int l, int r);
void player_set_soft_vol(int soft);
void player_set_rg(enum replaygain rg);
void player_set_rg_limit(int limit);
void player_set_rg_preamp(double db);

#define player_info_lock() cmus_mutex_lock(&player_info.mutex)
#define player_info_unlock() cmus_mutex_unlock(&player_info.mutex)

#endif
