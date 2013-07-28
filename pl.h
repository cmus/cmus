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

#ifndef PL_H
#define PL_H

#include "editable.h"
#include "track_info.h"
#include "track.h"

extern struct editable pl_editable;
extern struct simple_track *pl_cur_track;

void pl_init(void);
struct track_info *pl_set_next(void);
struct track_info *pl_set_prev(void);
struct track_info *pl_set_selected(void);
void pl_add_track(struct track_info *track_info);
void pl_sel_current(void);
void pl_reshuffle(void);
int pl_for_each(int (*cb)(void *data, struct track_info *ti), void *data);

#endif
