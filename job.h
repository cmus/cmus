/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2008 Timo Hirvonen
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

#ifndef JOB_H
#define JOB_H

#include "cmus.h"

struct add_data {
	enum file_type type;
	char *name;
	add_ti_cb add;
	unsigned int force : 1;
};

struct update_data {
	size_t size;
	size_t used;
	struct track_info **ti;
	unsigned int force : 1;
};

struct update_cache_data {
	unsigned int force : 1;
};

void do_add_job(void *data);
void free_add_job(void *data);
void do_update_job(void *data);
void free_update_job(void *data);
void do_update_cache_job(void *data);
void free_update_cache_job(void *data);

#endif
