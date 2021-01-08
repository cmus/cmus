/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2004-2006 Timo Hirvonen
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

#ifndef CMUS_LIB_H
#define CMUS_LIB_H

#include "editable.h"
#include "search.h"
#include "track_info.h"
#include "expr.h"
#include "rbtree.h"

struct tree_track {
	struct shuffle_track shuffle_track;

	/* position in track search tree */
	struct rb_node tree_node;

	struct album *album;
};

static inline struct track_info *tree_track_info(const struct tree_track *track)
{
	return ((struct simple_track *)track)->info;
}

static inline struct tree_track *to_tree_track(const struct rb_node *node)
{
	return container_of(node, struct tree_track, tree_node);
}


struct album {
	/* position in album search tree */
	struct rb_node tree_node;

	/* root of track tree */
	struct rb_root track_root;

	struct artist *artist;
	char *name;
	char *sort_name;
	char *collkey_name;
	char *collkey_sort_name;
	/* max date of the tracks added to this album */
	int date;
	/* min date of the tracks added to this album */
	int min_date;
};

struct artist {
	/* position in artist search tree */
	struct rb_node tree_node;

	/* root of album tree */
	struct rb_root album_root;

	char *name;
	char *sort_name;
	char *auto_sort_name;
	char *collkey_name;
	char *collkey_sort_name;
	char *collkey_auto_sort_name;

	/* albums visible for this artist in the tree_win? */
	unsigned int expanded : 1;
	unsigned int is_compilation : 1;
};

const char *artist_sort_name(const struct artist *);

enum aaa_mode {
	AAA_MODE_ALL,
	AAA_MODE_ARTIST,
	AAA_MODE_ALBUM
};

extern struct editable lib_editable;
extern struct tree_track *lib_cur_track;
extern struct rb_root lib_shuffle_root;
extern enum aaa_mode aaa_mode;
extern unsigned int play_sorted;
extern char *lib_live_filter;

extern struct searchable *tree_searchable;
extern struct window *lib_tree_win;
extern struct window *lib_track_win;
extern struct window *lib_cur_win;
extern struct rb_root lib_artist_root;

#define CUR_ALBUM	(lib_cur_track->album)
#define CUR_ARTIST	(lib_cur_track->album->artist)

void lib_init(void);
void tree_init(void);
struct track_info *lib_goto_next(void);
struct track_info *lib_goto_prev(void);
void lib_add_track(struct track_info *track_info, void *opaque);
void lib_set_filter(struct expr *expr);
void lib_set_live_filter(const char *str);
void lib_set_add_filter(struct expr *expr);
int lib_remove(struct track_info *ti);
void lib_clear_store(void);
void lib_reshuffle(void);
void lib_set_view(int view);
int lib_for_each(int (*cb)(void *data, struct track_info *ti), void *data,
		void *opaque);
int lib_for_each_filtered(int (*cb)(void *data, struct track_info *ti),
		void *data, void *opaque);

struct tree_track *lib_find_track(struct track_info *ti);
struct track_info *lib_set_track(struct tree_track *track);
void lib_store_cur_track(struct track_info *ti);
struct track_info *lib_get_cur_stored_track(void);

struct tree_track *tree_get_selected(void);
struct track_info *tree_activate_selected(void);
void tree_sort_artists(void);
void tree_add_track(struct tree_track *track);
void tree_remove(struct tree_track *track);
void tree_remove_sel(void);
void tree_toggle_active_window(void);
void tree_toggle_expand_artist(void);
void tree_expand_matching(const char *text);
void tree_expand_all(void);
void tree_sel_update(int changed);
void tree_sel_current(int auto_expand_albums);
void tree_sel_first(void);
void tree_sel_track(struct tree_track *t, int auto_expand_albums);
int tree_for_each_sel(int (*cb)(void *data, struct track_info *ti), void *data, int reverse, int advance);
int _tree_for_each_sel(int (*cb)(void *data, struct track_info *ti), void *data, int reverse);

struct track_info *sorted_activate_selected(void);
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

static inline struct artist *to_artist(const struct rb_node *node)
{
	return container_of(node, struct artist, tree_node);
}

static inline struct album *to_album(const struct rb_node *node)
{
	return container_of(node, struct album, tree_node);
}

#endif
