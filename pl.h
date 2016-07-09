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

#include "editable.h"
#include "track_info.h"
#include "track.h"

extern struct editable pl_editable;
extern struct simple_track *pl_cur_track;
extern struct rb_root pl_shuffle_root;

void pl_init(void);
void pl_exit(void);
struct track_info *pl_goto_next(void);
struct track_info *pl_goto_prev(void);
struct track_info *pl_activate_selected(void);
void pl_add_track(struct track_info *track_info, void *opaque);
void pl_sel_current(void);
void pl_reshuffle(void);
int pl_for_each(int (*cb)(void *data, struct track_info *ti), void *data, void *opaque);

struct searchable *pl_get_searchable(void);
unsigned int pl_playing_total_time(void);
void pl_set_nr_rows(int h);
int pl_needs_redraw(void);

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
