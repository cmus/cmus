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

#ifndef CACHE_H
#define CACHE_H

#include "track_info.h"
#include "locking.h"

extern pthread_mutex_t cache_mutex;

#define cache_lock() cmus_mutex_lock(&cache_mutex)
#define cache_unlock() cmus_mutex_unlock(&cache_mutex)

int cache_init(void);
int cache_close(void);
struct track_info *cache_get_ti(const char *filename, int force);
void cache_remove_ti(struct track_info *ti);
struct track_info **cache_refresh(int *count, int force);
struct track_info *lookup_cache_entry(const char *filename, unsigned int hash);

#endif
