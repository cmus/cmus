#ifndef _WORKER_H
#define _WORKER_H

#define JOB_TYPE_NONE	0
#define JOB_TYPE_ANY	-1

void worker_init(void);
void worker_exit(void);

/*
 * @type:     JOB_TYPE_* (>0)
 * @job_cb:   does the job
 * @free_cb:  frees @data
 * @data:     data for the callbacks
 */
void worker_add_job(int type, void (*job_cb)(void *data),
		void (*free_cb)(void *data), void *data);

/*
 * @type: job type. >0, use JOB_TYPE_ANY to remove all
 */
void worker_remove_jobs(int type);

/*
 * @type: type of this job
 *
 * returns: 0 or 1
 *
 * long jobs should call this to see whether it should cancel
 * call from job function _only_
 */
int worker_cancelling(void);

#endif
