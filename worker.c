#include "worker.h"
#include "locking.h"
#include "list.h"
#include "utils.h"
#include "xmalloc.h"
#include "debug.h"

#include <stdlib.h>
#include <pthread.h>

struct worker_job {
	struct list_head node;
	/* >0, 0 is 'any' */
	int type;
	void (*job_cb)(void *data);
	void (*free_cb)(void *data);
	void *data;
};

static LIST_HEAD(worker_job_head);
static pthread_mutex_t worker_mutex = CMUS_MUTEX_INITIALIZER;
static pthread_t worker_thread;
static int running = 1;
static int cancel_type = JOB_TYPE_NONE;

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

			list_del(item);
			cur_job = container_of(item, struct worker_job, node);
			worker_unlock();

			timer_get(&st);
			cur_job->job_cb(cur_job->data);
			timer_get(&et);
			timer_print("worker job", et - st);

			worker_lock();
			cur_job->free_cb(cur_job->data);
			free(cur_job);
			cur_job = NULL;
			worker_unlock();
		}
	}
}

void worker_init(void)
{
	int rc = pthread_create(&worker_thread, NULL, worker_loop, NULL);

	BUG_ON(rc);
}

void worker_exit(void)
{
	worker_lock();
	running = 0;
	worker_unlock();

	pthread_join(worker_thread, NULL);
}

void worker_add_job(int type, void (*job_cb)(void *data),
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
	worker_unlock();
}

void worker_remove_jobs(int type)
{
	struct list_head *item;

	worker_lock();
	cancel_type = type;

	/* remove jobs of the specified type from the queue */
	item = worker_job_head.next;
	while (item != &worker_job_head) {
		struct worker_job *job = container_of(item, struct worker_job, node);
		struct list_head *next = item->next;

		if (type == JOB_TYPE_ANY || type == job->type) {
			list_del(&job->node);
			job->free_cb(job->data);
			free(job);
		}
		item = next;
	}

	/* wait current job to finish or cancel if it's of the specified type */
	while (cur_job && (type == JOB_TYPE_ANY || type == cur_job->type)) {
		worker_unlock();
		ms_sleep(50);
		worker_lock();
	}

	cancel_type = JOB_TYPE_NONE;
	worker_unlock();
}

int worker_has_job(int type)
{
	struct worker_job *job;
	int has_job = 0;

	worker_lock();
	list_for_each_entry(job, &worker_job_head, node) {
		if (type == JOB_TYPE_ANY || type == job->type) {
			has_job = 1;
			break;
		}
	}
	if (cur_job && (type == JOB_TYPE_ANY || type == cur_job->type))
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
	return cur_job->type == cancel_type || cancel_type == JOB_TYPE_ANY;
}
