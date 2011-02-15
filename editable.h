/*
 * Copyright 2006 Timo Hirvonen
 */

#ifndef EDITABLE_H
#define EDITABLE_H

#include "window.h"
#include "list.h"
#include "rbtree.h"
#include "track.h"
#include "locking.h"

struct editable {
	struct window *win;
	struct list_head head;
	struct rb_root tree_root;
	unsigned int nr_tracks;
	unsigned int nr_marked;
	unsigned int total_time;
	sort_key_t *sort_keys;
	char sort_str[128];
	struct searchable *searchable;

	void (*free_track)(struct list_head *item);
};

extern pthread_mutex_t editable_mutex;

void editable_init(struct editable *e, void (*free_track)(struct list_head *item));
void editable_add(struct editable *e, struct simple_track *track);
void editable_remove_track(struct editable *e, struct simple_track *track);
void editable_remove_sel(struct editable *e);
void editable_sort(struct editable *e);
void editable_set_sort_keys(struct editable *e, sort_key_t *keys);
void editable_toggle_mark(struct editable *e);
void editable_move_after(struct editable *e);
void editable_move_before(struct editable *e);
void editable_clear(struct editable *e);
void editable_mark(struct editable *e, const char *filter);
void editable_unmark(struct editable *e);
void editable_invert_marks(struct editable *e);
int __editable_for_each_sel(struct editable *e, int (*cb)(void *data, struct track_info *ti),
		void *data, int reverse);
int editable_for_each_sel(struct editable *e, int (*cb)(void *data, struct track_info *ti),
		void *data, int reverse);

static inline void editable_track_to_iter(struct editable *e, struct simple_track *track, struct iter *iter)
{
	iter->data0 = &e->head;
	iter->data1 = track;
	iter->data2 = NULL;
}

#define editable_lock() cmus_mutex_lock(&editable_mutex)
#define editable_unlock() cmus_mutex_unlock(&editable_mutex)

#endif
