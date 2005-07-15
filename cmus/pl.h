/* 
 * Copyright 2004-2005 Timo Hirvonen
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

#ifndef _PL_H
#define _PL_H

#include <comment.h>
#include <list.h>
#include <window.h>
#include <search.h>
#include <track_info.h>
#include <locking.h>
#include <debug.h>

#include <pthread.h>
#include <sys/time.h>

struct track {
	/* next/prev track in artist/album/track tree */
	struct list_head node;

	/* next/prev track in shuffle list */
	struct list_head shuffle_node;

	/* next/prev track in sorted list */
	struct list_head sorted_node;

	struct album *album;
	struct track_info *info;

	char *name;
	int disc;
	int num;
	unsigned int url : 1;
};

struct album {
	/* next/prev album */
	struct list_head node;

	/* list of tracks */
	struct list_head track_head;

	struct artist *artist;
	char *name;
};

struct artist {
	/* next/prev artist */
	struct list_head node;

	/* list of albums */
	struct list_head album_head;
	char *name;

	/* albums visible for this artist in the tree_win? */
	unsigned int expanded : 1;

	/* if streams == 1 && name == NULL then display <Stream> */
	//unsigned int streams : 1;
};

enum play_mode {
	PLAY_MODE_TREE,
	PLAY_MODE_SHUFFLE,
	PLAY_MODE_SORTED
};

enum playlist_mode {
	PLAYLIST_MODE_ALL,
	PLAYLIST_MODE_ARTIST,
	PLAYLIST_MODE_ALBUM
};

/* PLAY_QUEUE_VIEW and BROWSER_VIEW are defined in ui_curses.h */
#define TREE_VIEW    0
#define SHUFFLE_VIEW 1
#define SORTED_VIEW  2

struct playlist {
	struct list_head artist_head;
	struct list_head shuffle_head;
	struct list_head sorted_head;

	struct artist *cur_artist;
	struct album *cur_album;
	struct track *cur_track;

	/* for sorted window */
	char **sort_keys;

	struct window *tree_win;
	struct window *track_win;
	struct window *shuffle_win;
	struct window *sorted_win;
	enum { TREE_WIN, TRACK_WIN, SHUFFLE_WIN, SORTED_WIN } cur_win;
	enum play_mode play_mode;
	enum playlist_mode playlist_mode;

	unsigned int nr_tracks;
	unsigned int total_time;

	unsigned int status_changed : 1;
	unsigned int track_win_changed : 1;
	unsigned int tree_win_changed : 1;
	unsigned int shuffle_win_changed : 1;
	unsigned int sorted_win_changed : 1;

	unsigned int repeat : 1;

	pthread_mutex_t mutex;
};

extern struct playlist playlist;
extern struct searchable *tree_searchable;
extern struct searchable *shuffle_searchable;
extern struct searchable *sorted_searchable;

extern int pl_init(void);
extern int pl_exit(void);

/* set current track, these return track_info on success */
extern struct track_info *pl_set_next(void);
extern struct track_info *pl_set_prev(void);
extern struct track_info *pl_set_selected(void);

extern void pl_add_track(struct track_info *track_info);

extern void pl_set_sort_keys(char **keys);
extern void pl_clear(void);
extern void pl_remove(struct track_info *ti);
extern void pl_remove_sel(void);
extern void pl_toggle_expand_artist(void);

extern void pl_toggle_repeat(void);
extern void pl_toggle_playlist_mode(void);
extern void pl_toggle_play_mode(void);
extern void pl_set_repeat(int value);
extern void pl_set_playlist_mode(enum playlist_mode playlist_mode);
extern void pl_set_play_mode(enum play_mode play_mode);
extern void pl_get_status(int *repeat, enum playlist_mode *playlist_mode, enum play_mode *play_mode, int *total_time);

/* these are unlocked */
extern void __pl_set_view(int view);
extern int __pl_toggle_active_window(void);

extern void pl_sel_up(int rows);
extern void pl_sel_down(int rows);
extern void pl_sel_page_up(void);
extern void pl_sel_page_down(void);
extern void pl_sel_top(void);
extern void pl_sel_bottom(void);
extern void pl_sel_current(void);

extern void pl_set_tree_win_nr_rows(int nr_rows);
extern void pl_set_track_win_nr_rows(int nr_rows);
extern void pl_set_shuffle_win_nr_rows(int nr_rows);
extern void pl_set_sorted_win_nr_rows(int nr_rows);

extern void pl_reshuffle(void);

/*
 * Run callback @cb for each selected track. Quit if @cb returns non-zero value.
 *
 * @cb:      callback funtion
 * @data:    data argument for @cb
 * @reverse: use reverse order?
 *
 * Returns: return value of last @cb call or 0 if @cb not called at all.
 */
extern int pl_for_each_selected(int (*cb)(void *data, struct track_info *ti), void *data, int reverse);
extern int pl_for_each(int (*cb)(void *data, struct track_info *ti), void *data);

#define pl_lock() cmus_mutex_lock(&playlist.mutex)
#define pl_unlock() cmus_mutex_unlock(&playlist.mutex)

static inline struct track *iter_to_shuffle_track(const struct iter *iter)
{
	BUG_ON(iter->data0 != &playlist.shuffle_head);
	return iter->data1;
}

static inline struct track *iter_to_sorted_track(const struct iter *iter)
{
	BUG_ON(iter->data0 != &playlist.sorted_head);
	return iter->data1;
}

static inline struct artist *iter_to_artist(const struct iter *iter)
{
	BUG_ON(iter->data0 != &playlist.artist_head);
	return iter->data1;
}

static inline struct album *iter_to_album(const struct iter *iter)
{
	BUG_ON(iter->data0 != &playlist.artist_head);
	return iter->data2;
}

static inline struct track *iter_to_track(const struct iter *iter)
{
	return iter->data1;
}

#endif
