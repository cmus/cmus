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

#ifndef CMUS_JOB_H
#define CMUS_JOB_H

#include <stdbool.h>

#include "cmus.h"
#include "worker.h"

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

extern int job_fd;

void job_init(void);
void job_exit(void);
void job_schedule_add(int type, struct add_data *data);
void job_schedule_update(struct update_data *data);
void job_schedule_update_cache(int type, struct update_cache_data *data);
void job_handle_results(void);

#endif
