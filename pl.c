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

#include <pl.h>
#include <utils.h>
#include <comment.h>
#include <list.h>
#include <xmalloc.h>
#include <window.h>
#include <search_mode.h>
#include <file.h>
#include <mergesort.h>
#include <debug.h>

#include <pthread.h>
#include <string.h>

static inline struct artist *to_artist(const struct list_head *item)
{
	return container_of(item, struct artist, node);
}

static inline struct album *to_album(const struct list_head *item)
{
	return container_of(item, struct album, node);
}

static inline struct track *to_track(const struct list_head *item)
{
	return container_of(item, struct track, node);
}

static inline struct track *to_shuffle(const struct list_head *item)
{
	return container_of(item, struct track, shuffle_node);
}

static inline struct track *to_sorted(const struct list_head *item)
{
	return container_of(item, struct track, sorted_node);
}

/* iterator {{{ */

/* tree (search) iterators {{{ */
static int tree_search_get_prev(struct iter *iter)
{
	struct list_head *head = iter->data0;
	struct track *track = iter->data1;
	struct artist *artist;
	struct album *album;

	BUG_ON(iter->data2);
	if (head == NULL)
		return 0;
	if (track == NULL) {
		/* head, get last track */
		if (head->prev == head) {
			/* empty, iter points to the head already */
			return 0;
		}
		artist = to_artist(head->prev);
		album = to_album(artist->album_head.prev);
		iter->data1 = to_track(album->track_head.prev);
		return 1;
	}
	/* prev track */
	if (track->node.prev == &track->album->track_head || search_restricted) {
		/* prev album */
		if (track->album->node.prev == &track->album->artist->album_head) {
			/* prev artist */
			if (track->album->artist->node.prev == &lib.artist_head)
				return 0;
			artist = to_artist(track->album->artist->node.prev);
			album = to_album(artist->album_head.prev);
			track = to_track(album->track_head.prev);
		} else {
			album = to_album(track->album->node.prev);
			track = to_track(album->track_head.prev);
		}
	} else {
		track = to_track(track->node.prev);
	}
	iter->data1 = track;
	return 1;
}

static int tree_search_get_next(struct iter *iter)
{
	struct list_head *head = iter->data0;
	struct track *track = iter->data1;
	struct artist *artist;
	struct album *album;

	BUG_ON(iter->data2);
	if (head == NULL)
		return 0;
	if (track == NULL) {
		/* head, get first track */
		if (head->next == head) {
			/* empty, iter points to the head already */
			return 0;
		}
		artist = to_artist(head->next);
		album = to_album(artist->album_head.next);
		iter->data1 = to_track(album->track_head.next);
		return 1;
	}
	/* next track */
	if (track->node.next == &track->album->track_head || search_restricted) {
		/* next album */
		if (track->album->node.next == &track->album->artist->album_head) {
			/* next artist */
			if (track->album->artist->node.next == &lib.artist_head)
				return 0;
			artist = to_artist(track->album->artist->node.next);
			album = to_album(artist->album_head.next);
			track = to_track(album->track_head.next);
		} else {
			album = to_album(track->album->node.next);
			track = to_track(album->track_head.next);
		}
	} else {
		track = to_track(track->node.next);
	}
	iter->data1 = track;
	return 1;
}
/* }}} */

/* tree window iterators {{{ */
static int tree_get_prev(struct iter *iter)
{
	struct list_head *head = iter->data0;
	struct artist *artist = iter->data1;
	struct album *album = iter->data2;

	BUG_ON(head == NULL);
	BUG_ON(artist == NULL && album != NULL);
	if (artist == NULL) {
		/* head, get last artist and/or album */
		if (head->prev == head) {
			/* empty, iter points to the head already */
			return 0;
		}
		artist = to_artist(head->prev);
		if (artist->expanded) {
			album = to_album(artist->album_head.prev);
		} else {
			album = NULL;
		}
		iter->data1 = artist;
		iter->data2 = album;
		return 1;
	}
	if (artist->expanded && album) {
		/* prev album */
		if (album->node.prev == &artist->album_head) {
			iter->data2 = NULL;
			return 1;
		} else {
			iter->data2 = to_album(album->node.prev);
			return 1;
		}
	}

	/* prev artist */
	if (artist->node.prev == &lib.artist_head) {
		iter->data1 = NULL;
		iter->data2 = NULL;
		return 0;
	}
	artist = to_artist(artist->node.prev);
	iter->data1 = artist;
	iter->data2 = NULL;
	if (artist->expanded) {
		/* last album */
		iter->data2 = to_album(artist->album_head.prev);
	}
	return 1;
}

static int tree_get_next(struct iter *iter)
{
	struct list_head *head = iter->data0;
	struct artist *artist = iter->data1;
	struct album *album = iter->data2;

	BUG_ON(head == NULL);
	BUG_ON(artist == NULL && album != NULL);
	if (artist == NULL) {
		/* head, get first artist */
		if (head->next == head) {
			/* empty, iter points to the head already */
			return 0;
		}
		iter->data1 = to_artist(head->next);
		iter->data2 = NULL;
		return 1;
	}
	if (artist->expanded) {
		/* next album */
		if (album == NULL) {
			/* first album */
			iter->data2 = to_album(artist->album_head.next);
			return 1;
		}
		if (album->node.next != &artist->album_head) {
			iter->data2 = to_album(album->node.next);
			return 1;
		}
	}

	/* next artist */
	if (artist->node.next == head) {
		iter->data1 = NULL;
		iter->data2 = NULL;
		return 0;
	}
	iter->data1 = to_artist(artist->node.next);
	iter->data2 = NULL;
	return 1;
}
/* }}} */

static GENERIC_ITER_PREV(track_get_prev, struct track, node)
static GENERIC_ITER_NEXT(track_get_next, struct track, node)

static GENERIC_ITER_PREV(sorted_get_prev, struct track, sorted_node)
static GENERIC_ITER_NEXT(sorted_get_next, struct track, sorted_node)

static inline void tree_search_track_to_iter(struct track *track, struct iter *iter)
{
	iter->data0 = &lib.artist_head;
	iter->data1 = track;
	iter->data2 = NULL;
}

static inline void sorted_track_to_iter(struct track *track, struct iter *iter)
{
	iter->data0 = &lib.sorted_head;
	iter->data1 = track;
	iter->data2 = NULL;
}

static inline void album_to_iter(struct album *album, struct iter *iter)
{
	iter->data0 = &lib.artist_head;
	iter->data1 = album->artist;
	iter->data2 = album;
}

static inline void artist_to_iter(struct artist *artist, struct iter *iter)
{
	iter->data0 = &lib.artist_head;
	iter->data1 = artist;
	iter->data2 = NULL;
}

static inline void track_to_iter(struct track *track, struct iter *iter)
{
	iter->data0 = &track->album->track_head;
	iter->data1 = track;
	iter->data2 = NULL;
}

/* iterator }}} */

struct library lib;
struct searchable *tree_searchable;
struct searchable *sorted_searchable;

/* these are called always library locked */

static inline void status_changed(void)
{
	lib.status_changed = 1;
}

static void all_wins_changed(void)
{
	lib.tree_win->changed = 1;
	lib.track_win->changed = 1;
	lib.sorted_win->changed = 1;
}

static void tree_sel_changed(void)
{
	struct iter sel;
	struct album *album;

	window_get_sel(lib.tree_win, &sel);
	album = iter_to_album(&sel);
	if (album == NULL) {
		window_set_empty(lib.track_win);
	} else {
		window_set_contents(lib.track_win, &album->track_head);
	}
}

/* search {{{ */

static int sorted_search_get_current(void *data, struct iter *iter)
{
	return window_get_sel(lib.sorted_win, iter);
}

static int tree_search_get_current(void *data, struct iter *iter)
{
	struct artist *artist;
	struct album *album;
	struct track *track;
	struct iter tmpiter;

	if (list_empty(&lib.artist_head))
		return 0;
	if (window_get_sel(lib.track_win, &tmpiter)) {
		track = iter_to_track(&tmpiter);
		tree_search_track_to_iter(track, iter);
		return 1;
	}

	/* artist not expanded. track_win is empty
	 * set tmp to the first track of the selected artist */
	window_get_sel(lib.tree_win, &tmpiter);
	artist = iter_to_artist(&tmpiter);
	album = to_album(artist->album_head.next);
	track = to_track(album->track_head.next);
	tree_search_track_to_iter(track, iter);
	return 1;
}

static int sorted_search_matches(void *data, struct iter *iter, const char *text)
{
	struct track *track;
	unsigned int flags = TI_MATCH_TITLE;

	if (!search_restricted)
		flags |= TI_MATCH_ARTIST | TI_MATCH_ALBUM;

	track = iter_to_sorted_track(iter);
	if (!track_info_matches(track->info, text, flags))
		return 0;
	window_set_sel(lib.sorted_win, iter);
	return 1;
}

static inline struct track *iter_to_tree_search_track(const struct iter *iter)
{
	BUG_ON(iter->data0 != &lib.artist_head);
	return iter->data1;
}

static int tree_search_matches(void *data, struct iter *iter, const char *text)
{
	struct track *track;
	struct iter tmpiter;
	unsigned int flags = TI_MATCH_ARTIST | TI_MATCH_ALBUM;

	if (!search_restricted)
		flags |= TI_MATCH_TITLE;
	track = iter_to_tree_search_track(iter);
	if (!track_info_matches(track->info, text, flags))
		return 0;
	track->album->artist->expanded = 1;
	album_to_iter(track->album, &tmpiter);
	window_set_sel(lib.tree_win, &tmpiter);

	track_to_iter(track, &tmpiter);
	window_set_sel(lib.track_win, &tmpiter);
	return 1;
}

static void search_lock(void *data)
{
	lib_lock();
}

static void search_unlock(void *data)
{
	lib_unlock();
}

static const struct searchable_ops tree_search_ops = {
	.lock = search_lock,
	.unlock = search_unlock,
	.get_prev = tree_search_get_prev,
	.get_next = tree_search_get_next,
	.get_current = tree_search_get_current,
	.matches = tree_search_matches
};

static const struct searchable_ops sorted_search_ops = {
	.lock = search_lock,
	.unlock = search_unlock,
	.get_prev = sorted_get_prev,
	.get_next = sorted_get_next,
	.get_current = sorted_search_get_current,
	.matches = sorted_search_matches
};

/* search }}} */

static int xstrcasecmp(const char *a, const char *b)
{
	if (a == NULL) {
		if (b == NULL)
			return 0;
		return -1;
	} else if (b == NULL) {
		return 1;
	}
	return u_strcasecmp(a, b);
}

static inline int album_selected(struct album *album)
{
	struct iter sel;

	if (window_get_sel(lib.tree_win, &sel))
		return album == iter_to_album(&sel);
	return 0;
}

static inline void tree_win_get_selected(struct artist **artist, struct album **album)
{
	struct iter sel;

	*artist = NULL;
	*album = NULL;
	if (window_get_sel(lib.tree_win, &sel)) {
		*artist = iter_to_artist(&sel);
		*album = iter_to_album(&sel);
	}
}

static int sorted_view_cmp(const struct list_head *a_head, const struct list_head *b_head)
{
	const struct track *a = to_sorted(a_head);
	const struct track *b = to_sorted(b_head);
	int i, res = 0;

	for (i = 0; lib.sort_keys[i]; i++) {
		const char *key = lib.sort_keys[i];
		const char *av, *bv;

		/* numeric compare for tracknumber and discnumber */
		if (strcmp(key, "tracknumber") == 0) {
			res = a->num - b->num;
			if (res)
				break;
		}
		if (strcmp(key, "discnumber") == 0) {
			res = a->disc - b->disc;
			if (res)
				break;
		}
		if (strcmp(key, "filename") == 0) {
			/* NOTE: filenames are not necessarily UTF-8 */
			res = strcasecmp(a->info->filename, b->info->filename);
			if (res)
				break;
		}
		av = comments_get_val(a->info->comments, key);
		bv = comments_get_val(b->info->comments, key);
		res = xstrcasecmp(av, bv);
		if (res)
			break;
	}
	return res;
}

static void sort_sorted_list(void)
{
	list_mergesort(&lib.sorted_head, sorted_view_cmp);
}

static void artist_free(struct artist *artist)
{
	free(artist->name);
	free(artist);
}

static void album_free(struct album *album)
{
	free(album->name);
	free(album);
}

static void track_free(struct track *track)
{
	/* don't unref track->info, it is still in the track info store */
	free(track->name);
	free(track);
}

/* adding artist/album/track {{{ */

static void find_artist_and_album(const char *artist_name,
		const char *album_name, struct artist **_artist,
		struct album **_album)
{
	struct artist *artist;
	struct album *album;

	list_for_each_entry(artist, &lib.artist_head, node) {
		int res;
		
		res = xstrcasecmp(artist->name, artist_name);
		if (res == 0) {
			*_artist = artist;
			list_for_each_entry(album, &artist->album_head, node) {
				res = xstrcasecmp(album->name, album_name);
				if (res == 0) {
					*_album = album;
					return;
				}
			}
			*_album = NULL;
			return;
		}
	}
	*_artist = NULL;
	*_album = NULL;
	return;
}

static struct artist *add_artist(const char *name)
{
	struct list_head *item;
	struct artist *artist;

	artist = xnew(struct artist, 1);
	artist->name = xxstrdup(name);
	list_init(&artist->album_head);
	artist->expanded = 0;
	list_for_each(item, &lib.artist_head) {
		struct artist *a = to_artist(item);

		if (xstrcasecmp(name, a->name) < 0)
			break;
	}
	/* add before item */
	list_add_tail(&artist->node, item);
	return artist;
}

static struct album *artist_add_album(struct artist *artist, const char *name, int date)
{
	struct list_head *item;
	struct album *album;

	album = xnew(struct album, 1);
	album->name = xxstrdup(name);
	album->date = date;
	list_init(&album->track_head);
	album->artist = artist;
	list_for_each(item, &artist->album_head) {
		struct album *a = to_album(item);

		if (date < a->date)
			break;
		if (date > a->date)
			continue;
		if (xstrcasecmp(name, a->name) < 0)
			break;
	}
	/* add before item */
	list_add_tail(&album->node, item);
	return album;
}

static void shuffle_list_add_track(struct track *track, int nr_tracks)
{
	struct list_head *item;
	int pos;
	
	pos = rand() % (nr_tracks + 1);
	item = &lib.shuffle_head;
	if (pos <= lib.nr_tracks / 2) {
		while (pos) {
			item = item->next;
			pos--;
		}
	} else {
		pos = lib.nr_tracks - pos;
		while (pos) {
			item = item->prev;
			pos--;
		}
	}
	/* add before item */
	list_add_tail(&track->shuffle_node, item);
}

static void sorted_list_add_track(struct track *track)
{
	struct list_head *item;

	/* It is _much_ faster to iterate in reverse order because playlist
	 * file is usually sorted.
	 */
	item = lib.sorted_head.prev;
	while (item != &lib.sorted_head) {
		if (sorted_view_cmp(&track->sorted_node, item) >= 0)
			break;
		item = item->prev;
	}
	/* add after item */
	list_add(&track->sorted_node, item);
}

static void album_add_track(struct album *album, struct track *track)
{
	struct list_head *item;

	track->album = album;
	list_for_each(item, &album->track_head) {
		struct track *t = to_track(item);

		if (track->disc < t->disc)
			break;
		if (track->disc == t->disc) {
			if (track->num == -1 || t->num == -1) {
				/* can't sort by track number => sort by filename */
				/* NOTE: filenames are not necessarily UTF-8 */
				if (strcasecmp(track->info->filename, t->info->filename) < 0)
					break;
			} else {
				if (track->num < t->num)
					break;
				if (track->num == t->num) {
					/* shouldn't happen */
					if (xstrcasecmp(track->name, t->name) < 0)
						break;
				}
			}
		}
	}
	/* add before item */
	list_add_tail(&track->node, item);
}

static struct track *track_new(struct track_info *ti)
{
	struct track *track;

	track = xnew(struct track, 1);
	track->info = ti;
	track->name = xxstrdup(comments_get_val(ti->comments, "title"));
	track->url = is_url(ti->filename);
	track->disc = comments_get_int(ti->comments, "discnumber");
	track->num = comments_get_int(ti->comments, "tracknumber");
	return track;
}

/* add track to view 1 */
static void tree_add_track(struct track *track)
{
	const struct track_info *ti = track->info;
	const char *album_name, *artist_name;
	struct artist *artist;
	struct album *album;
	int date;

	album_name = comments_get_val(ti->comments, "album");
	artist_name = comments_get_val(ti->comments, "artist");

	if (track->url && artist_name == NULL && album_name == NULL) {
		artist_name = "<Stream>";
		album_name = "<Stream>";
	}

	find_artist_and_album(artist_name, album_name, &artist, &album);
	if (album) {
		album_add_track(album, track);

		/* is the album where we added the track selected? */
		if (album_selected(album)) {
			/* update track window */
			window_changed(lib.track_win);
		}
	} else if (artist) {
		date = comments_get_int(ti->comments, "date");
		album = artist_add_album(artist, album_name, date);
		album_add_track(album, track);

		if (artist->expanded) {
			/* update tree window */
			window_changed(lib.tree_win);
			/* album is not selected => no need to update track_win */
		}
	} else {
		date = comments_get_int(ti->comments, "date");
		artist = add_artist(artist_name);
		album = artist_add_album(artist, album_name, date);
		album_add_track(album, track);

		window_changed(lib.tree_win);
	}
}

/* add track to views 1-3 */
static void views_add_track(struct track_info *ti)
{
	struct track *track;

	track = track_new(ti);

	tree_add_track(track);

	shuffle_list_add_track(track, lib.nr_tracks);

	sorted_list_add_track(track);
	window_changed(lib.sorted_win);

	if (track->info->duration != -1)
		lib.total_time += track->info->duration;
	lib.nr_tracks++;
	status_changed();
}

/* add track to views 1-3 lazily
 * call views_update() after all tracks added */
static void views_add_track_lazy(struct track_info *ti)
{
	struct track *track;

	track = track_new(ti);

	tree_add_track(track);
	shuffle_list_add_track(track, lib.nr_tracks);
	list_add(&track->sorted_node, &lib.sorted_head);

	if (track->info->duration != -1)
		lib.total_time += track->info->duration;
	lib.nr_tracks++;
}

/* call this after adding tracks using views_add_track_lazy() */
static void views_update(void)
{
	sort_sorted_list();

	if (lib.cur_win == lib.track_win)
		lib.cur_win = lib.tree_win;
	window_goto_top(lib.tree_win);

	window_changed(lib.sorted_win);

	window_goto_top(lib.sorted_win);

	status_changed();
}

struct fh_entry {
	struct fh_entry *next;

	/* ref count is increased when added to this hash
	 *
	 * library itself doesn't increment ref count for tracks it
	 * contains because when track is in the library views it is in
	 * this hash too
	 */
	struct track_info *ti;
};

#define FH_SIZE (1024)
static struct fh_entry *ti_hash[FH_SIZE] = { NULL, };

/* this is from glib */
static unsigned int str_hash(const char *str)
{
	unsigned int hash = 0;
	int i;

	for (i = 0; str[i]; i++)
		hash = (hash << 5) - hash + str[i];
	return hash;
}

static int hash_insert(struct track_info *ti)
{
	const char *filename = ti->filename;
	unsigned int pos = str_hash(filename) % FH_SIZE;
	struct fh_entry **entryp;
	struct fh_entry *e;

	entryp = &ti_hash[pos];
	e = *entryp;
	while (e) {
		if (strcmp(e->ti->filename, filename) == 0) {
			/* found, don't insert */
			return 0;
		}
		e = e->next;
	}

	e = xnew(struct fh_entry, 1);
	track_info_ref(ti);
	e->ti = ti;
	e->next = *entryp;
	*entryp = e;
	return 1;
}

static void hash_remove(struct track_info *ti)
{
	const char *filename = ti->filename;
	unsigned int pos = str_hash(filename) % FH_SIZE;
	struct fh_entry **entryp;

	entryp = &ti_hash[pos];
	while (1) {
		struct fh_entry *e = *entryp;

		BUG_ON(e == NULL);
		if (strcmp(e->ti->filename, filename) == 0) {
			*entryp = e->next;
			track_info_unref(e->ti);
			free(e);
			break;
		}
		entryp = &e->next;
	}
}

void lib_add_track(struct track_info *ti)
{
	lib_lock();
	if (!hash_insert(ti)) {
		/* duplicate files not allowed */
		lib_unlock();
		return;
	}
	if (lib.filter == NULL || expr_eval(lib.filter, ti))
		views_add_track(ti);
	lib_unlock();
}

/* adding artist/album/track }}} */

/* removing artist/album/track {{{ */

static void __shuffle_list_remove_track(struct track *track)
{
	list_del(&track->shuffle_node);
}
	
static void __sorted_list_remove_track(struct track *track)
{
	struct iter iter;

	sorted_track_to_iter(track, &iter);
	window_row_vanishes(lib.sorted_win, &iter);
	list_del(&track->sorted_node);
}

static void __album_remove_track(struct track *track)
{
	if (album_selected(track->album)) {
		struct iter iter;

		track_to_iter(track, &iter);
		window_row_vanishes(lib.track_win, &iter);
	}
	list_del(&track->node);
}

static void __artist_remove_album(struct album *album)
{
	if (album->artist->expanded) {
		struct iter iter;

		album_to_iter(album, &iter);
		window_row_vanishes(lib.tree_win, &iter);
	}
	list_del(&album->node);
}

static void __lib_remove_artist(struct artist *artist)
{
	struct iter iter;

	artist_to_iter(artist, &iter);
	window_row_vanishes(lib.tree_win, &iter);
	list_del(&artist->node);
}

static void remove_track(struct track *track)
{
	struct album *album = track->album;
	struct artist *sel_artist;
	struct album *sel_album;

	BUG_ON(lib.nr_tracks == 0);

	tree_win_get_selected(&sel_artist, &sel_album);

	__shuffle_list_remove_track(track);
	__sorted_list_remove_track(track);
	__album_remove_track(track);
	lib.nr_tracks--;

	if (track->info->duration != -1)
		lib.total_time -= track->info->duration;
	if (track == lib.cur_track) {
		lib.cur_artist = NULL;
		lib.cur_album = NULL;
		lib.cur_track = NULL;
	}

	track_free(track);

	if (list_empty(&album->track_head)) {
		struct artist *artist = album->artist;

		if (sel_album == album && lib.cur_win == lib.track_win)
			lib.cur_win = lib.tree_win;

		__artist_remove_album(album);
		album_free(album);

		if (list_empty(&artist->album_head)) {
			artist->expanded = 0;
			__lib_remove_artist(artist);
			artist_free(artist);
		}
	}
}

/* remove track from views and hash table and then free the track */
static void remove_and_free_track(struct track *track)
{
	struct track_info *ti = track->info;

	/* frees track */
	remove_track(track);
	/* frees ti (if ref count falls to zero) */
	hash_remove(ti);
}

static void remove_sel_artist(struct artist *artist)
{
	struct list_head *aitem, *ahead;

	ahead = &artist->album_head;
	aitem = ahead->next;
	while (aitem != ahead) {
		struct list_head *titem, *thead;
		struct list_head *anext = aitem->next;
		struct album *album = to_album(aitem);

		thead = &album->track_head;
		titem = thead->next;
		while (titem != thead) {
			struct list_head *tnext = titem->next;
			struct track *track = to_track(titem);

			remove_and_free_track(track);
			titem = tnext;
		}
		/* all tracks removed => album removed
		 * if the last album was removed then the artist was removed too
		 */
		aitem = anext;
	}
}

static void remove_sel_album(struct album *album)
{
	struct list_head *item, *head;

	head = &album->track_head;
	item = head->next;
	while (item != head) {
		struct list_head *next = item->next;
		struct track *track = to_track(item);

		remove_and_free_track(track);
		item = next;
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
	struct track *track;

	if (window_get_sel(lib.track_win, &sel)) {
		track = iter_to_track(&sel);
		BUG_ON(track == NULL);
		remove_and_free_track(track);
	}
}

static void sorted_win_remove_sel(void)
{
	struct iter sel;
	struct track *track;

	if (window_get_sel(lib.sorted_win, &sel)) {
		track = iter_to_sorted_track(&sel);
		BUG_ON(track == NULL);
		remove_and_free_track(track);
	}
}

static void clear_views(void)
{
	struct list_head *item, *head;

	head = &lib.artist_head;
	item = head->next;
	while (item != head) {
		struct list_head *aitem, *ahead;
		struct list_head *next = item->next;
		struct artist *artist = to_artist(item);

		ahead = &artist->album_head;
		aitem = ahead->next;
		while (aitem != ahead) {
			struct list_head *titem, *thead;
			struct list_head *anext = aitem->next;
			struct album *album = to_album(aitem);

			thead = &album->track_head;
			titem = thead->next;
			while (titem != thead) {
				struct list_head *tnext = titem->next;
				struct track *track = to_track(titem);

				remove_track(track);
				titem = tnext;
			}

			aitem = anext;
		}

		item = next;
	}
	status_changed();
}

static void clear_store(void)
{
	int i;

	for (i = 0; i < FH_SIZE; i++) {
		struct fh_entry *e, *next;

		e = ti_hash[i];
		while (e) {
			next = e->next;
			track_info_unref(e->ti);
			free(e);
			e = next;
		}
		ti_hash[i] = NULL;
	}
}

/* removing artist/album/track }}} */

static void __set_cur_first_track(void)
{
	lib.cur_artist = to_artist(lib.artist_head.next);
	lib.cur_album = to_album(lib.cur_artist->album_head.next);
	lib.cur_track = to_track(lib.cur_album->track_head.next);
}

static int play_mode_filter(const struct track *track)
{
	const struct album *album = track->album;

	if (lib.playlist_mode == PLAYLIST_MODE_ALBUM)
		return lib.cur_album == album;

	if (lib.playlist_mode == PLAYLIST_MODE_ARTIST)
		return lib.cur_artist == album->artist;

	/* PLAYLIST_MODE_ALL */
	return 1;
}

/* set next/prev track {{{ */

static int set_cur_next_normal(void)
{
	if (lib.cur_track == NULL) {
		__set_cur_first_track();
		return 0;
	}

	/* not last track of the album? */
	if (lib.cur_track->node.next != &lib.cur_album->track_head) {
		/* next track of the album */
		lib.cur_track = to_track(lib.cur_track->node.next);
		return 0;
	}

	if (lib.playlist_mode == PLAYLIST_MODE_ALBUM) {
		if (!lib.repeat)
			return -1;
		/* first track of the album */
		lib.cur_track = to_track(lib.cur_album->track_head.next);
		return 0;
	}	

	/* not last album of the artist? */
	if (lib.cur_album->node.next != &lib.cur_artist->album_head) {
		/* first track of the next album of the artist */
		lib.cur_album = to_album(lib.cur_album->node.next);
		lib.cur_track = to_track(lib.cur_album->track_head.next);
		return 0;
	}

	if (lib.playlist_mode == PLAYLIST_MODE_ARTIST) {
		if (!lib.repeat)
			return -1;
		/* first track of the first album of the artist */
		lib.cur_album = to_album(lib.cur_artist->album_head.next);
		lib.cur_track = to_track(lib.cur_album->track_head.next);
		return 0;
	}

	/* not last artist of the library? */
	if (lib.cur_artist->node.next != &lib.artist_head) {
		/* first track of the first album of the next artist */
		lib.cur_artist = to_artist(lib.cur_artist->node.next);
		lib.cur_album = to_album(lib.cur_artist->album_head.next);
		lib.cur_track = to_track(lib.cur_album->track_head.next);
		return 0;
	}

	if (!lib.repeat)
		return -1;

	/* first track */
	__set_cur_first_track();
	return 0;
}

static int set_cur_prev_normal(void)
{
	if (lib.cur_track == NULL) {
		__set_cur_first_track();
		return 0;
	}
	/* not first track of the album? */
	if (lib.cur_track->node.prev != &lib.cur_album->track_head) {
		/* prev track of the album */
		lib.cur_track = to_track(lib.cur_track->node.prev);
		return 0;
	}

	if (lib.playlist_mode == PLAYLIST_MODE_ALBUM) {
		if (!lib.repeat)
			return -1;
		/* last track of the album */
		lib.cur_track = to_track(lib.cur_album->track_head.prev);
		return 0;
	}	

	/* not first album of the artist? */
	if (lib.cur_album->node.prev != &lib.cur_artist->album_head) {
		/* last track of the prev album of the artist */
		lib.cur_album = to_album(lib.cur_album->node.prev);
		lib.cur_track = to_track(lib.cur_album->track_head.prev);
		return 0;
	}

	if (lib.playlist_mode == PLAYLIST_MODE_ARTIST) {
		if (!lib.repeat)
			return -1;
		/* last track of the last album of the artist */
		lib.cur_album = to_album(lib.cur_artist->album_head.prev);
		lib.cur_track = to_track(lib.cur_album->track_head.prev);
		return 0;
	}

	/* not first artist of the library? */
	if (lib.cur_artist->node.prev != &lib.artist_head) {
		/* last track of the last album of the prev artist */
		lib.cur_artist = to_artist(lib.cur_artist->node.prev);
		lib.cur_album = to_album(lib.cur_artist->album_head.prev);
		lib.cur_track = to_track(lib.cur_album->track_head.prev);
		return 0;
	}

	if (!lib.repeat)
		return -1;

	/* last track */
	lib.cur_artist = to_artist(lib.artist_head.prev);
	lib.cur_album = to_album(lib.cur_artist->album_head.prev);
	lib.cur_track = to_track(lib.cur_album->track_head.prev);
	return 0;
}

/* shuffle */

static void __set_cur_shuffle_track(struct list_head *item)
{
	struct track *track = to_shuffle(item);

	lib.cur_track = track;
	lib.cur_album = track->album;
	lib.cur_artist = track->album->artist;
}

static int set_cur_next_shuffle(void)
{
	struct list_head *item;

	if (lib.cur_track == NULL) {
		/* first in shuffle list */
		__set_cur_shuffle_track(lib.shuffle_head.next);
		return 0;
	}

	item = lib.cur_track->shuffle_node.next;
again:
	while (item != &lib.shuffle_head) {
		struct track *track = to_shuffle(item);

		if (play_mode_filter(track)) {
			__set_cur_shuffle_track(item);
			return 0;
		}
		item = item->next;
	}
	item = lib.shuffle_head.next;
	if (lib.repeat)
		goto again;
	return -1;
}

static int set_cur_prev_shuffle(void)
{
	struct list_head *item;

	if (lib.cur_track == NULL) {
		/* first in shuffle list */
		__set_cur_shuffle_track(lib.shuffle_head.next);
		return 0;
	}

	item = lib.cur_track->shuffle_node.prev;
again:
	while (item != &lib.shuffle_head) {
		struct track *track = to_shuffle(item);

		if (play_mode_filter(track)) {
			__set_cur_shuffle_track(item);
			return 0;
		}
		item = item->prev;
	}
	item = lib.shuffle_head.prev;
	if (lib.repeat)
		goto again;
	return -1;
}

/* sorted */

static void __set_cur_sorted_track(struct list_head *item)
{
	struct track *track = to_sorted(item);

	lib.cur_track = track;
	lib.cur_album = track->album;
	lib.cur_artist = track->album->artist;
}

static int set_cur_next_sorted(void)
{
	struct list_head *item;

	if (lib.cur_track == NULL) {
		/* first in sorted list */
		__set_cur_sorted_track(lib.sorted_head.next);
		return 0;
	}

	item = lib.cur_track->sorted_node.next;
again:
	while (item != &lib.sorted_head) {
		struct track *track = to_sorted(item);

		if (play_mode_filter(track)) {
			__set_cur_sorted_track(item);
			return 0;
		}
		item = item->next;
	}
	item = lib.sorted_head.next;
	if (lib.repeat)
		goto again;
	return -1;
}

static int set_cur_prev_sorted(void)
{
	struct list_head *item;

	if (lib.cur_track == NULL) {
		/* first in sorted list */
		__set_cur_sorted_track(lib.sorted_head.next);
		return 0;
	}

	item = lib.cur_track->sorted_node.prev;
again:
	while (item != &lib.sorted_head) {
		struct track *track = to_sorted(item);

		if (play_mode_filter(track)) {
			__set_cur_sorted_track(item);
			return 0;
		}
		item = item->prev;
	}
	item = lib.sorted_head.prev;
	if (lib.repeat)
		goto again;
	return -1;
}

/* set next/prev track }}} */

void lib_reshuffle(void)
{
	struct list_head *item;
	int count, i;

	lib_lock();
	item = lib.shuffle_head.next;
	count = lib.nr_tracks;
	list_init(&lib.shuffle_head);
	for (i = 0; i < count; i++) {
		struct list_head *next = item->next;
		struct track *track = to_shuffle(item);

		shuffle_list_add_track(track, i);
		item = next;
	}
	lib_unlock();
}

void lib_init(void)
{
	struct iter iter;

	cmus_mutex_init(&lib.mutex);

	list_init(&lib.artist_head);
	list_init(&lib.shuffle_head);
	list_init(&lib.sorted_head);
	lib.nr_tracks = 0;

	lib.cur_artist = NULL;
	lib.cur_album = NULL;
	lib.cur_track = NULL;

	lib.total_time = 0;
	lib.repeat = 0;
	lib.playlist_mode = PLAYLIST_MODE_ALL;
	lib.play_mode = PLAY_MODE_TREE;

	lib.sort_keys = xnew(char *, 1);
	lib.sort_keys[0] = NULL;

	lib.filter = NULL;

	lib.tree_win = window_new(tree_get_prev, tree_get_next);
	lib.track_win = window_new(track_get_prev, track_get_next);
	lib.sorted_win = window_new(sorted_get_prev, sorted_get_next);

	lib.tree_win->sel_changed = tree_sel_changed;

	window_set_empty(lib.track_win);
	window_set_contents(lib.tree_win, &lib.artist_head);
	window_set_contents(lib.sorted_win, &lib.sorted_head);
	lib.cur_win = lib.tree_win;

	srand(time(NULL));

	iter.data1 = NULL;
	iter.data2 = NULL;

	iter.data0 = &lib.artist_head;
	tree_searchable = searchable_new(NULL, &iter, &tree_search_ops);

	iter.data0 = &lib.sorted_head;
	sorted_searchable = searchable_new(NULL, &iter, &sorted_search_ops);
}

void lib_exit(void)
{
	lib_clear();
	searchable_free(tree_searchable);
	searchable_free(sorted_searchable);
	window_free(lib.tree_win);
	window_free(lib.track_win);
	window_free(lib.sorted_win);
	free_str_array(lib.sort_keys);
}

struct track_info *lib_set_next(void)
{
	struct track_info *info = NULL;
	int rc;

	lib_lock();
	if (list_empty(&lib.artist_head)) {
		BUG_ON(lib.cur_track != NULL);
		lib_unlock();
		return NULL;
	}
	if (lib.play_mode == PLAY_MODE_SHUFFLE) {
		rc = set_cur_next_shuffle();
	} else if (lib.play_mode == PLAY_MODE_SORTED) {
		rc = set_cur_next_sorted();
	} else {
		rc = set_cur_next_normal();
	}
	if (rc == 0) {
		info = lib.cur_track->info;
		track_info_ref(info);
		all_wins_changed();
	}
	lib_unlock();
	return info;
}

struct track_info *lib_set_prev(void)
{
	struct track_info *info = NULL;
	int rc;

	lib_lock();
	if (list_empty(&lib.artist_head)) {
		BUG_ON(lib.cur_track != NULL);
		lib_unlock();
		return NULL;
	}
	if (lib.play_mode == PLAY_MODE_SHUFFLE) {
		rc = set_cur_prev_shuffle();
	} else if (lib.play_mode == PLAY_MODE_SORTED) {
		rc = set_cur_prev_sorted();
	} else {
		rc = set_cur_prev_normal();
	}
	if (rc == 0) {
		info = lib.cur_track->info;
		track_info_ref(info);
		all_wins_changed();
	}
	lib_unlock();
	return info;
}

struct track_info *lib_set_selected(void)
{
	struct track_info *info;
	struct iter sel;
	struct track *track;

	lib_lock();
	if (list_empty(&lib.artist_head)) {
		lib_unlock();
		return NULL;
	}
	if (lib.cur_win == lib.sorted_win) {
		window_get_sel(lib.sorted_win, &sel);
		track = iter_to_sorted_track(&sel);
	} else {
		struct artist *artist;
		struct album *album;

		tree_win_get_selected(&artist, &album);
		if (album == NULL) {
			/* only artist selected, track window is empty
			 * => get first album of the selected artist and first track of that album
			 */
			album = to_album(artist->album_head.next);
			track = to_track(album->track_head.next);
		} else {
			window_get_sel(lib.track_win, &sel);
			track = iter_to_track(&sel);
		}
	}
	BUG_ON(track == NULL);
	lib.cur_track = track;
	lib.cur_album = lib.cur_track->album;
	lib.cur_artist = lib.cur_album->artist;
	info = lib.cur_track->info;
	track_info_ref(info);
	all_wins_changed();
	lib_unlock();
	return info;
}

void lib_set_sort_keys(char **keys)
{
	lib_lock();
	free_str_array(lib.sort_keys);
	lib.sort_keys = keys;
	sort_sorted_list();
	window_changed(lib.sorted_win);
	window_goto_top(lib.sorted_win);
	lib_unlock();
}

void lib_clear(void)
{
	lib_lock();
	clear_views();
	clear_store();
	lib_unlock();
}

void lib_set_filter(struct expr *filter)
{
	struct track_info *cur_ti = NULL;
	int i;

	lib_lock();

	/* try to save cur_track */
	if (lib.cur_track) {
		cur_ti = lib.cur_track->info;
		track_info_ref(cur_ti);
	}

	clear_views();

	if (lib.filter)
		expr_free(lib.filter);
	lib.filter = filter;

	for (i = 0; i < FH_SIZE; i++) {
		struct fh_entry *e;

		e = ti_hash[i];
		while (e) {
			struct track_info *ti = e->ti;

			if (filter == NULL || expr_eval(filter, ti))
				views_add_track_lazy(ti);
			e = e->next;
		}
	}
	views_update();

	/* restore cur_track */
	if (cur_ti) {
		struct track *track;

		list_for_each_entry(track, &lib.sorted_head, sorted_node) {
			if (strcmp(track->info->filename, cur_ti->filename) == 0) {
				lib.cur_track = track;
				lib.cur_album = track->album;
				lib.cur_artist = track->album->artist;
				break;
			}
		}
		track_info_unref(cur_ti);
	}

	lib_unlock();
}

void lib_remove(struct track_info *ti)
{
	const char *artist_name;
	const char *album_name;
	struct artist *artist;
	struct album *album;
	struct track *track;

	lib_lock();
	hash_remove(ti);

	artist_name = comments_get_val(ti->comments, "artist");
	album_name = comments_get_val(ti->comments, "album");
	find_artist_and_album(artist_name, album_name, &artist, &album);
	if (album == NULL) {
		d_print("album '%s' not found\n", album_name);
		lib_unlock();
		return;
	}
	list_for_each_entry(track, &album->track_head, node) {
		if (ti == track->info) {
			d_print("removing %s\n", ti->filename);
			/* remove only from the views */
			remove_track(track);
			break;
		}
	}
	lib_unlock();
}

void lib_toggle_expand_artist(void)
{
	lib_lock();
	if (lib.cur_win == lib.tree_win || lib.cur_win == lib.track_win) {
		struct iter sel;
		struct artist *artist;

		window_get_sel(lib.tree_win, &sel);
		artist = iter_to_artist(&sel);
		if (artist) {
			if (artist->expanded) {
				/* deselect album, select artist */
				artist_to_iter(artist, &sel);
				window_set_sel(lib.tree_win, &sel);

				artist->expanded = 0;
				lib.cur_win = lib.tree_win;
			} else {
				artist->expanded = 1;
			}
			window_changed(lib.tree_win);
		}
	}
	lib_unlock();
}

void lib_toggle_repeat(void)
{
	lib_lock();
	lib.repeat = lib.repeat ^ 1;
	status_changed();
	lib_unlock();
}

void lib_toggle_playlist_mode(void)
{
	lib_lock();
	lib.playlist_mode++;
	lib.playlist_mode %= 3;
	status_changed();
	lib_unlock();
}

void lib_toggle_play_mode(void)
{
	lib_lock();
	lib.play_mode++;
	lib.play_mode %= 3;
	status_changed();
	lib_unlock();
}

void __lib_set_view(int view)
{
	/* lib.tree_win or lib.track_win */
	static struct window *tree_view_active_win = NULL;

	BUG_ON(view < 0);
	BUG_ON(view > 2);

	if (view == TREE_VIEW) {
		if (lib.cur_win != lib.tree_win && lib.cur_win != lib.track_win)
			lib.cur_win = tree_view_active_win;
	} else {
		if (lib.cur_win == lib.tree_win || lib.cur_win == lib.track_win)
			tree_view_active_win = lib.cur_win;
		lib.cur_win = lib.sorted_win;
	}
}

void lib_toggle_active_window(void)
{
	lib_lock();
	if (lib.cur_win == lib.tree_win) {
		struct artist *artist;
		struct album *album;

		tree_win_get_selected(&artist, &album);
		if (album) {
			lib.cur_win = lib.track_win;
			lib.tree_win->changed = 1;
			lib.track_win->changed = 1;
		}
	} else if (lib.cur_win == lib.track_win) {
		lib.cur_win = lib.tree_win;
		lib.tree_win->changed = 1;
		lib.track_win->changed = 1;
	}
	lib_unlock();
}

void lib_remove_sel(void)
{
	lib_lock();
	if (lib.cur_win == lib.tree_win) {
		tree_win_remove_sel();
	} else if (lib.cur_win == lib.track_win) {
		track_win_remove_sel();
	} else if (lib.cur_win == lib.sorted_win) {
		sorted_win_remove_sel();
	}
	status_changed();
	lib_unlock();
}

void lib_sel_current(void)
{
	lib_lock();
	if (lib.cur_track) {
		struct iter iter;

		if (lib.cur_win == lib.sorted_win) {
			sorted_track_to_iter(lib.cur_track, &iter);
			window_set_sel(lib.sorted_win, &iter);
		} else {
			lib.cur_artist->expanded = 1;

			if (lib.cur_win != lib.track_win) {
				lib.cur_win = lib.track_win;
				lib.tree_win->changed = 1;
				lib.track_win->changed = 1;
			}

			album_to_iter(lib.cur_album, &iter);
			window_set_sel(lib.tree_win, &iter);

			track_to_iter(lib.cur_track, &iter);
			window_set_sel(lib.track_win, &iter);
		}
	}
	lib_unlock();
}

static int album_for_each_track(struct album *album, int (*cb)(void *data, struct track_info *ti),
		void *data, int reverse)
{
	struct track *track;
	int rc = 0;

	if (reverse) {
		list_for_each_entry_reverse(track, &album->track_head, node) {
			rc = cb(data, track->info);
			if (rc)
				break;
		}
	} else {
		list_for_each_entry(track, &album->track_head, node) {
			rc = cb(data, track->info);
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
	int rc = 0;

	if (reverse) {
		list_for_each_entry_reverse(album, &artist->album_head, node) {
			rc = album_for_each_track(album, cb, data, 1);
			if (rc)
				break;
		}
	} else {
		list_for_each_entry(album, &artist->album_head, node) {
			rc = album_for_each_track(album, cb, data, 0);
			if (rc)
				break;
		}
	}
	return rc;
}

int __lib_for_each_selected(int (*cb)(void *data, struct track_info *ti), void *data, int reverse)
{
	struct iter sel;
	struct track *track;
	int rc = 0;

	if (lib.cur_win == lib.tree_win) {
		if (window_get_sel(lib.tree_win, &sel)) {
			struct artist *artist;
			struct album *album;

			artist = iter_to_artist(&sel);
			album = iter_to_album(&sel);
			BUG_ON(artist == NULL);
			if (album == NULL) {
				rc = artist_for_each_track(artist, cb, data, reverse);
			} else {
				rc = album_for_each_track(album, cb, data, reverse);
			}
		}
	} else if (lib.cur_win == lib.track_win) {
		if (window_get_sel(lib.track_win, &sel)) {
			track = iter_to_track(&sel);
			BUG_ON(track == NULL);
			rc = cb(data, track->info);
		}
	} else if (lib.cur_win == lib.sorted_win) {
		if (window_get_sel(lib.sorted_win, &sel)) {
			track = iter_to_sorted_track(&sel);
			BUG_ON(track == NULL);
			rc = cb(data, track->info);
		}
	}
	return rc;
}

int lib_for_each_selected(int (*cb)(void *data, struct track_info *ti), void *data, int reverse)
{
	int rc;

	lib_lock();
	rc = __lib_for_each_selected(cb, data, reverse);
	lib_unlock();
	return rc;
}

static int ti_filename_cmp(const void *a, const void *b)
{
	const struct track_info *tia = *(const struct track_info **)a;
	const struct track_info *tib = *(const struct track_info **)b;

	return strcmp(tia->filename, tib->filename);
}

int lib_for_each(int (*cb)(void *data, struct track_info *ti), void *data)
{
	int i, rc = 0, count = 0, size = 1024;
	struct track_info **tis;

	tis = xnew(struct track_info *, size);

	lib_lock();

	/* collect all track_infos */
	for (i = 0; i < FH_SIZE; i++) {
		struct fh_entry *e;

		e = ti_hash[i];
		while (e) {
			if (count == size) {
				size *= 2;
				tis = xrenew(struct track_info *, tis, size);
			}
			tis[count++] = e->ti;
			e = e->next;
		}
	}

	/* sort them by filename and call cb for each */
	qsort(tis, count, sizeof(struct track_info *), ti_filename_cmp);
	for (i = 0; i < count; i++) {
		rc = cb(data, tis[i]);
		if (rc)
			break;
	}
	lib_unlock();

	free(tis);
	return rc;
}
