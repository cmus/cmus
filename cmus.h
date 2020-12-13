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

#ifndef CMUS_API_H
#define CMUS_API_H

#include "track_info.h"

enum file_type {
	/* not found, device file... */
	FILE_TYPE_INVALID,

	FILE_TYPE_URL,
	FILE_TYPE_PL,
	FILE_TYPE_DIR,
	FILE_TYPE_FILE,
	FILE_TYPE_CDDA
};

typedef int (*track_info_cb)(void *data, struct track_info *ti);

/* lib_for_each, lib_for_each_filtered, pl_for_each, play_queue_for_each */
typedef int (*for_each_ti_cb)(track_info_cb cb, void *data, void *opaque);

/* lib_for_each_sel, pl_for_each_sel, play_queue_for_each_sel */
typedef int (*for_each_sel_ti_cb)(track_info_cb cb, void *data, int reverse, int advance);

/* lib_add_track, pl_add_track, play_queue_append, play_queue_prepend */
typedef void (*add_ti_cb)(struct track_info *, void *opaque);

/* cmus_save, cmus_save_ext */
typedef int (*save_ti_cb)(for_each_ti_cb for_each_ti, const char *filename,
		void *opaque);

int cmus_init(void);
void cmus_exit(void);
void cmus_play_file(const char *filename);

/* detect file type, returns absolute path or url in @ret */
enum file_type cmus_detect_ft(const char *name, char **ret);

/* add to library, playlist or queue view
 *
 * @add   callback that does the actual adding
 * @name  playlist, directory, file, URL
 * @ft    detected FILE_TYPE_*
 * @jt    JOB_TYPE_{LIB,PL,QUEUE}
 *
 * returns immediately, actual work is done in the worker thread.
 */
void cmus_add(add_ti_cb, const char *name, enum file_type ft, int jt,
		int force, void *opaque);

int cmus_save(for_each_ti_cb for_each_ti, const char *filename, void *opaque);
int cmus_save_ext(for_each_ti_cb for_each_ti, const char *filename,
		void *opaque);

void cmus_update_cache(int force);
void cmus_update_lib(void);
void cmus_update_tis(struct track_info **tis, int nr, int force);

int cmus_is_playlist(const char *filename);
int cmus_is_playable(const char *filename);
int cmus_is_supported(const char *filename);

int cmus_playlist_for_each(const char *buf, int size, int reverse,
		int (*cb)(void *data, const char *line),
		void *data);

void cmus_next(void);
void cmus_prev(void);

extern int cmus_next_track_request_fd;
struct track_info *cmus_get_next_track(void);
void cmus_provide_next_track(void);
void cmus_track_request_init(void);

int cmus_can_raise_vte(void);
void cmus_raise_vte(void);

#endif
