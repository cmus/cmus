/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2005-2006 Timo Hirvonen
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

#ifndef _PLAY_QUEUE_H
#define _PLAY_QUEUE_H

#include "editable.h"
#include "track_info.h"

extern struct editable pq_editable;

void play_queue_init(void);
void play_queue_append(struct track_info *ti);
void play_queue_prepend(struct track_info *ti);
struct track_info *play_queue_remove(void);
int play_queue_for_each(int (*cb)(void *data, struct track_info *ti), void *data);

#endif
