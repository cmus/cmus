/* 
 * Copyright 2005 Timo Hirvonen
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

#ifndef _CMUS_H
#define _CMUS_H

#include <track_db.h>

extern int repeat;
extern int shuffle;

int cmus_init(void);
void cmus_exit(void);
void cmus_play_file(const char *filename);

int cmus_enqueue(const char *name, int prepend);
int cmus_add_to_lib(const char *name);
int cmus_add_to_pl(const char *name);
void cmus_clear_playlist(void);
int cmus_save_playlist(const char *filename);
int cmus_load_playlist(const char *name);
void cmus_update_playlist(void);

struct track_info *cmus_get_track_info(const char *name);

int cmus_is_playlist(const char *filename);
void cmus_update_selected(void);

int cmus_playlist_for_each(const char *buf, int size, int reverse,
		int (*cb)(void *data, const char *line),
		void *data);

/* bindable */
void cmus_next(void);
void cmus_prev(void);
void cmus_seek_bwd(void);
void cmus_seek_fwd(void);
void cmus_vol_up(void);
void cmus_vol_down(void);
void cmus_vol_left_up(void);
void cmus_vol_left_down(void);
void cmus_vol_right_up(void);
void cmus_vol_right_down(void);
void cmus_toggle_repeat(void);
void cmus_toggle_shuffle(void);

#endif
