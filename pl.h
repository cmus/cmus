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

extern pthread_mutex_t pl_mutex;
extern struct window *pl_win;
extern struct searchable *pl_searchable;
extern struct simple_track *pl_cur_track;
extern unsigned int pl_nr_tracks;
extern unsigned int pl_total_time;
extern unsigned int pl_nr_marked;
extern char **pl_sort_keys;

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
void pl_move_after(void);
void pl_move_before(void);
void pl_sel_current(void);

void pl_clear(void);
void pl_reshuffle(void);
void pl_mark(const char *filter);
void pl_unmark(void);
void pl_invert_marks(void);

int pl_for_each_sel(int (*cb)(void *data, struct track_info *ti), void *data, int reverse);
int pl_for_each(int (*cb)(void *data, struct track_info *ti), void *data);

#define pl_lock() cmus_mutex_lock(&pl_mutex)
#define pl_unlock() cmus_mutex_unlock(&pl_mutex)

#endif
