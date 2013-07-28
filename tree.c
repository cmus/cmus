/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2006 Timo Hirvonen
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

#include "lib.h"
#include "search_mode.h"
#include "xmalloc.h"
#include "utils.h"
#include "debug.h"
#include "mergesort.h"
#include "options.h"
#include "u_collate.h"
#include "rbtree.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

struct searchable *tree_searchable;
struct window *lib_tree_win;
struct window *lib_track_win;
struct window *lib_cur_win;
struct rb_root lib_artist_root;

static inline void tree_search_track_to_iter(struct tree_track *track, struct iter *iter)
{
	iter->data0 = &lib_artist_root;
	iter->data1 = track;
	iter->data2 = NULL;
}

static inline void album_to_iter(struct album *album, struct iter *iter)
{
	iter->data0 = &lib_artist_root;
	iter->data1 = album->artist;
	iter->data2 = album;
}

static inline void artist_to_iter(struct artist *artist, struct iter *iter)
{
	iter->data0 = &lib_artist_root;
	iter->data1 = artist;
	iter->data2 = NULL;
}

static inline void tree_track_to_iter(struct tree_track *track, struct iter *iter)
{
	iter->data0 = &track->album->track_root;
	iter->data1 = track;
	iter->data2 = NULL;
}

static void tree_set_expand_artist(struct artist *artist, int expand)
{
	struct iter sel;

	if (expand) {
		artist->expanded = 1;
	} else {
		/* deselect album, select artist */
		artist_to_iter(artist, &sel);
		window_set_sel(lib_tree_win, &sel);

		artist->expanded = 0;
		lib_cur_win = lib_tree_win;
	}
	window_changed(lib_tree_win);
}

/* tree (search) iterators {{{ */
static int tree_search_get_prev(struct iter *iter)
{
	struct rb_root *root = iter->data0;
	struct tree_track *track = iter->data1;
	struct artist *artist;
	struct album *album;

	BUG_ON(iter->data2);
	if (root == NULL)
		return 0;
	if (track == NULL) {
		/* head, get last track */
		if (rb_root_empty(root)) {
			/* empty, iter points to the head already */
			return 0;
		}
		artist = to_artist(rb_last(root));
		album = to_album(rb_last(&artist->album_root));
		iter->data1 = to_tree_track(rb_last(&album->track_root));
		return 1;
	}
	/* prev track */
	if (rb_prev(&track->tree_node) == NULL || search_restricted) {
		/* prev album */
		if (rb_prev(&track->album->tree_node) == NULL) {
			/* prev artist */
			if (rb_prev(&track->album->artist->tree_node) == NULL)
				return 0;
			artist = to_artist(rb_prev(&track->album->artist->tree_node));
			album = to_album(rb_last(&artist->album_root));
			track = to_tree_track(rb_last(&album->track_root));
		} else {
			album = to_album(rb_prev(&track->album->tree_node));
			track = to_tree_track(rb_last(&album->track_root));
		}
	} else {
		track = to_tree_track(rb_prev(&track->tree_node));
	}
	iter->data1 = track;
	return 1;
}

static int tree_search_get_next(struct iter *iter)
{
	struct rb_root *root = iter->data0;
	struct tree_track *track = iter->data1;
	struct artist *artist;
	struct album *album;

	BUG_ON(iter->data2);
	if (root == NULL)
		return 0;
	if (track == NULL) {
		/* head, get first track */
		if (rb_root_empty(root)) {
			/* empty, iter points to the head already */
			return 0;
		}
		artist = to_artist(rb_first(root));
		album = to_album(rb_first(&artist->album_root));
		iter->data1 = to_tree_track(rb_first(&album->track_root));
		return 1;
	}
	/* next track */
	if (rb_next(&track->tree_node) == NULL || search_restricted) {
		/* next album */
		if (rb_next(&track->album->tree_node) == NULL) {
			/* next artist */
			if (rb_next(&track->album->artist->tree_node) == NULL)
				return 0;
			artist = to_artist(rb_next(&track->album->artist->tree_node));
			album = to_album(rb_first(&artist->album_root));
			track = to_tree_track(rb_first(&album->track_root));
		} else {
			album = to_album(rb_next(&track->album->tree_node));
			track = to_tree_track(rb_first(&album->track_root));
		}
	} else {
		track = to_tree_track(rb_next(&track->tree_node));
	}
	iter->data1 = track;
	return 1;
}
/* }}} */

/* tree window iterators {{{ */
static int tree_get_prev(struct iter *iter)
{
	struct rb_root *root = iter->data0;
	struct artist *artist = iter->data1;
	struct album *album = iter->data2;

	BUG_ON(root == NULL);
	BUG_ON(artist == NULL && album != NULL);
	if (artist == NULL) {
		/* head, get last artist and/or album */
		if (rb_root_empty(root)) {
			/* empty, iter points to the head already */
			return 0;
		}
		artist = to_artist(rb_last(root));
		if (artist->expanded) {
			album = to_album(rb_last(&artist->album_root));
		} else {
			album = NULL;
		}
		iter->data1 = artist;
		iter->data2 = album;
		return 1;
	}
	if (artist->expanded && album) {
		/* prev album */
		if (rb_prev(&album->tree_node) == NULL) {
			iter->data2 = NULL;
			return 1;
		} else {
			iter->data2 = to_album(rb_prev(&album->tree_node));
			return 1;
		}
	}

	/* prev artist */
	if (rb_prev(&artist->tree_node) == NULL) {
		iter->data1 = NULL;
		iter->data2 = NULL;
		return 0;
	}
	artist = to_artist(rb_prev(&artist->tree_node));
	iter->data1 = artist;
	iter->data2 = NULL;
	if (artist->expanded) {
		/* last album */
		iter->data2 = to_album(rb_last(&artist->album_root));
	}
	return 1;
}

static int tree_get_next(struct iter *iter)
{
	struct rb_root *root = iter->data0;
	struct artist *artist = iter->data1;
	struct album *album = iter->data2;

	BUG_ON(root == NULL);
	BUG_ON(artist == NULL && album != NULL);
	if (artist == NULL) {
		/* head, get first artist */
		if (rb_root_empty(root)) {
			/* empty, iter points to the head already */
			return 0;
		}
		iter->data1 = to_artist(rb_first(root));
		iter->data2 = NULL;
		return 1;
	}
	if (artist->expanded) {
		/* next album */
		if (album == NULL) {
			/* first album */
			iter->data2 = to_album(rb_first(&artist->album_root));
			return 1;
		}
		if (rb_next(&album->tree_node) != NULL) {
			iter->data2 = to_album(rb_next(&album->tree_node));
			return 1;
		}
	}

	/* next artist */
	if (rb_next(&artist->tree_node) == NULL) {
		iter->data1 = NULL;
		iter->data2 = NULL;
		return 0;
	}
	iter->data1 = to_artist(rb_next(&artist->tree_node));
	iter->data2 = NULL;
	return 1;
}
/* }}} */

static GENERIC_TREE_ITER_PREV(tree_track_get_prev, struct tree_track, tree_node)
static GENERIC_TREE_ITER_NEXT(tree_track_get_next, struct tree_track, tree_node)

/* search (tree) {{{ */
static int tree_search_get_current(void *data, struct iter *iter)
{
	struct artist *artist;
	struct album *album;
	struct tree_track *track;
	struct iter tmpiter;

	if (rb_root_empty(&lib_artist_root))
		return 0;
	if (window_get_sel(lib_track_win, &tmpiter)) {
		track = iter_to_tree_track(&tmpiter);
		tree_search_track_to_iter(track, iter);
		return 1;
	}

	/* artist not expanded. track_win is empty
	 * set tmp to the first track of the selected artist */
	window_get_sel(lib_tree_win, &tmpiter);
	artist = iter_to_artist(&tmpiter);
	album = to_album(rb_first(&artist->album_root));
	track = to_tree_track(rb_first(&album->track_root));
	tree_search_track_to_iter(track, iter);
	return 1;
}

static inline struct tree_track *iter_to_tree_search_track(const struct iter *iter)
{
	BUG_ON(iter->data0 != &lib_artist_root);
	return iter->data1;
}

static int tree_search_matches(void *data, struct iter *iter, const char *text);

static const struct searchable_ops tree_search_ops = {
	.get_prev = tree_search_get_prev,
	.get_next = tree_search_get_next,
	.get_current = tree_search_get_current,
	.matches = tree_search_matches
};
/* search (tree) }}} */

static inline int album_selected(struct album *album)
{
	struct iter sel;

	if (window_get_sel(lib_tree_win, &sel))
		return album == iter_to_album(&sel);
	return 0;
}

static void tree_sel_changed(void)
{
	struct iter sel;
	struct album *album;

	window_get_sel(lib_tree_win, &sel);
	album = iter_to_album(&sel);
	if (album == NULL) {
		window_set_empty(lib_track_win);
	} else {
		window_set_contents(lib_track_win, &album->track_root);
	}
}

static inline void tree_win_get_selected(struct artist **artist, struct album **album)
{
	struct iter sel;

	*artist = NULL;
	*album = NULL;
	if (window_get_sel(lib_tree_win, &sel)) {
		*artist = iter_to_artist(&sel);
		*album = iter_to_album(&sel);
	}
}

static char *auto_artist_sort_name(const char *name)
{
	const char *name_orig = name;
	char *buf;

	if (strncasecmp(name, "the ", 4) != 0)
		return NULL;

	name += 4;
	while (isspace((int)*name))
		++name;

	if (*name == '\0')
		return NULL;

	buf = xnew(char, strlen(name_orig) + 2);
	sprintf(buf, "%s, %c%c%c", name, name_orig[0],
					 name_orig[1],
					 name_orig[2]);
	return buf;
}

static struct artist *artist_new(const char *name, const char *sort_name, int is_compilation)
{
	struct artist *a = xnew(struct artist, 1);

	a->name = xstrdup(name);
	a->sort_name = sort_name ? xstrdup(sort_name) : NULL;
	a->auto_sort_name = auto_artist_sort_name(name);
	a->collkey_name = u_strcasecoll_key(a->name);
	a->collkey_sort_name = u_strcasecoll_key0(a->sort_name);
	a->collkey_auto_sort_name = u_strcasecoll_key0(a->auto_sort_name);
	a->expanded = 0;
	a->is_compilation = is_compilation;
	rb_root_init(&a->album_root);

	return a;
}

static struct artist *artist_copy(const struct artist *artist)
{
	return artist_new(artist->name, artist->sort_name, artist->is_compilation);
}

static void artist_free(struct artist *artist)
{
	free(artist->name);
	free(artist->sort_name);
	free(artist->auto_sort_name);
	free(artist->collkey_name);
	free(artist->collkey_sort_name);
	free(artist->collkey_auto_sort_name);
	free(artist);
}

static struct album *album_new(struct artist *artist, const char *name,
		const char *sort_name, int date)
{
	struct album *album = xnew(struct album, 1);

	album->name = xstrdup(name);
	album->sort_name = sort_name ? xstrdup(sort_name) : NULL;
	album->collkey_name = u_strcasecoll_key(name);
	album->collkey_sort_name = u_strcasecoll_key0(sort_name);
	album->date = date;
	rb_root_init(&album->track_root);
	album->artist = artist;

	return album;
}

static void album_free(struct album *album)
{
	free(album->name);
	free(album->sort_name);
	free(album->collkey_name);
	free(album->collkey_sort_name);
	free(album);
}

void tree_init(void)
{
	struct iter iter;

	rb_root_init(&lib_artist_root);

	lib_tree_win = window_new(tree_get_prev, tree_get_next);
	lib_track_win = window_new(tree_track_get_prev, tree_track_get_next);
	lib_cur_win = lib_tree_win;

	lib_tree_win->sel_changed = tree_sel_changed;

	window_set_empty(lib_track_win);
	window_set_contents(lib_tree_win, &lib_artist_root);

	iter.data0 = &lib_artist_root;
	iter.data1 = NULL;
	iter.data2 = NULL;
	tree_searchable = searchable_new(NULL, &iter, &tree_search_ops);
}

struct tree_track *tree_get_selected(void)
{
	struct artist *artist;
	struct album *album;
	struct tree_track *track;
	struct iter sel;

	if (rb_root_empty(&lib_artist_root))
		return NULL;

	tree_win_get_selected(&artist, &album);
	if (album == NULL) {
		/* only artist selected, track window is empty
		 * => get first album of the selected artist and first track of that album
		 */
		album = to_album(rb_first(&artist->album_root));
		track = to_tree_track(rb_first(&album->track_root));
	} else {
		window_get_sel(lib_track_win, &sel);
		track = iter_to_tree_track(&sel);
	}

	return track;
}

struct track_info *tree_set_selected(void)
{
	struct track_info *info;

	lib_cur_track = tree_get_selected();
	if (!lib_cur_track)
		return NULL;

	lib_tree_win->changed = 1;
	lib_track_win->changed = 1;

	info = tree_track_info(lib_cur_track);
	track_info_ref(info);
	return info;
}

static int special_name_cmp(const char *a, const char *collkey_a,
		               const char *b, const char *collkey_b)
{
	/* keep <Stream> etc. top */
	int cmp = (*a != '<') - (*b != '<');

	if (cmp)
		return cmp;
	return strcmp(collkey_a, collkey_b);
}

static inline const char *album_sort_collkey(const struct album *a)
{
        if (a->sort_name)
                return a->collkey_sort_name;

        return a->collkey_name;
}

static int special_album_cmp(const struct album *a, const struct album *b)
{
	return special_name_cmp(a->name, album_sort_collkey(a), b->name, album_sort_collkey(b));
}

static int special_album_cmp_date(const struct album *a, const struct album *b)
{
	/* keep <Stream> etc. top */
	int cmp = (*a->name != '<') - (*b->name != '<');
	if (cmp)
		return cmp;

	cmp = a->date - b->date;
	if (cmp)
		return cmp;

	return strcmp(album_sort_collkey(a), album_sort_collkey(b));
}

/* has to follow the same logic as artist_sort_name() */
static inline const char *artist_sort_collkey(const struct artist *a)
{
        if (a->sort_name)
                return a->collkey_sort_name;

        if (smart_artist_sort && a->auto_sort_name)
                return a->collkey_auto_sort_name;

        return a->collkey_name;
}

static struct artist *do_find_artist(const struct artist *artist,
		                     struct rb_root *root,
				     struct rb_node ***p_new,
				     struct rb_node **p_parent)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	const char *a = artist_sort_name(artist);
	const char *collkey_a = artist_sort_collkey(artist);

	while (*new) {
		struct artist *cur_artist = to_artist(*new);
		const char *b = artist_sort_name(cur_artist);
		const char *collkey_b = artist_sort_collkey(cur_artist);
		int result = special_name_cmp(a, collkey_a, b, collkey_b);

		parent = *new;
		if (result < 0)
			new = &((*new)->rb_left);
		else if (result > 0)
			new = &((*new)->rb_right);
		else
			return cur_artist;
	}
	if (p_new)
		*p_new = new;
	if (p_parent)
		*p_parent = parent;
	return NULL;
}

/* search (tree) {{{ */
static struct artist *collapse_artist;

static int tree_search_matches(void *data, struct iter *iter, const char *text)
{
	struct tree_track *track;
	struct iter tmpiter;
	unsigned int flags = TI_MATCH_ARTIST | TI_MATCH_ALBUM | TI_MATCH_ALBUMARTIST;

	if (!search_restricted)
		flags |= TI_MATCH_TITLE;
	track = iter_to_tree_search_track(iter);
	if (!track_info_matches(tree_track_info(track), text, flags))
		return 0;

	/* collapse old search result */
	if (collapse_artist) {
		struct artist *artist = do_find_artist(collapse_artist, &lib_artist_root, NULL, NULL);
		if (artist && artist != track->album->artist) {
			if (artist->expanded)
				tree_set_expand_artist(artist, 0);
			artist_free(collapse_artist);
			collapse_artist = (!track->album->artist->expanded) ? artist_copy(track->album->artist) : NULL;
		}
	} else if (!track->album->artist->expanded)
		collapse_artist = artist_copy(track->album->artist);

	track->album->artist->expanded = 1;
	album_to_iter(track->album, &tmpiter);
	window_set_sel(lib_tree_win, &tmpiter);

	tree_track_to_iter(track, &tmpiter);
	window_set_sel(lib_track_win, &tmpiter);
	return 1;
}
/* search (tree) }}} */

static void insert_artist(struct artist *artist, struct rb_root *root)
{
	struct rb_node **new = &(root->rb_node), *parent = NULL;
	struct artist *found;

	found = do_find_artist(artist, root, &new, &parent);
	if (!found) {
		rb_link_node(&artist->tree_node, parent, new);
		rb_insert_color(&artist->tree_node, root);
	}
}

static void add_artist(struct artist *artist)
{
	insert_artist(artist, &lib_artist_root);
}

static struct artist *find_artist(const struct artist *artist)
{
	return do_find_artist(artist, &lib_artist_root, NULL, NULL);
}

static struct album *do_find_album(const struct album *album,
				   int (*cmp)(const struct album *, const struct album *),
				   struct rb_node ***p_new,
				   struct rb_node **p_parent)
{
	struct rb_node **new = &(album->artist->album_root.rb_node), *parent = NULL;

	while (*new) {
		struct album *a = to_album(*new);

		int result = cmp(album, a);

		parent = *new;
		if (result < 0)
			new = &((*new)->rb_left);
		else if (result > 0)
			new = &((*new)->rb_right);
		else
			return a;
	}
	if (p_new)
		*p_new = new;
	if (p_parent)
		*p_parent = parent;
	return NULL;
}

static struct album *find_album(const struct album *album)
{
	struct album *a;
	struct rb_node *tmp;

	/* do a linear search because we want find albums with different date */
	rb_for_each_entry(a, tmp, &album->artist->album_root, tree_node) {
		if (special_album_cmp(album, a) == 0)
			return a;
	}
	return NULL;
}

static void add_album(struct album *album)
{
	struct rb_node **new = &(album->artist->album_root.rb_node), *parent = NULL;
	struct album *found;

	/*
	 * Sort regular albums by date, but sort compilations
	 * alphabetically.
	 */
	found = do_find_album(album,
			      album->artist->is_compilation ? special_album_cmp
							    : special_album_cmp_date,
			      &new, &parent);
	if (!found) {
		rb_link_node(&album->tree_node, parent, new);
		rb_insert_color(&album->tree_node, &album->artist->album_root);
	}
}

static void album_add_track(struct album *album, struct tree_track *track)
{
	/*
	 * NOTE: This is not perfect.  You should ignore track numbers if
	 *       either is unset and use filename instead, but usually you
	 *       have all track numbers set or all unset (within one album
	 *       of course).
	 */
	static const sort_key_t album_track_sort_keys[] = {
		SORT_DISCNUMBER, SORT_TRACKNUMBER, SORT_FILENAME, SORT_INVALID
	};
	struct rb_node **new = &(album->track_root.rb_node), *parent = NULL;

	track->album = album;
	while (*new) {
		const struct simple_track *a = (const struct simple_track *) track;
		const struct simple_track *b = (const struct simple_track *) to_tree_track(*new);
		int result = track_info_cmp(a->info, b->info, album_track_sort_keys);

		parent = *new;
		if (result < 0)
			new = &((*new)->rb_left);
		else if (result > 0)
			new = &((*new)->rb_right);
		else
			/* only add to tree if not there already */
			return;
	}

	rb_link_node(&track->tree_node, parent, new);
	rb_insert_color(&track->tree_node, &album->track_root);
}

static const char *tree_artist_name(const struct track_info* ti)
{
	const char *val = ti->albumartist;

	if (ti->is_va_compilation)
		val = "<Various Artists>";
	if (!val || strcmp(val, "") == 0)
		val = "<No Name>";

	return val;
}

static const char *tree_album_name(const struct track_info* ti)
{
	const char *val = ti->album;

	if (!val || strcmp(val, "") == 0)
		val = "<No Name>";

	return val;
}

static void remove_album(struct album *album)
{
	if (album->artist->expanded) {
		struct iter iter;

		album_to_iter(album, &iter);
		window_row_vanishes(lib_tree_win, &iter);
	}
	rb_erase(&album->tree_node, &album->artist->album_root);
}

static void remove_artist(struct artist *artist)
{
	struct iter iter;

	artist_to_iter(artist, &iter);
	window_row_vanishes(lib_tree_win, &iter);
	rb_erase(&artist->tree_node, &lib_artist_root);
}

void tree_add_track(struct tree_track *track)
{
	const struct track_info *ti = tree_track_info(track);
	const char *album_name, *artist_name, *artistsort_name = NULL;
	const char *albumsort_name = NULL;
	struct artist *artist, *new_artist;
	struct album *album, *new_album;
	int date;
	int is_va_compilation = 0;

	date = ti->originaldate;
	if (date < 0)
		date = ti->date;

	if (is_http_url(ti->filename)) {
		artist_name = "<Stream>";
		album_name = "<Stream>";
	} else {
		album_name	= tree_album_name(ti);
		artist_name	= tree_artist_name(ti);
		artistsort_name	= ti->artistsort;
		albumsort_name	= ti->albumsort;

		is_va_compilation = ti->is_va_compilation;
	}

	new_artist = artist_new(artist_name, artistsort_name, is_va_compilation);
	album = NULL;

	artist = find_artist(new_artist);
	if (artist) {
		artist_free(new_artist);
		new_album = album_new(artist, album_name, albumsort_name, date);
		album = find_album(new_album);
		if (album)
			album_free(new_album);
	} else
		new_album = album_new(new_artist, album_name, albumsort_name, date);

	if (artist) {
		int changed = 0;
		/* If it makes sense to update sort_name, do it */
		if (!artist->sort_name && artistsort_name) {
			artist->sort_name = xstrdup(artistsort_name);
			artist->collkey_sort_name = u_strcasecoll_key(artistsort_name);
			changed = 1;
		}
		/* If names differ, update */
		if (!artist->auto_sort_name) {
			char *auto_sort_name = auto_artist_sort_name(artist_name);
			if (auto_sort_name) {
				free(artist->name);
				free(artist->collkey_name);
				artist->name = xstrdup(artist_name);
				artist->collkey_name = u_strcasecoll_key(artist_name);
				artist->auto_sort_name = auto_sort_name;
				artist->collkey_auto_sort_name = u_strcasecoll_key(auto_sort_name);
				changed = 1;
			}
		}
		if (changed) {
			remove_artist(artist);
			add_artist(artist);
			window_changed(lib_tree_win);
		}
	}

	if (album) {
		album_add_track(album, track);

		/* If it makes sense to update album date, do it */
		if (album->date < date) {
			album->date = date;

			remove_album(album);
			add_album(album);
			if (artist->expanded)
				window_changed(lib_tree_win);
		}

		if (album_selected(album))
			window_changed(lib_track_win);
	} else if (artist) {
		add_album(new_album);
		album_add_track(new_album, track);

		if (artist->expanded)
			window_changed(lib_tree_win);
	} else {
		add_artist(new_artist);
		add_album(new_album);
		album_add_track(new_album, track);

		window_changed(lib_tree_win);
	}
}

static void remove_sel_artist(struct artist *artist)
{
	struct rb_node *a_node, *a_tmp;

	rb_for_each_safe(a_node, a_tmp, &artist->album_root) {
		struct rb_node *t_node, *t_tmp;
		struct album *album = to_album(a_node);

		rb_for_each_safe(t_node, t_tmp, &album->track_root) {
			struct tree_track *track = to_tree_track(t_node);

			editable_remove_track(&lib_editable, (struct simple_track *)track);
		}
		/* all tracks removed => album removed
		 * if the last album was removed then the artist was removed too
		 */
	}
}

static void remove_sel_album(struct album *album)
{
	struct rb_node *node, *tmp;

	rb_for_each_safe(node, tmp, &album->track_root) {
		struct tree_track *track = to_tree_track(node);

		editable_remove_track(&lib_editable, (struct simple_track *)track);
	}
}

static void tree_win_remove_sel(void)
{
	struct artist *artist;
	struct album *album;

	tree_win_get_selected(&artist, &album);
	if (album) {
		remove_sel_album(album);
	} else if (artist) {
		remove_sel_artist(artist);
	}
}

static void track_win_remove_sel(void)
{
	struct iter sel;
	struct tree_track *track;

	if (window_get_sel(lib_track_win, &sel)) {
		track = iter_to_tree_track(&sel);
		BUG_ON(track == NULL);
		editable_remove_track(&lib_editable, (struct simple_track *)track);
	}
}

void tree_toggle_active_window(void)
{
	if (lib_cur_win == lib_tree_win) {
		struct artist *artist;
		struct album *album;

		tree_win_get_selected(&artist, &album);
		if (album) {
			lib_cur_win = lib_track_win;
			lib_tree_win->changed = 1;
			lib_track_win->changed = 1;
		}
	} else if (lib_cur_win == lib_track_win) {
		lib_cur_win = lib_tree_win;
		lib_tree_win->changed = 1;
		lib_track_win->changed = 1;
	}
}

void tree_toggle_expand_artist(void)
{
	struct iter sel;
	struct artist *artist;

	window_get_sel(lib_tree_win, &sel);
	artist = iter_to_artist(&sel);
	if (artist)
		tree_set_expand_artist(artist, !artist->expanded);
}

void tree_expand_matching(const char *text)
{
	struct artist *artist;
	struct rb_node *tmp1;
	int have_track_selected = 0;

	rb_for_each_entry(artist, tmp1, &lib_artist_root, tree_node) {
		struct album *album = NULL;
		struct rb_node *tmp2;
		int album_matched = 0;

		rb_for_each_entry(album, tmp2, &artist->album_root, tree_node) {
			struct tree_track *tree_track = to_tree_track(rb_first(&album->track_root));
			struct track_info *ti = ((struct simple_track *) tree_track)->info;
			album_matched = track_info_matches_full(ti, text, TI_MATCH_ALBUM, TI_MATCH_ARTIST | TI_MATCH_ALBUMARTIST, 0);
			if (album_matched)
				break;
		}
		artist->expanded = album_matched;
		if (!have_track_selected) {
			struct tree_track *tree_track;
			int track_matched = 0;

			if (!album)
				album = to_album(rb_first(&artist->album_root));

			rb_for_each_entry(tree_track, tmp2, &album->track_root, tree_node) {
				struct track_info *ti = ((struct simple_track *) tree_track)->info;
				track_matched = track_info_matches_full(ti, text, TI_MATCH_TITLE, 0, 0);
				if (track_matched)
					break;
			}
			if (album_matched || track_matched) {
				if (!tree_track)
					tree_track = to_tree_track(rb_first(&album->track_root));
				tree_sel_track(tree_track);
				have_track_selected = 1;
			}
		}
	}
	window_changed(lib_tree_win);
}

void tree_expand_all(void)
{
	struct artist *artist;
	struct rb_node *tmp;

	rb_for_each_entry(artist, tmp, &lib_artist_root, tree_node) {
		artist->expanded = 1;
	}
	window_changed(lib_tree_win);
}

static void remove_track(struct tree_track *track)
{
	if (album_selected(track->album)) {
		struct iter iter;

		tree_track_to_iter(track, &iter);
		window_row_vanishes(lib_track_win, &iter);
	}
	rb_erase(&track->tree_node, &track->album->track_root);
}

void tree_remove(struct tree_track *track)
{
	struct album *album = track->album;
	struct artist *sel_artist;
	struct album *sel_album;

	tree_win_get_selected(&sel_artist, &sel_album);

	remove_track(track);
	/* don't free the track */

	if (rb_root_empty(&album->track_root)) {
		struct artist *artist = album->artist;

		if (sel_album == album)
			lib_cur_win = lib_tree_win;

		remove_album(album);
		album_free(album);

		if (rb_root_empty(&artist->album_root)) {
			artist->expanded = 0;
			remove_artist(artist);
			artist_free(artist);
		}
	}
}

void tree_remove_sel(void)
{
	if (lib_cur_win == lib_tree_win) {
		tree_win_remove_sel();
	} else {
		track_win_remove_sel();
	}
}

void tree_sort_artists(void)
{
	struct rb_node *a_node, *a_tmp;

	rb_for_each_safe(a_node, a_tmp, &lib_artist_root) {
		struct rb_node *l_node, *l_tmp;
		struct artist *artist = to_artist(a_node);

		rb_for_each_safe(l_node, l_tmp, &artist->album_root) {
			struct rb_node *t_node, *t_tmp;
			struct album *album = to_album(l_node);

			rb_for_each_safe(t_node, t_tmp, &album->track_root) {
				struct tree_track *track = to_tree_track(t_node);

				tree_remove(track);
				tree_add_track(track);
			}
		}
	}
}

void tree_sel_current(void)
{
	tree_sel_track(lib_cur_track);
}

void tree_sel_first(void)
{
	if (!rb_root_empty(&lib_artist_root)) {
		struct artist *artist = to_artist(rb_first(&lib_artist_root));
		struct album *album = to_album(rb_first(&artist->album_root));
		struct tree_track *tree_track = to_tree_track(rb_first(&album->track_root));
		tree_sel_track(tree_track);
	}
}

void tree_sel_track(struct tree_track *t)
{
	if (t) {
		struct iter iter;

		t->album->artist->expanded = 1;

		if (lib_cur_win == lib_tree_win) {
			lib_cur_win = lib_track_win;
			lib_tree_win->changed = 1;
			lib_track_win->changed = 1;
		}

		album_to_iter(t->album, &iter);
		window_set_sel(lib_tree_win, &iter);

		tree_track_to_iter(t, &iter);
		window_set_sel(lib_track_win, &iter);
	}
}

static int album_for_each_track(struct album *album, int (*cb)(void *data, struct track_info *ti),
		void *data, int reverse)
{
	struct tree_track *track;
	struct rb_node *tmp;
	int rc = 0;

	if (reverse) {
		rb_for_each_entry_reverse(track, tmp, &album->track_root, tree_node) {
			rc = cb(data, tree_track_info(track));
			if (rc)
				break;
		}
	} else {
		rb_for_each_entry(track, tmp, &album->track_root, tree_node) {
			rc = cb(data, tree_track_info(track));
			if (rc)
				break;
		}
	}
	return rc;
}

static int artist_for_each_track(struct artist *artist, int (*cb)(void *data, struct track_info *ti),
		void *data, int reverse)
{
	struct album *album;
	struct rb_node *tmp;
	int rc = 0;

	if (reverse) {
		rb_for_each_entry_reverse(album, tmp, &artist->album_root, tree_node) {
			rc = album_for_each_track(album, cb, data, 1);
			if (rc)
				break;
		}
	} else {
		rb_for_each_entry(album, tmp, &artist->album_root, tree_node) {
			rc = album_for_each_track(album, cb, data, 0);
			if (rc)
				break;
		}
	}
	return rc;
}

int __tree_for_each_sel(int (*cb)(void *data, struct track_info *ti), void *data, int reverse)
{
	int rc = 0;

	if (lib_cur_win == lib_tree_win) {
		struct artist *artist;
		struct album *album;

		tree_win_get_selected(&artist, &album);
		if (artist) {
			if (album == NULL) {
				rc = artist_for_each_track(artist, cb, data, reverse);
			} else {
				rc = album_for_each_track(album, cb, data, reverse);
			}
		}
	} else {
		struct iter sel;
		struct tree_track *track;

		if (window_get_sel(lib_track_win, &sel)) {
			track = iter_to_tree_track(&sel);
			rc = cb(data, tree_track_info(track));
		}
	}
	return rc;
}

int tree_for_each_sel(int (*cb)(void *data, struct track_info *ti), void *data, int reverse)
{
	int rc = __tree_for_each_sel(cb, data, reverse);

	window_down(lib_cur_win, 1);
	return rc;
}
