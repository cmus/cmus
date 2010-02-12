/*
 * Copyright 2006 Timo Hirvonen
 */

#include "lib.h"
#include "search_mode.h"
#include "xmalloc.h"
#include "comment.h"
#include "utils.h"
#include "debug.h"
#include "mergesort.h"
#include "options.h"

#include <ctype.h>
#include <stdio.h>

struct searchable *tree_searchable;
struct window *lib_tree_win;
struct window *lib_track_win;
struct window *lib_cur_win;
LIST_HEAD(lib_artist_head);

/* tree (search) iterators {{{ */
static int tree_search_get_prev(struct iter *iter)
{
	struct list_head *head = iter->data0;
	struct tree_track *track = iter->data1;
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
		iter->data1 = to_tree_track(album->track_head.prev);
		return 1;
	}
	/* prev track */
	if (track->node.prev == &track->album->track_head || search_restricted) {
		/* prev album */
		if (track->album->node.prev == &track->album->artist->album_head) {
			/* prev artist */
			if (track->album->artist->node.prev == &lib_artist_head)
				return 0;
			artist = to_artist(track->album->artist->node.prev);
			album = to_album(artist->album_head.prev);
			track = to_tree_track(album->track_head.prev);
		} else {
			album = to_album(track->album->node.prev);
			track = to_tree_track(album->track_head.prev);
		}
	} else {
		track = to_tree_track(track->node.prev);
	}
	iter->data1 = track;
	return 1;
}

static int tree_search_get_next(struct iter *iter)
{
	struct list_head *head = iter->data0;
	struct tree_track *track = iter->data1;
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
		iter->data1 = to_tree_track(album->track_head.next);
		return 1;
	}
	/* next track */
	if (track->node.next == &track->album->track_head || search_restricted) {
		/* next album */
		if (track->album->node.next == &track->album->artist->album_head) {
			/* next artist */
			if (track->album->artist->node.next == &lib_artist_head)
				return 0;
			artist = to_artist(track->album->artist->node.next);
			album = to_album(artist->album_head.next);
			track = to_tree_track(album->track_head.next);
		} else {
			album = to_album(track->album->node.next);
			track = to_tree_track(album->track_head.next);
		}
	} else {
		track = to_tree_track(track->node.next);
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
	if (artist->node.prev == &lib_artist_head) {
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

static GENERIC_ITER_PREV(tree_track_get_prev, struct tree_track, node)
static GENERIC_ITER_NEXT(tree_track_get_next, struct tree_track, node)

static inline void tree_search_track_to_iter(struct tree_track *track, struct iter *iter)
{
	iter->data0 = &lib_artist_head;
	iter->data1 = track;
	iter->data2 = NULL;
}

static inline void album_to_iter(struct album *album, struct iter *iter)
{
	iter->data0 = &lib_artist_head;
	iter->data1 = album->artist;
	iter->data2 = album;
}

static inline void artist_to_iter(struct artist *artist, struct iter *iter)
{
	iter->data0 = &lib_artist_head;
	iter->data1 = artist;
	iter->data2 = NULL;
}

static inline void tree_track_to_iter(struct tree_track *track, struct iter *iter)
{
	iter->data0 = &track->album->track_head;
	iter->data1 = track;
	iter->data2 = NULL;
}

/* search (tree) {{{ */
static int tree_search_get_current(void *data, struct iter *iter)
{
	struct artist *artist;
	struct album *album;
	struct tree_track *track;
	struct iter tmpiter;

	if (list_empty(&lib_artist_head))
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
	album = to_album(artist->album_head.next);
	track = to_tree_track(album->track_head.next);
	tree_search_track_to_iter(track, iter);
	return 1;
}

static inline struct tree_track *iter_to_tree_search_track(const struct iter *iter)
{
	BUG_ON(iter->data0 != &lib_artist_head);
	return iter->data1;
}

static int tree_search_matches(void *data, struct iter *iter, const char *text)
{
	struct tree_track *track;
	struct iter tmpiter;
	unsigned int flags = TI_MATCH_ARTIST | TI_MATCH_ALBUM;

	if (!search_restricted)
		flags |= TI_MATCH_TITLE;
	track = iter_to_tree_search_track(iter);
	if (!track_info_matches(tree_track_info(track), text, flags))
		return 0;
	track->album->artist->expanded = 1;
	album_to_iter(track->album, &tmpiter);
	window_set_sel(lib_tree_win, &tmpiter);

	tree_track_to_iter(track, &tmpiter);
	window_set_sel(lib_track_win, &tmpiter);
	return 1;
}

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
		window_set_contents(lib_track_win, &album->track_head);
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

static void artist_free(struct artist *artist)
{
	free(artist->name);
	free(artist->sort_name);
	free(artist->auto_sort_name);
	free(artist);
}

static void album_free(struct album *album)
{
	free(album->name);
	free(album);
}

void tree_init(void)
{
	struct iter iter;

	list_init(&lib_artist_head);

	lib_tree_win = window_new(tree_get_prev, tree_get_next);
	lib_track_win = window_new(tree_track_get_prev, tree_track_get_next);
	lib_cur_win = lib_tree_win;

	lib_tree_win->sel_changed = tree_sel_changed;

	window_set_empty(lib_track_win);
	window_set_contents(lib_tree_win, &lib_artist_head);

	iter.data0 = &lib_artist_head;
	iter.data1 = NULL;
	iter.data2 = NULL;
	tree_searchable = searchable_new(NULL, &iter, &tree_search_ops);
}

struct track_info *tree_set_selected(void)
{
	struct artist *artist;
	struct album *album;
	struct track_info *info;
	struct iter sel;

	if (list_empty(&lib_artist_head))
		return NULL;

	tree_win_get_selected(&artist, &album);
	if (album == NULL) {
		/* only artist selected, track window is empty
		 * => get first album of the selected artist and first track of that album
		 */
		album = to_album(artist->album_head.next);
		lib_cur_track = to_tree_track(album->track_head.next);
	} else {
		window_get_sel(lib_track_win, &sel);
		lib_cur_track = iter_to_tree_track(&sel);
	}

	lib_tree_win->changed = 1;
	lib_track_win->changed = 1;

	info = tree_track_info(lib_cur_track);
	track_info_ref(info);
	return info;
}

static void find_artist_and_album(const char *artist_name,
		const char *album_name, struct artist **_artist,
		struct album **_album)
{
	struct artist *artist;
	struct album *album;

	list_for_each_entry(artist, &lib_artist_head, node) {
		int res;

		res = u_strcasecmp(artist->name, artist_name);
		if (res == 0) {
			*_artist = artist;
			list_for_each_entry(album, &artist->album_head, node) {
				res = u_strcasecmp(album->name, album_name);
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

static int special_name_cmp(const char *a, const char *b)
{
	/* keep <Stream> etc. top */
	int cmp = (*a != '<') - (*b != '<');

	if (cmp)
		return cmp;
	return u_strcasecmp(a, b);
}

static int special_album_cmp(const struct album *a, const struct album *b)
{
	/* keep <Stream> etc. top */
	int cmp = (*a->name != '<') - (*b->name != '<');

	if (cmp)
		return cmp;

	if (a->date != b->date)
		return a->date - b->date;

	return u_strcasecmp(a->name, b->name);
}

static void insert_artist(struct artist *artist)
{
	const char *a = artist_sort_name(artist);
	struct list_head *item;

	list_for_each(item, &lib_artist_head) {
		const char *b = artist_sort_name(to_artist(item));

		if (special_name_cmp(a, b) < 0)
			break;
	}
	/* add before item */
	list_add_tail(&artist->node, item);
}

static int artist_cmp(const struct list_head *a, const struct list_head *b)
{
	return special_name_cmp(artist_sort_name(to_artist(a)),
				artist_sort_name(to_artist(b)));
}

void tree_sort_artists(void)
{
	list_mergesort(&lib_artist_head, artist_cmp);

	window_changed(lib_tree_win);
}

static const char *auto_artist_sort_name(const char *name)
{
	const char *name_orig = name;
	char *buf;

	if (strncasecmp(name, "the ", 4) != 0)
		return xstrdup(name);

	name += 4;
	while (isspace(*name))
		++name;

	if (*name == '\0')
		return xstrdup(name_orig);

	buf = xnew(char, strlen(name_orig) + 2);
	sprintf(buf, "%s, %c%c%c", name, name_orig[0],
					 name_orig[1],
					 name_orig[2]);
	return buf;
}

static struct artist *add_artist(const char *name, const char *sort_name)
{
	struct artist *a = xnew(struct artist, 1);

	a->name = xstrdup(name);
	a->sort_name = sort_name ? xstrdup(sort_name) : NULL;
	a->auto_sort_name = auto_artist_sort_name(name);
	a->expanded = 0;
	list_init(&a->album_head);

	insert_artist(a);

	return a;
}

static struct album *artist_add_album(struct artist *artist, const char *name, int date, int is_compilation)
{
	struct list_head *item;
	struct album *album;

	album = xnew(struct album, 1);
	album->name = xstrdup(name);
	album->date = date;
	list_init(&album->track_head);
	album->artist = artist;

	/*
	 * Sort regular albums by date, but sort compilations
	 * alphabetically.
	 */
	if (is_compilation) {
		list_for_each(item, &artist->album_head) {
			struct album *a = to_album(item);

			if (special_name_cmp(name, a->name) < 0)
				break;
		}
	} else {
		list_for_each(item, &artist->album_head) {
			struct album *a = to_album(item);

			if (special_album_cmp(album, a) < 0)
				break;
		}
	}

	/* add before item */
	list_add_tail(&album->node, item);
	return album;
}

static void album_add_track(struct album *album, struct tree_track *track)
{
	/*
	 * NOTE: This is not perfect.  You should ignore track numbers if
	 *       either is unset and use filename instead, but usually you
	 *       have all track numbers set or all unset (within one album
	 *       of course).
	 */
	static const char * const album_track_sort_keys[] = {
		"discnumber", "tracknumber", "filename", NULL
	};
	struct list_head *item;

	track->album = album;
	list_for_each(item, &album->track_head) {
		const struct simple_track *a = (const struct simple_track *)track;
		const struct simple_track *b = (const struct simple_track *)to_tree_track(item);

		if (track_info_cmp(a->info, b->info, album_track_sort_keys) < 0)
			break;
	}
	/* add before item */
	list_add_tail(&track->node, item);
}

void tree_add_track(struct tree_track *track)
{
	const struct track_info *ti = tree_track_info(track);
	const char *album_name, *artist_name, *artistsort_name = NULL;
	struct artist *artist;
	struct album *album;
	int date;
	int is_compilation = 0;

	date = comments_get_date(ti->comments, "date");

	if (is_url(ti->filename)) {
		artist_name = "<Stream>";
		artistsort_name = "<Stream>";
		album_name = "<Stream>";
	} else {
		album_name = keyvals_get_val(ti->comments, "album");
		if (!album_name || strcmp(album_name, "") == 0)
			album_name = "<No Name>";

		artist_name = keyvals_get_val(ti->comments, "albumartist");

		if (!artist_name && track_info_is_compilation(ti)) {
			artist_name = "<Compilations>";
			is_compilation = 1;
		}

		if (!artist_name)
			artist_name = keyvals_get_val(ti->comments, "artist");

		if (!artist_name || strcmp(artist_name, "") == 0)
			artist_name = "<No Name>";

		artistsort_name = keyvals_get_val(ti->comments, "albumartistsort");
		if (!artistsort_name)
			artistsort_name = keyvals_get_val(ti->comments, "artistsort");
	}

	find_artist_and_album(artist_name, album_name, &artist, &album);

	if (artist) {
		/* If it makes sense to update sort_name, do it */
		if (!artist->sort_name && artistsort_name) {
			artist->sort_name = xstrdup(artistsort_name);
			window_changed(lib_tree_win);
		}
	}

	if (album) {
		album_add_track(album, track);

		/* If it makes sense to update album date, do it */
		if (date != -1 && album->date == -1) {
			album->date = date;
			window_changed(lib_tree_win);
		}

		if (album_selected(album))
			window_changed(lib_track_win);
	} else if (artist) {
		album = artist_add_album(artist, album_name, date, is_compilation);
		album_add_track(album, track);

		if (artist->expanded)
			/* album is not selected => no need to update track_win */
			window_changed(lib_tree_win);
	} else {
		artist = add_artist(artist_name, artistsort_name);
		album = artist_add_album(artist, album_name, date, is_compilation);
		album_add_track(album, track);

		window_changed(lib_tree_win);
	}
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
			struct tree_track *track = to_tree_track(titem);

			editable_remove_track(&lib_editable, (struct simple_track *)track);
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
		struct tree_track *track = to_tree_track(item);

		editable_remove_track(&lib_editable, (struct simple_track *)track);
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
	if (artist) {
		if (artist->expanded) {
			/* deselect album, select artist */
			artist_to_iter(artist, &sel);
			window_set_sel(lib_tree_win, &sel);

			artist->expanded = 0;
			lib_cur_win = lib_tree_win;
		} else {
			artist->expanded = 1;
		}
		window_changed(lib_tree_win);
	}
}

static void remove_track(struct tree_track *track)
{
	if (album_selected(track->album)) {
		struct iter iter;

		tree_track_to_iter(track, &iter);
		window_row_vanishes(lib_track_win, &iter);
	}
	list_del(&track->node);
}

static void remove_album(struct album *album)
{
	if (album->artist->expanded) {
		struct iter iter;

		album_to_iter(album, &iter);
		window_row_vanishes(lib_tree_win, &iter);
	}
	list_del(&album->node);
}

static void remove_artist(struct artist *artist)
{
	struct iter iter;

	artist_to_iter(artist, &iter);
	window_row_vanishes(lib_tree_win, &iter);
	list_del(&artist->node);
}

void tree_remove(struct tree_track *track)
{
	struct album *album = track->album;
	struct artist *sel_artist;
	struct album *sel_album;

	tree_win_get_selected(&sel_artist, &sel_album);

	remove_track(track);
	/* don't free the track */

	if (list_empty(&album->track_head)) {
		struct artist *artist = album->artist;

		if (sel_album == album)
			lib_cur_win = lib_tree_win;

		remove_album(album);
		album_free(album);

		if (list_empty(&artist->album_head)) {
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

void tree_sel_current(void)
{
	if (lib_cur_track) {
		struct iter iter;

		CUR_ARTIST->expanded = 1;

		if (lib_cur_win == lib_tree_win) {
			lib_cur_win = lib_track_win;
			lib_tree_win->changed = 1;
			lib_track_win->changed = 1;
		}

		album_to_iter(CUR_ALBUM, &iter);
		window_set_sel(lib_tree_win, &iter);

		tree_track_to_iter(lib_cur_track, &iter);
		window_set_sel(lib_track_win, &iter);
	}
}

static int album_for_each_track(struct album *album, int (*cb)(void *data, struct track_info *ti),
		void *data, int reverse)
{
	struct tree_track *track;
	int rc = 0;

	if (reverse) {
		list_for_each_entry_reverse(track, &album->track_head, node) {
			rc = cb(data, tree_track_info(track));
			if (rc)
				break;
		}
	} else {
		list_for_each_entry(track, &album->track_head, node) {
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
