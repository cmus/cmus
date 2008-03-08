/* 
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _PLAYER_H
#define _PLAYER_H

#include "locking.h"
#include "comment.h"
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

enum player_status {
	PLAYER_STATUS_STOPPED,
	PLAYER_STATUS_PLAYING,
	PLAYER_STATUS_PAUSED
};

enum replaygain {
	RG_DISABLED,
	RG_TRACK,
	RG_ALBUM
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
extern enum replaygain replaygain;
extern int replaygain_limit;
extern double replaygain_preamp;
extern int soft_vol;
extern int soft_vol_l;
extern int soft_vol_r;

void player_load_plugins(void);

void player_init(const struct player_callbacks *callbacks);
void player_exit(void);

/* set current file */
void player_set_file(struct track_info *ti);

/* set current file and start playing */
void player_play_file(struct track_info *ti);

void player_play(void);
void player_stop(void);
void player_pause(void);
void player_seek(double offset, int relative);
void player_set_op(const char *name);
void player_set_buffer_chunks(unsigned int nr_chunks);
int player_get_buffer_chunks(void);

void player_set_soft_volume(int l, int r);
void player_set_soft_vol(int soft);
void player_set_rg(enum replaygain rg);
void player_set_rg_limit(int limit);
void player_set_rg_preamp(double db);

int player_set_op_option(unsigned int id, const char *val);
int player_get_op_option(unsigned int id, char **val);

#define player_info_lock() cmus_mutex_lock(&player_info.mutex)
#define player_info_unlock() cmus_mutex_unlock(&player_info.mutex)

#endif
