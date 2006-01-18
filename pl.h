/*
 * Copyright 2006 Timo Hirvonen
 */

#ifndef PL_H
#define PL_H

#include "track_info.h"
#include "list.h"
#include "locking.h"
#include "iter.h"
#include "track.h"

struct pl_track {
	struct shuffle_track shuffle_track;

	unsigned int marked : 1;
};

static inline struct track_info *pl_track_info(const struct pl_track *track)
{
	return ((struct simple_track *)track)->info;
}

extern pthread_mutex_t pl_mutex;
extern struct window *pl_win;
extern struct searchable *pl_searchable;
extern struct pl_track *pl_cur_track;
extern unsigned int pl_nr_tracks;
extern unsigned int pl_total_time;
extern unsigned int pl_nr_marked;
extern char **pl_sort_keys;
/* extern unsigned int pl_status_changed; */

void pl_init(void);

/* set current track, these return track_info on success */
struct track_info *pl_set_next(void);
struct track_info *pl_set_prev(void);
struct track_info *pl_set_selected(void);

void pl_add_track(struct track_info *track_info);

void pl_set_sort_keys(char **keys);

/* bindable */
void pl_remove_sel(void);
void pl_toggle_mark(void);
void pl_sel_current(void);

void pl_clear(void);
void pl_reshuffle(void);

#define pl_lock() cmus_mutex_lock(&pl_mutex)
#define pl_unlock() cmus_mutex_unlock(&pl_mutex)

static inline struct pl_track *iter_to_pl_track(const struct iter *iter)
{
	return iter->data1;
}

#endif
