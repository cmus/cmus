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

#include <locking.h>
#include <comment.h>

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

struct player_callbacks {
	int (*get_next)(char **filename);
};

struct player_info {
	pthread_mutex_t mutex;

	char filename[256];

	/* stream metadata */
	char metadata[255 * 16 + 1];

	/* status */
	enum player_status status;
	int pos;
	int cont;

	/* volume */
	int vol_left;
	int vol_right;

	int buffer_fill;
	int buffer_size;

	/* display this if not NULL */
	char *error_msg;

	unsigned int file_changed : 1;
	unsigned int metadata_changed : 1;
	unsigned int status_changed : 1;
	unsigned int position_changed : 1;
	unsigned int buffer_fill_changed : 1;
	unsigned int volume_changed : 1;
};

extern struct player_info player_info;

extern int player_init(const struct player_callbacks *callbacks);
extern void player_exit(void);

/* set current file */
extern void player_set_file(const char *filename);

/* set current file and start playing */
extern void player_play_file(const char *filename);

extern void player_play(void);
extern void player_stop(void);
extern void player_pause(void);
extern void player_seek(double offset, int whence);
extern int player_set_op(const char *name);
extern void player_set_buffer_size(unsigned int nr_chunks);
extern int player_get_buffer_size(void);
extern void player_set_buffer_seconds(unsigned int seconds);
extern void player_toggle_cont(void);
extern void player_set_cont(int value);
extern int player_get_fileinfo(const char *filename, int *duration,
		struct comment **comments);

/* volume: 0 - 100 */

extern int player_get_volume(int *left, int *right);
extern int player_set_volume(int left, int right);
extern int player_add_volume(int left, int right);

extern int player_set_op_option(const char *key, const char *val);
extern int player_for_each_op_option(void (*callback)(void *data, const char *key), void *data);
extern char **player_get_supported_extensions(void);

#define player_info_lock() cmus_mutex_lock(&player_info.mutex)
#define player_info_unlock() cmus_mutex_unlock(&player_info.mutex)

#endif
