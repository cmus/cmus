/*
 * Copyright 2004-2006 Timo Hirvonen
 */

#ifndef LIB_H
#define LIB_H

#include "editable.h"
#include "search.h"
#include "track_info.h"
#include "expr.h"

#include <sys/time.h>

enum tree_sort_method {
	SORT_NORMAL=1,
	SORT_COMPILATION
};

struct tree_track {
	struct shuffle_track shuffle_track;
	struct list_head node;
	struct album *album;
	enum tree_sort_method tree_sort;
};

static inline struct track_info *tree_track_info(const struct tree_track *track)
{
	return ((struct simple_track *)track)->info;
}

static inline struct tree_track *to_tree_track(const struct list_head *item)
{
	return container_of(item, struct tree_track, node);
}

struct album {
	/* next/prev album */
	struct list_head node;

	/* list of tracks */
	struct list_head track_head;

	struct artist *artist;
	char *name;
	/* date of the first track added to this album */
	int date;
};

struct artist {
	/* next/prev artist */
	struct list_head node;

	/* list of albums */
	struct list_head album_head;
	char *name;

	/* albums visible for this artist in the tree_win? */
	unsigned int expanded : 1;
};

enum aaa_mode {
	AAA_MODE_ALL,
	AAA_MODE_ARTIST,
	AAA_MODE_ALBUM
};

extern struct editable lib_editable;
extern struct tree_track *lib_cur_track;
extern enum aaa_mode aaa_mode;
extern unsigned int play_sorted;

extern struct searchable *tree_searchable;
extern struct window *lib_tree_win;
extern struct window *lib_track_win;
extern struct window *lib_cur_win;
extern struct list_head lib_artist_head;

#define CUR_ALBUM	(lib_cur_track->album)
#define CUR_ARTIST	(lib_cur_track->album->artist)

void lib_init(void);
void tree_init(void);
struct track_info *lib_set_next(void);
struct track_info *lib_set_prev(void);
void lib_add_track(struct track_info *track_info);
void lib_set_filter(struct expr *expr);
int lib_remove(struct track_info *ti);
void lib_clear_store(void);
void lib_reshuffle(void);
void lib_set_view(int view);
int lib_for_each(int (*cb)(void *data, struct track_info *ti), void *data);

struct track_info *tree_set_selected(void);
void tree_sort_artists(void);
void tree_add_track(struct tree_track *track);
void tree_remove(struct tree_track *track);
void tree_remove_sel(void);
void tree_toggle_active_window(void);
void tree_toggle_expand_artist(void);
void tree_sel_current(void);
int tree_for_each_sel(int (*cb)(void *data, struct track_info *ti), void *data, int reverse);
int __tree_for_each_sel(int (*cb)(void *data, struct track_info *ti), void *data, int reverse);

struct track_info *sorted_set_selected(void);
void sorted_sel_current(void);

static inline struct tree_track *iter_to_sorted_track(const struct iter *iter)
{
	return iter->data1;
}

static inline struct artist *iter_to_artist(const struct iter *iter)
{
	return iter->data1;
}

static inline struct album *iter_to_album(const struct iter *iter)
{
	return iter->data2;
}

static inline struct tree_track *iter_to_tree_track(const struct iter *iter)
{
	return iter->data1;
}

static inline struct artist *to_artist(const struct list_head *item)
{
	return container_of(item, struct artist, node);
}

static inline struct album *to_album(const struct list_head *item)
{
	return container_of(item, struct album, node);
}

#endif
