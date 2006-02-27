#include "locking.h"
#include "debug.h"

#include <string.h>

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
