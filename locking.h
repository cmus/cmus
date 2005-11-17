/* 
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _LOCKING_H
#define _LOCKING_H

#include <debug.h>

#include <pthread.h>
#include <string.h>

#if defined(_GNU_SOURCE) && DEBUG > 0

/* check if thread tries to lock mutext twice */

#define CMUS_MUTEX_INITIALIZER PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP

static inline void cmus_mutex_init(pthread_mutex_t *mutex)
{
	pthread_mutexattr_t attr;
	int rc;

	rc = pthread_mutexattr_init(&attr);
	if (rc)
		goto __error;
	rc = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
	if (rc)
		goto __error;
	rc = pthread_mutex_init(mutex, &attr);
	if (rc)
		goto __error;
	return;
__error:
	BUG("error initializing mutex: %s\n", strerror(rc));
}

#else

/* no checking */

#define CMUS_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER

static inline void cmus_mutex_init(pthread_mutex_t *mutex)
{
	int rc;

	rc = pthread_mutex_init(mutex, NULL);
	if (rc)
		BUG("error initializing mutex: %s\n", strerror(rc));
}

#endif

#define cmus_mutex_lock(mutex) \
do { \
	int _rc_; \
	_rc_ = pthread_mutex_lock(mutex); \
	if (_rc_) \
		BUG("error locking mutex: %s\n", strerror(_rc_)); \
} while (0)

#define cmus_mutex_unlock(mutex) \
do { \
	int _rc_; \
	_rc_ = pthread_mutex_unlock(mutex); \
	if (_rc_) \
		BUG("error unlocking mutex: %s\n", strerror(_rc_)); \
} while (0)

#endif
