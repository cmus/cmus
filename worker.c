/* 
 * Copyright 2005 Timo Hirvonen
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <worker.h>
#include <locking.h>
#include <list.h>
#include <utils.h>
#include <xmalloc.h>
#include <debug.h>

#include <stdlib.h>
#include <pthread.h>

struct worker_job {
	struct list_head node;
	/* >0, 0 is 'any' */
	int type;
	void (*cb)(void *data);
	void *data;
};

static LIST_HEAD(worker_job_head);
static pthread_mutex_t worker_mutex = CMUS_MUTEX_INITIALIZER;
static pthread_t worker_thread;
static int running = 1;
static int cancel_type = JOB_TYPE_NONE;
static struct worker_job *cur_job = NULL;

#define worker_lock() cmus_mutex_lock(&worker_mutex)
#define worker_unlock() cmus_mutex_unlock(&worker_mutex)

static void *worker_loop(void *arg)
{
	while (1) {
		worker_lock();
		if (list_empty(&worker_job_head)) {
			if (!running) {
				worker_unlock();
				return NULL;
			}
			worker_unlock();
			ms_sleep(100);
		} else {
			struct list_head *item = worker_job_head.next;
			uint64_t st, et;

			d_print("taking job\n");
			list_del(item);
			cur_job = list_entry(item, struct worker_job, node);
			worker_unlock();

			timer_get(&st);
			cur_job->cb(cur_job->data);
			timer_get(&et);
			timer_print("worker job", et - st);

			worker_lock();
			free(cur_job);
			cur_job = NULL;
			worker_unlock();
		}
	}
}

void worker_init(void)
{
	int rc;

	rc = pthread_create(&worker_thread, NULL, worker_loop, NULL);
	BUG_ON(rc);
}

void worker_exit(void)
{
	int rc;

	worker_lock();
	running = 0;
	worker_unlock();

	/* should always succeed */
	rc = pthread_join(worker_thread, NULL);
	BUG_ON(rc);
}

void worker_add_job(int type, void (*cb)(void *data), void *data)
{
	struct worker_job *job;

	BUG_ON(type == 0);
	BUG_ON(cb == NULL);

	job = xnew(struct worker_job, 1);
	job->type = type;
	job->cb = cb;
	job->data = data;

	worker_lock();
	list_add_tail(&job->node, &worker_job_head);
	worker_unlock();
}

void worker_remove_jobs(int type)
{
	struct list_head *item;

	BUG_ON(type == JOB_TYPE_NONE);

	worker_lock();
	cancel_type = type;
	item = worker_job_head.next;
	while (item != &worker_job_head) {
		struct worker_job *job = list_entry(item, struct worker_job, node);
		struct list_head *next = item->next;

		if (type == JOB_TYPE_ANY || type == job->type) {
			list_del(&job->node);
			free(job);
		}
		item = next;
	}
	while (cur_job && (type == JOB_TYPE_ANY || type == cur_job->type)) {
		/* wait current job to finish or cancel */
		worker_unlock();
		ms_sleep(50);
		worker_lock();
	}
	cancel_type = JOB_TYPE_NONE;
	worker_unlock();
}

/*
 * this is always called from the worker thread
 * cur_job is guaranteed to be non-NULL
 */
int worker_cancelling(void)
{
	int cancelling;

	BUG_ON(cur_job == NULL);
	cancelling = cancel_type == JOB_TYPE_ANY || cur_job->type == cancel_type;
	return cancelling;
}
