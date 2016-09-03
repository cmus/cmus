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

#ifndef CMUS_WORKER_H
#define CMUS_WORKER_H

#include <stdint.h>

#define JOB_TYPE_NONE 0
#define JOB_TYPE_ANY  ~0

typedef int (*worker_match_cb)(uint32_t type, void *job_data, void *opaque);

void worker_init(void);
void worker_start(void);
void worker_exit(void);

void worker_add_job(uint32_t type, void (*job_cb)(void *job_data),
		void (*free_cb)(void *job_data), void *job_data);

/* NOTE: The callbacks below run in parallel with the job_cb function. Access to
 * job_data must by synchronized.
 */

void worker_remove_jobs_by_type(uint32_t pat);
void worker_remove_jobs_by_cb(worker_match_cb cb, void *opaque);

int worker_has_job_by_type(uint32_t pat);
int worker_has_job_by_cb(worker_match_cb cb, void *opaque);

int worker_cancelling(void);

#endif
