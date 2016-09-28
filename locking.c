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

#include "locking.h"
#include "debug.h"

#include <string.h>
#include <stdatomic.h>
#include <stdbool.h>

struct fifo_waiter {
	struct fifo_waiter * _Atomic next;
	pthread_cond_t cond;
};

pthread_t main_thread;

void cmus_mutex_lock(pthread_mutex_t *mutex)
{
	int rc = pthread_mutex_lock(mutex);
	if (unlikely(rc))
		BUG("error locking mutex: %s\n", strerror(rc));
}

void cmus_mutex_unlock(pthread_mutex_t *mutex)
{
	int rc = pthread_mutex_unlock(mutex);
	if (unlikely(rc))
		BUG("error unlocking mutex: %s\n", strerror(rc));
}

void cmus_rwlock_rdlock(pthread_rwlock_t *lock)
{
	int rc = pthread_rwlock_rdlock(lock);
	if (unlikely(rc))
		BUG("error locking mutex: %s\n", strerror(rc));
}

void cmus_rwlock_wrlock(pthread_rwlock_t *lock)
{
	int rc = pthread_rwlock_wrlock(lock);
	if (unlikely(rc))
		BUG("error locking mutex: %s\n", strerror(rc));
}

void cmus_rwlock_unlock(pthread_rwlock_t *lock)
{
	int rc = pthread_rwlock_unlock(lock);
	if (unlikely(rc))
		BUG("error unlocking mutex: %s\n", strerror(rc));
}

void fifo_mutex_lock(struct fifo_mutex *fifo)
{
	struct fifo_waiter self = {
		.cond = PTHREAD_COND_INITIALIZER,
		.next = ATOMIC_VAR_INIT(NULL),
	};

	struct fifo_waiter *old_tail = atomic_exchange_explicit(&fifo->tail, &self,
			memory_order_relaxed);
	if (old_tail)
		atomic_store_explicit(&old_tail->next, &self, memory_order_release);

	cmus_mutex_lock(&fifo->mutex);
	if (old_tail) {
		while (fifo->head != &self)
			pthread_cond_wait(&self.cond, &fifo->mutex);
		pthread_cond_destroy(&self.cond);
	}

	struct fifo_waiter *self_addr = &self;
	bool was_tail = atomic_compare_exchange_strong_explicit(&fifo->tail, &self_addr,
			NULL, memory_order_relaxed, memory_order_relaxed);
	struct fifo_waiter *next = NULL;
	if (!was_tail) {
		while (!(next = atomic_load_explicit(&self.next, memory_order_consume)))
			;
	}
	fifo->head = next;
}

void fifo_mutex_unlock(struct fifo_mutex *fifo)
{
	if (fifo->head)
		pthread_cond_signal(&fifo->head->cond);
	cmus_mutex_unlock(&fifo->mutex);
}

void fifo_mutex_yield(struct fifo_mutex *fifo)
{
	if (fifo->head || atomic_load_explicit(&fifo->tail, memory_order_relaxed)) {
		fifo_mutex_unlock(fifo);
		fifo_mutex_lock(fifo);
	}
}
