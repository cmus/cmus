#include "debug.h"
#include "locking.h"
#include "delegate.h"

#include <stdlib.h>
#include <stdbool.h>

static pthread_t delegate_thread;
static pthread_mutex_t delegate_mutex = CMUS_MUTEX_INITIALIZER;
static pthread_cond_t delegate_cond = CMUS_COND_INITIALIZER;

static struct delegate_args_header *delegate_first;
static struct delegate_args_header *delegate_last;

static bool delegate_stop = false;

static void *delegate_loop(void *arg)
{
	cmus_mutex_lock(&delegate_mutex);

	while (true) {
		struct delegate_args_header *prev, *cmd = delegate_first;
		delegate_first = delegate_last = NULL;

		cmus_mutex_unlock(&delegate_mutex);

		while (cmd) {
			cmd->handler(cmd);
			prev = cmd;
			cmd = cmd->next;
			free(prev);
		}

		cmus_mutex_lock(&delegate_mutex);

		if (delegate_stop)
			break;
		if (!delegate_first)
			pthread_cond_wait(&delegate_cond, &delegate_mutex);
	}

	cmus_mutex_unlock(&delegate_mutex);

	return NULL;
}

void delegate_init(void)
{
	int rc = pthread_create(&delegate_thread, NULL, delegate_loop,
			NULL);
	BUG_ON(rc);
}

void delegate_exit(void)
{
	cmus_mutex_lock(&delegate_mutex);
	delegate_stop = true;
	cmus_mutex_unlock(&delegate_mutex);
	pthread_cond_signal(&delegate_cond);

	int rc = pthread_join(delegate_thread, NULL);
	BUG_ON(rc);
}

void delegate_add_cmd(struct delegate_args_header *header)
{
	cmus_mutex_lock(&delegate_mutex);
	if (delegate_last)
		delegate_last->next = header;
	else
		delegate_first = delegate_last = header;
	cmus_mutex_unlock(&delegate_mutex);

	pthread_cond_signal(&delegate_cond);
}
