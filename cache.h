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

#ifndef CMUS_CACHE_H
#define CMUS_CACHE_H

#include "track_info.h"
#include "locking.h"

extern struct fifo_mutex cache_mutex;

#define cache_lock() fifo_mutex_lock(&cache_mutex)
#define cache_yield() fifo_mutex_yield(&cache_mutex)
#define cache_unlock() fifo_mutex_unlock(&cache_mutex)

int cache_init(void);
int cache_close(void);
struct track_info *cache_get_ti(const char *filename, int force);
void cache_remove_ti(struct track_info *ti);
struct track_info **cache_refresh(int *count, int force);
struct track_info *lookup_cache_entry(const char *filename, unsigned int hash);

#endif
