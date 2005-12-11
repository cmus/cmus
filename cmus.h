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

extern int cmus_init(void);
extern void cmus_exit(void);
extern void cmus_play_file(const char *filename);

extern int cmus_enqueue(const char *name, int prepend);
extern int cmus_add(const char *name);
extern void cmus_clear_playlist(void);
extern int cmus_save_playlist(const char *filename);
extern int cmus_load_playlist(const char *name);
extern void cmus_update_playlist(void);

extern struct track_info *cmus_get_track_info(const char *name);

extern int cmus_is_playlist(const char *filename);
extern void cmus_update_selected(void);

int cmus_playlist_for_each(const char *buf, int size, int reverse,
		int (*cb)(void *data, const char *line),
		void *data);

/* bindable */
extern void cmus_next(void);
extern void cmus_prev(void);
extern void cmus_seek_bwd(void);
extern void cmus_seek_fwd(void);
extern void cmus_vol_up(void);
extern void cmus_vol_down(void);
extern void cmus_vol_left_up(void);
extern void cmus_vol_left_down(void);
extern void cmus_vol_right_up(void);
extern void cmus_vol_right_down(void);

#endif
