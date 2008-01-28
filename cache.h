#ifndef CACHE_H
#define CACHE_H

#include "track_info.h"
#include "locking.h"

extern pthread_mutex_t cache_mutex;

#define cache_lock() cmus_mutex_lock(&cache_mutex)
#define cache_unlock() cmus_mutex_unlock(&cache_mutex)

int cache_init(void);
int cache_close(void);
struct track_info *cache_get_ti(const char *filename);
void cache_remove_ti(struct track_info *ti);

#endif
