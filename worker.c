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

#include "worker.h"
#include "locking.h"
#include "list.h"
#include "xmalloc.h"
#include "debug.h"
#include "job.h"

#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>

struct worker_job {
	struct list_head node;

	uint32_t type;
	void (*job_cb)(void *data);
	void (*free_cb)(void *data);
	void *data;
};

enum worker_state {
	WORKER_PAUSED,
	WORKER_RUNNING,
	WORKER_STOPPED,
};

static LIST_HEAD(worker_job_head);
static pthread_mutex_t worker_mutex = CMUS_MUTEX_INITIALIZER;
static pthread_cond_t worker_cond = PTHREAD_COND_INITIALIZER;
static pthread_t worker_thread;
static enum worker_state state = WORKER_PAUSED;
static int cancel_current = 0;

/*
 * - only worker thread modifies this
 * - cur_job->job_cb can read this without locking
 * - anyone else must lock worker before reading this
 */
static struct worker_job *cur_job = NULL;

#define worker_lock() cmus_mutex_lock(&worker_mutex)
#define worker_unlock() cmus_mutex_unlock(&worker_mutex)

static void *worker_loop(void *arg)
{
	srand(time(NULL));

	worker_lock();
	while (1) {
		if (state != WORKER_RUNNING || list_empty(&worker_job_head)) {
			int rc;

			if (state == WORKER_STOPPED)
				break;

			rc = pthread_cond_wait(&worker_cond, &worker_mutex);
			if (rc)
				d_print("pthread_cond_wait: %s\n", strerror(rc));
		} else {
			struct list_head *item = worker_job_head.next;
			uint64_t t;

			list_del(item);
			cur_job = container_of(item, struct worker_job, node);
			worker_unlock();

			t = timer_get();
			cur_job->job_cb(cur_job->data);
			timer_print("worker job", timer_get() - t);

			worker_lock();
			cur_job->free_cb(cur_job->data);
			free(cur_job);
			cur_job = NULL;

			// wakeup worker_remove_jobs_*() if needed
			if (cancel_current) {
				cancel_current = 0;
				pthread_cond_signal(&worker_cond);
			}
		}
	}
	worker_unlock();
	return NULL;
}

void worker_init(void)
{
	int rc = pthread_create(&worker_thread, NULL, worker_loop, NULL);

	BUG_ON(rc);
}

static void worker_set_state(enum worker_state s)
{
	worker_lock();
	state = s;
	pthread_cond_signal(&worker_cond);
	worker_unlock();
}

void worker_start(void)
{
	worker_set_state(WORKER_RUNNING);
}

void worker_exit(void)
{
	worker_set_state(WORKER_STOPPED);
	pthread_join(worker_thread, NULL);
}

void worker_add_job(uint32_t type, void (*job_cb)(void *data),
		void (*free_cb)(void *data), void *data)
{
	struct worker_job *job;

	job = xnew(struct worker_job, 1);
	job->type = type;
	job->job_cb = job_cb;
	job->free_cb = free_cb;
	job->data = data;

	worker_lock();
	list_add_tail(&job->node, &worker_job_head);
	pthread_cond_signal(&worker_cond);
	worker_unlock();
}

static int worker_matches_type(uint32_t type, void *job_data,
		void *opaque)
{
	uint32_t *pat = opaque;
	return !!(type & *pat);
}

void worker_remove_jobs_by_type(uint32_t pat)
{
	worker_remove_jobs_by_cb(worker_matches_type, &pat);
}

void worker_remove_jobs_by_cb(worker_match_cb cb, void *opaque)
{
	struct list_head *item;

	worker_lock();

	item = worker_job_head.next;
	while (item != &worker_job_head) {
		struct worker_job *job = container_of(item, struct worker_job,
				node);
		struct list_head *next = item->next;

		if (cb(job->type, job->data, opaque)) {
			list_del(&job->node);
			job->free_cb(job->data);
			free(job);
		}
		item = next;
	}

	/* wait current job to finish or cancel if it's of the specified type */
	if (cur_job && cb(cur_job->type, cur_job->data, opaque)) {
		cancel_current = 1;
		while (cancel_current)
			pthread_cond_wait(&worker_cond, &worker_mutex);
	}

	worker_unlock();
}

int worker_has_job_by_type(uint32_t pat)
{
	return worker_has_job_by_cb(worker_matches_type, &pat);
}

int worker_has_job_by_cb(worker_match_cb cb, void *opaque)
{
	struct worker_job *job;
	int has_job = 0;

	worker_lock();
	list_for_each_entry(job, &worker_job_head, node) {
		if (cb(job->type, job->data, opaque)) {
			has_job = 1;
			break;
		}
	}
	if (cur_job && cb(job->type, job->data, opaque))
		has_job = 1;
	worker_unlock();
	return has_job;
}

/*
 * this is only called from the worker thread
 * cur_job is guaranteed to be non-NULL
 */
int worker_cancelling(void)
{
	return cancel_current;
}
