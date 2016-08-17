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

#include "cmus.h"

#define JOB_TYPE_LIB   1 << 0
#define JOB_TYPE_PL    1 << 1
#define JOB_TYPE_QUEUE 1 << 2

#define JOB_TYPE_ADD          1 << 16
#define JOB_TYPE_UPDATE       1 << 17
#define JOB_TYPE_UPDATE_CACHE 1 << 18
#define JOB_TYPE_DELETE       1 << 19

struct add_data {
	enum file_type type;
	char *name;
	add_ti_cb add;
	void *opaque;
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

struct pl_delete_data {
	struct playlist *pl;
	void (*cb)(struct playlist *);
};

extern int job_fd;

void job_init(void);
void job_exit(void);
void job_schedule_add(int type, struct add_data *data);
void job_schedule_update(struct update_data *data);
void job_schedule_update_cache(int type, struct update_cache_data *data);
void job_schedule_pl_delete(struct pl_delete_data *data);
void job_handle(void);

#endif
