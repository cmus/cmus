/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2006 Timo Hirvonen
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

#ifndef CMUS_PL_H
#define CMUS_PL_H

#include "track_info.h"
#include "window.h"
#include "editable.h"
#include "cmus.h"

struct pl_list_info {
	const char *name;
	unsigned marked : 1;
	unsigned active : 1;
	unsigned selected : 1;
	unsigned current : 1;
};

extern struct window *pl_list_win;
extern struct editable_shared pl_editable_shared;

void pl_init(void);
void pl_exit(void);
void pl_save(void);
void pl_import(const char *path);
void pl_export_selected_pl(const char *path);
struct searchable *pl_get_searchable(void);
int pl_add_file_to_marked_pl(const char *file);
void pl_add_track_to_marked_pl(struct track_info *ti);
void pl_rename_selected_pl(const char *name);
void pl_create(const char *name);
void pl_get_sort_str(char *buf, size_t size);
void pl_set_sort_str(const char *buf);
void pl_clear(void);
struct track_info *pl_goto_next(void);
struct track_info *pl_goto_prev(void);
struct track_info *pl_play_selected_row(void);
void pl_select_playing_track(void);
void pl_reshuffle(void);
int _pl_for_each_sel(track_info_cb cb, void *data, int reverse);
int pl_for_each_sel(track_info_cb cb, void *data, int reverse, int advance);
void pl_reload_visible(void);
struct window *pl_cursor_win(void);
void pl_set_nr_rows(int h);
unsigned int pl_visible_total_time(void);
unsigned int pl_playing_total_time(void);
struct simple_track *pl_get_playing_track(void);
void pl_update_track(struct track_info *old, struct track_info *new);
int pl_get_cursor_in_track_window(void);
int pl_visible_is_marked(void);
const char *pl_marked_pl_name(void);
void pl_set_marked_pl_by_name(const char *name);

void pl_mark_for_redraw(void);
int pl_needs_redraw(void);
void pl_draw(void (*list)(struct window *win),
		void (*tracks)(struct window *win), int full);
void pl_list_iter_to_info(struct iter *iter, struct pl_list_info *info);

static inline void pl_add_track_to_marked_pl2(struct track_info *ti,
		void *opaque)
{
	pl_add_track_to_marked_pl(ti);
}

/* cmd wrappers */

void pl_invert_marks(void);
void pl_mark(char *arg);
void pl_unmark(void);
void pl_rand(void);
void pl_win_mv_after(void);
void pl_win_mv_before(void);
void pl_win_remove(void);
void pl_win_toggle(void);
void pl_win_update(void);
void pl_win_next(void);

#endif
