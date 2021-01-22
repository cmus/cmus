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

#include "pl.h"
#include "prog.h"
#include "editable.h"
#include "options.h"
#include "xmalloc.h"
#include "load_dir.h"
#include "list.h"
#include "job.h"
#include "misc.h"
#include "ui_curses.h"
#include "xstrjoin.h"
#include "worker.h"
#include "uchar.h"
#include "mergesort.h"

#include <unistd.h>
#include <stdio.h>
#include <signal.h>

struct playlist {
	struct list_head node;

	char *name;
	struct editable editable;
	struct rb_root shuffle_root;
	struct simple_track *cur_track;
};

static struct playlist *pl_visible; /* never NULL */
static struct playlist *pl_marked; /* never NULL */
struct window *pl_list_win;

/* pl_playing_track shares its track_info reference with the playlist it's in.
 * pl_playing_track and pl_playing might be null but pl_playing_track != NULL
 * implies pl_playing != NULL and pl_playing_track is in pl_playing.
 */
static struct simple_track *pl_playing_track;
static struct playlist *pl_playing;

static int pl_cursor_in_track_window;
struct editable_shared pl_editable_shared;
static LIST_HEAD(pl_head); /* never empty */

static struct searchable *pl_searchable;

static char *pl_name_to_pl_file(const char *name)
{
	return xstrjoin(cmus_playlist_dir, "/", name);
}

static void pl_to_iter(struct playlist *pl, struct iter *iter)
{
	*iter = (struct iter) {
		.data0 = &pl_head,
		.data1 = &pl->node
	};
}

static struct playlist *pl_from_list(const struct list_head *list)
{
	return container_of(list, struct playlist, node);
}

static struct playlist *pl_from_editable(const struct editable *editable)
{
	return container_of(editable, struct playlist, editable);
}

static int pl_search_get_generic(struct iter *iter,
		struct list_head *(*list_step)(struct list_head *list),
		int (*iter_step)(struct iter *iter))
{
	struct list_head *pl_node = iter->data2;
	struct playlist *pl;

	if (!pl_node)
		pl_node = &pl_head;

	if (iter_step(iter))
		return 1;

	pl_node = list_step(pl_node);
	if (pl_node == &pl_head)
		return 0;

	pl = pl_from_list(pl_node);
	iter->data0 = &pl->editable.head;
	iter->data1 = NULL;
	iter->data2 = pl_node;
	return 1;
}

static int pl_search_get_prev(struct iter *iter)
{
	return pl_search_get_generic(iter, list_prev, simple_track_get_prev);
}

static int pl_search_get_next(struct iter *iter)
{
	return pl_search_get_generic(iter, list_next, simple_track_get_next);
}

static int pl_search_get_current(void *data, struct iter *iter)
{
	window_get_sel(pl_editable_shared.win, iter);
	iter->data2 = &pl_visible->node;
	return 1;
}

static int pl_search_matches(void *data, struct iter *iter, const char *text)
{
	struct playlist *pl = pl_from_list(iter->data2);
	int matched = 0;

	char **words = get_words(text);
	for (size_t i = 0; words[i]; i++) {

		/* set in the loop to deal with empty search string */
		matched = 1;

		if (!u_strcasestr_base(pl->name, words[i])) {
			matched = 0;
			break;
		}
	}
	free_str_array(words);

	if (!matched && iter->data1)
		matched = _simple_track_search_matches(iter, text);

	if (matched) {
		struct iter list_iter;
		pl_to_iter(pl, &list_iter);
		window_set_sel(pl_list_win, &list_iter);

		editable_take_ownership(&pl->editable);

		if (iter->data1) {
			struct iter track_iter = *iter;
			track_iter.data2 = NULL;
			window_set_sel(pl_editable_shared.win, &track_iter);
		}

		pl_cursor_in_track_window = !!iter->data1;
	}

	return matched;
}

static const struct searchable_ops pl_searchable_ops = {
	.get_prev = pl_search_get_prev,
	.get_next = pl_search_get_next,
	.get_current = pl_search_get_current,
	.matches = pl_search_matches,
};

static void pl_free_track(struct editable *e, struct list_head *item)
{
	struct playlist *pl = pl_from_editable(e);
	struct simple_track *track = to_simple_track(item);
	struct shuffle_track *shuffle_track =
		simple_track_to_shuffle_track(track);

	if (track == pl->cur_track)
		pl->cur_track = NULL;

	rb_erase(&shuffle_track->tree_node, &pl->shuffle_root);
	track_info_unref(track->info);
	free(track);
}

static struct playlist *pl_new(const char *name)
{
	struct playlist *pl = xnew0(struct playlist, 1);
	pl->name = xstrdup(name);
	editable_init(&pl->editable, &pl_editable_shared, 0);
	return pl;
}

static void pl_free(struct playlist *pl)
{
	editable_clear(&pl->editable);
	free(pl->name);
	free(pl);
}

static void pl_add_track(struct playlist *pl, struct track_info *ti)
{
	struct shuffle_track *track = xnew(struct shuffle_track, 1);

	track_info_ref(ti);
	simple_track_init(&track->simple_track, ti);
	shuffle_list_add(track, &pl->shuffle_root);
	editable_add(&pl->editable, &track->simple_track);
}

static void pl_add_cb(struct track_info *ti, void *opaque)
{
	pl_add_track(opaque, ti);
}

int pl_add_file_to_marked_pl(const char *file)
{
	char *full = NULL;
	enum file_type type = cmus_detect_ft(file, &full);
	int not_invalid = type != FILE_TYPE_INVALID;
	if (not_invalid)
		cmus_add(pl_add_cb, full, type, JOB_TYPE_PL, 0, pl_marked);
	free(full);
	return not_invalid;
}

void pl_add_track_to_marked_pl(struct track_info *ti)
{
	pl_add_track(pl_marked, ti);
}

static int pl_list_compare(const struct list_head *l, const struct list_head *r)
{
	struct playlist *pl = pl_from_list(l);
	struct playlist *pr = pl_from_list(r);
	return strcmp(pl->name, pr->name);
}

static void pl_sort_all(void)
{
	list_mergesort(&pl_head, pl_list_compare);
}

static void pl_load_one(const char *file)
{
	char *full = pl_name_to_pl_file(file);

	struct playlist *pl = pl_new(file);
	cmus_add(pl_add_cb, full, FILE_TYPE_PL, JOB_TYPE_PL, 0, pl);
	list_add_tail(&pl->node, &pl_head);

	free(full);
}

static void pl_load_all(void)
{
	struct directory dir;
	if (dir_open(&dir, cmus_playlist_dir))
		die_errno("error: cannot open playlist directory %s", cmus_playlist_dir);
	const char *file;
	while ((file = dir_read(&dir))) {
		if (strcmp(file, ".") == 0 || strcmp(file, "..") == 0)
			continue;
		if (!S_ISREG(dir.st.st_mode)) {
			error_msg("error: %s in %s is not a regular file", file,
					cmus_playlist_dir);
			continue;
		}
		pl_load_one(file);
	}
	dir_close(&dir);
}

static void pl_create_default(void)
{
	struct playlist *pl = pl_new("default");
	list_add_tail(&pl->node, &pl_head);
}

static GENERIC_ITER_PREV(pl_list_get_prev, struct playlist, node);
static GENERIC_ITER_NEXT(pl_list_get_next, struct playlist, node);

static void pl_list_sel_changed(void)
{
	struct list_head *list = pl_list_win->sel.data1;
	struct playlist *pl = pl_from_list(list);
	pl_visible = pl;
	editable_take_ownership(&pl_visible->editable);
}

static int pl_dummy_filter(const struct simple_track *track)
{
	return 1;
}

static int pl_empty(struct playlist *pl)
{
	return editable_empty(&pl->editable);
}

static struct simple_track *pl_get_selected_track(void)
{
	/* pl_visible is not empty */

	struct iter sel = pl_editable_shared.win->sel;
	return iter_to_simple_track(&sel);
}

static struct simple_track *pl_get_first_track(struct playlist *pl)
{
	/* pl is not empty */

	if (shuffle) {
		struct shuffle_track *st = shuffle_list_get_next(&pl->shuffle_root, NULL, pl_dummy_filter);
		return &st->simple_track;
	} else {
		return to_simple_track(pl->editable.head.next);
	}
}

static struct track_info *pl_play_track(struct playlist *pl, struct simple_track *t, bool force_follow)
{
	/* t is a track in pl */

	if (pl != pl_playing)
		pl_list_win->changed = 1;

	pl_playing_track = t;
	pl_playing = pl;
	pl_editable_shared.win->changed = 1;

	if (force_follow || follow)
		pl_select_playing_track();

	/* reference owned by the caller */
	track_info_ref(pl_playing_track->info);

	return pl_playing_track->info;
}

static struct track_info *pl_play_selected_track(void)
{
	if (pl_empty(pl_visible))
		return NULL;

	return pl_play_track(pl_visible, pl_get_selected_track(), false);
}

static struct track_info *pl_play_first_in_pl_playing(void)
{
	if (!pl_playing)
		pl_playing = pl_visible;

	if (pl_empty(pl_playing)) {
		pl_playing = NULL;
		return NULL;
	}

	return pl_play_track(pl_playing, pl_get_first_track(pl_playing), false);
}

static struct simple_track *pl_get_next(struct playlist *pl, struct simple_track *cur)
{
	return simple_list_get_next(&pl->editable.head, cur, pl_dummy_filter);
}

static struct simple_track *pl_get_next_shuffled(struct playlist *pl,
		struct simple_track *cur)
{
	struct shuffle_track *st = simple_track_to_shuffle_track(cur);
	st = shuffle_list_get_next(&pl->shuffle_root, st, pl_dummy_filter);
	return &st->simple_track;
}

static struct simple_track *pl_get_prev(struct playlist *pl,
		struct simple_track *cur)
{
	return simple_list_get_prev(&pl->editable.head, cur, pl_dummy_filter);
}

static struct simple_track *pl_get_prev_shuffled(struct playlist *pl,
		struct simple_track *cur)
{
	struct shuffle_track *st = simple_track_to_shuffle_track(cur);
	st = shuffle_list_get_prev(&pl->shuffle_root, st, pl_dummy_filter);
	return &st->simple_track;
}

static int pl_match_add_job(uint32_t type, void *job_data, void *opaque)
{
	uint32_t pat = JOB_TYPE_PL | JOB_TYPE_ADD;
	if (type != pat)
		return 0;

	struct add_data *add_data= job_data;
	return add_data->opaque == opaque;
}

static void pl_cancel_add_jobs(struct playlist *pl)
{
	worker_remove_jobs_by_cb(pl_match_add_job, pl);
}

static int pl_save_cb(track_info_cb cb, void *data, void *opaque)
{
	struct playlist *pl = opaque;
	return editable_for_each(&pl->editable, cb, data, 0);
}

static void pl_save_one(struct playlist *pl)
{
	char *path = pl_name_to_pl_file(pl->name);
	cmus_save(pl_save_cb, path, pl);
	free(path);
}

static void pl_save_all(void)
{
	struct playlist *pl;
	list_for_each_entry(pl, &pl_head, node)
		pl_save_one(pl);
}

static void pl_delete_selected_pl(void)
{
	if (list_len(&pl_head) == 1) {
		error_msg("cannot delete the last playlist");
		return;
	}

	if (yes_no_query("Delete selected playlist? [y/N]") != UI_QUERY_ANSWER_YES)
		return;

	struct playlist *pl = pl_visible;

	struct iter iter;
	pl_to_iter(pl, &iter);
	window_row_vanishes(pl_list_win, &iter);

	list_del(&pl->node);

	if (pl == pl_marked)
		pl_marked = pl_visible;
	if (pl == pl_playing) {
		pl_playing = NULL;
		pl_playing_track = NULL;
	}

	char *path = pl_name_to_pl_file(pl->name);
	unlink(path);
	free(path);

	pl_cancel_add_jobs(pl);

	/* can't free the pl now because the worker thread might hold a
	 * reference to it. instead free it once all running jobs are done.
	 */
	struct pl_delete_data *pdd = xnew(struct pl_delete_data, 1);
	pdd->cb = pl_free;
	pdd->pl = pl;
	job_schedule_pl_delete(pdd);
}

static void pl_mark_selected_pl(void)
{
	pl_marked = pl_visible;
	pl_list_win->changed = 1;
}

typedef struct simple_track *(*pl_shuffled_move)(struct playlist *pl,
		struct simple_track *cur);
typedef struct simple_track *(*pl_normal_move)(struct playlist *pl,
		struct simple_track *cur);

static struct track_info *pl_goto_generic(pl_shuffled_move shuffled,
		pl_normal_move normal)
{
	if (!pl_playing_track)
		return pl_play_first_in_pl_playing();

	struct simple_track *track;

	if (shuffle)
		track = shuffled(pl_playing, pl_playing_track);
	else
		track = normal(pl_playing, pl_playing_track);

	if (track)
		return pl_play_track(pl_playing, track, false);
	return NULL;
}

static void pl_clear_visible_pl(void)
{
	if (pl_cursor_in_track_window)
		pl_win_next();
	if (pl_visible == pl_playing)
		pl_playing_track = NULL;
	editable_clear(&pl_visible->editable);
	pl_cancel_add_jobs(pl_visible);
}

static int pl_name_exists(const char *name)
{
	struct playlist *pl;
	list_for_each_entry(pl, &pl_head, node) {
		if (strcmp(pl->name, name) == 0)
			return 1;
	}
	return 0;
}

static int pl_check_new_pl_name(const char *name)
{
	if (strchr(name, '/')) {
		error_msg("playlists cannot contain the '/' character");
		return 0;
	}

	if (pl_name_exists(name)) {
		error_msg("another playlist named %s already exists", name);
		return 0;
	}

	return 1;
}

static char *pl_create_name(const char *file)
{
	size_t file_len = strlen(file);

	char *name = xnew(char, file_len + 10);
	strcpy(name, file);

	for (int i = 1; pl_name_exists(name); i++) {
		if (i == 100) {
			free(name);
			return NULL;
		}
		sprintf(name + file_len, ".%d", i);
	}

	return name;
}

static void pl_delete_selected_track(void)
{
	/* pl_cursor_in_track_window == true */

	if (pl_get_selected_track() == pl_playing_track)
		pl_playing_track = NULL;
	editable_remove_sel(&pl_visible->editable);
	if (pl_empty(pl_visible))
		pl_win_next();
}

void pl_init(void)
{
	editable_shared_init(&pl_editable_shared, pl_free_track);

	pl_load_all();
	if (list_empty(&pl_head))
		pl_create_default();

	pl_sort_all();

	pl_list_win = window_new(pl_list_get_prev, pl_list_get_next);
	pl_list_win->sel_changed = pl_list_sel_changed;
	window_set_contents(pl_list_win, &pl_head);
	window_changed(pl_list_win);

	/* pl_visible set by window_set_contents */
	pl_marked = pl_visible;

	struct iter iter = { 0 };
	pl_searchable = searchable_new(NULL, &iter, &pl_searchable_ops);
}

void pl_exit(void)
{
	pl_save_all();
}

void pl_save(void)
{
	pl_save_all();
}

void pl_import(const char *path)
{
	const char *file = get_filename(path);
	if (!file) {
		error_msg("\"%s\" is not a valid path", path);
		return;
	}

	char *name = pl_create_name(file);
	if (!name) {
		error_msg("a playlist named \"%s\" already exists ", file);
		return;
	}

	if (strcmp(name, file) != 0)
		info_msg("adding \"%s\" as \"%s\"", file, name);

	struct playlist *pl = pl_new(name);
	cmus_add(pl_add_cb, path, FILE_TYPE_PL, JOB_TYPE_PL, 0, pl);
	list_add_tail(&pl->node, &pl_head);
	pl_list_win->changed = 1;

	free(name);
}

void pl_export_selected_pl(const char *path)
{
	char *tmp = expand_filename(path);
	if (access(tmp, F_OK) != 0 || yes_no_query("File exists. Overwrite? [y/N]") == UI_QUERY_ANSWER_YES)
		cmus_save(pl_save_cb, tmp, pl_visible);
	free(tmp);
}

struct searchable *pl_get_searchable(void)
{
	return pl_searchable;
}

struct track_info *pl_goto_next(void)
{
	return pl_goto_generic(pl_get_next_shuffled, pl_get_next);
}

struct track_info *pl_goto_prev(void)
{
	return pl_goto_generic(pl_get_prev_shuffled, pl_get_prev);
}

struct track_info *pl_play_selected_row(void)
{
	/* a bit tricky because we want to insert the selected track at the
	 * current position in the shuffle list. but we must be careful not to
	 * insert a track into a foreign shuffle list.
	 */

	int was_in_track_window = pl_cursor_in_track_window;

	struct playlist *prev_pl = pl_playing;
	struct simple_track *prev_track = pl_playing_track;

	struct track_info *rv = NULL;

	if (!pl_cursor_in_track_window) {
		if (shuffle && !pl_empty(pl_visible)) {
			struct shuffle_track *st = shuffle_list_get_next(&pl_visible->shuffle_root, NULL, pl_dummy_filter);
			struct simple_track *track = &st->simple_track;
			rv = pl_play_track(pl_visible, track, true);
		}
	}

	if (!rv)
		rv = pl_play_selected_track();

	if (shuffle && rv && (pl_playing == prev_pl) && prev_track) {
		struct shuffle_track *prev_st = simple_track_to_shuffle_track(prev_track);
		struct shuffle_track *cur_st =
			simple_track_to_shuffle_track(pl_playing_track);
		shuffle_insert(&pl_playing->shuffle_root, prev_st, cur_st);
	}

	pl_cursor_in_track_window = was_in_track_window;

	return rv;
}

void pl_select_playing_track(void)
{
	if (!pl_playing_track)
		return;

	struct iter iter;

	editable_take_ownership(&pl_playing->editable);

	editable_track_to_iter(&pl_playing->editable, pl_playing_track, &iter);
	window_set_sel(pl_editable_shared.win, &iter);

	pl_to_iter(pl_playing, &iter);
	window_set_sel(pl_list_win, &iter);

	if (!pl_cursor_in_track_window)
		pl_mark_for_redraw();

	pl_cursor_in_track_window = 1;
}

void pl_reshuffle(void)
{
	if (pl_playing)
		shuffle_list_reshuffle(&pl_playing->shuffle_root);
}

void pl_get_sort_str(char *buf, size_t size)
{
	strscpy(buf, pl_editable_shared.sort_str, size);
}

void pl_set_sort_str(const char *buf)
{
	sort_key_t *keys = parse_sort_keys(buf);

	if (!keys)
		return;

	editable_shared_set_sort_keys(&pl_editable_shared, keys);
	sort_keys_to_str(keys, pl_editable_shared.sort_str,
			sizeof(pl_editable_shared.sort_str));

	struct playlist *pl;
	list_for_each_entry(pl, &pl_head, node)
		editable_sort(&pl->editable);
}

void pl_rename_selected_pl(const char *name)
{
	if (strcmp(pl_visible->name, name) == 0)
		return;

	if (!pl_check_new_pl_name(name))
		return;

	char *full_cur = pl_name_to_pl_file(pl_visible->name);
	char *full_new = pl_name_to_pl_file(name);
	rename(full_cur, full_new);
	free(full_cur);
	free(full_new);

	free(pl_visible->name);
	pl_visible->name = xstrdup(name);

	pl_mark_for_redraw();
}

void pl_clear(void)
{
	if (!pl_cursor_in_track_window)
		return;

	pl_clear_visible_pl();
}

void pl_mark_for_redraw(void)
{
	pl_list_win->changed = 1;
	pl_editable_shared.win->changed = 1;
}

int pl_needs_redraw(void)
{
	return pl_list_win->changed || pl_editable_shared.win->changed;
}

struct window *pl_cursor_win(void)
{
	if (pl_cursor_in_track_window)
		return pl_editable_shared.win;
	else
		return pl_list_win;
}

int _pl_for_each_sel(track_info_cb cb, void *data, int reverse)
{
	if (pl_cursor_in_track_window)
		return _editable_for_each_sel(&pl_visible->editable, cb, data, reverse);
	else
		return editable_for_each(&pl_visible->editable, cb, data, reverse);
}

int pl_for_each_sel(track_info_cb cb, void *data, int reverse, int advance)
{
	if (pl_cursor_in_track_window)
		return editable_for_each_sel(&pl_visible->editable, cb, data, reverse, advance);
	else
		return editable_for_each(&pl_visible->editable, cb, data, reverse);
}

#define pl_tw_only(cmd) if (!pl_cursor_in_track_window) { \
	info_msg(":%s only works in the track window", cmd); \
} else

void pl_invert_marks(void)
{
	pl_tw_only("invert")
		editable_invert_marks(&pl_visible->editable);
}

void pl_mark(char *arg)
{
	pl_tw_only("mark")
		editable_invert_marks(&pl_visible->editable);
}

void pl_unmark(void)
{
	pl_tw_only("unmark")
		editable_unmark(&pl_visible->editable);
}

void pl_rand(void)
{
	pl_tw_only("rand")
		editable_rand(&pl_visible->editable);
}

void pl_win_mv_after(void)
{
	if (pl_cursor_in_track_window)
		editable_move_after(&pl_visible->editable);
}

void pl_win_mv_before(void)
{
	if (pl_cursor_in_track_window)
		editable_move_before(&pl_visible->editable);
}

void pl_win_remove(void)
{
	if (pl_cursor_in_track_window)
		pl_delete_selected_track();
	else
		pl_delete_selected_pl();
}

void pl_win_toggle(void)
{
	if (pl_cursor_in_track_window)
		editable_toggle_mark(&pl_visible->editable);
	else
		pl_mark_selected_pl();
}

void pl_win_update(void)
{
	if (yes_no_query("Reload this playlist? [y/N]") != UI_QUERY_ANSWER_YES)
		return;

	pl_clear_visible_pl();

	char *full = pl_name_to_pl_file(pl_visible->name);
	cmus_add(pl_add_cb, full, FILE_TYPE_PL, JOB_TYPE_PL, 0, pl_visible);
	free(full);
}

void pl_win_next(void)
{
	pl_cursor_in_track_window ^= 1;
	if (pl_empty(pl_visible))
		pl_cursor_in_track_window = 0;
	pl_mark_for_redraw();
}

void pl_set_nr_rows(int h)
{
	window_set_nr_rows(pl_list_win, h);
	window_set_nr_rows(pl_editable_shared.win, h);
}

unsigned int pl_visible_total_time(void)
{
	return pl_visible->editable.total_time;
}

unsigned int pl_playing_total_time(void)
{
	if (pl_playing)
		return pl_playing->editable.total_time;
	return 0;
}

void pl_list_iter_to_info(struct iter *iter, struct pl_list_info *info)
{
	struct playlist *pl = pl_from_list(iter->data1);

	info->name = pl->name;
	info->marked = pl == pl_marked;
	info->active = !pl_cursor_in_track_window;
	info->selected = pl == pl_visible;
	info->current = pl == pl_playing;
}

void pl_draw(void (*list)(struct window *win),
		void (*tracks)(struct window *win), int full)
{
	if (full || pl_list_win->changed)
		list(pl_list_win);
	if (full || pl_editable_shared.win->changed)
		tracks(pl_editable_shared.win);
	pl_list_win->changed = 0;
	pl_editable_shared.win->changed = 0;
}

struct simple_track *pl_get_playing_track(void)
{
	return pl_playing_track;
}

void pl_update_track(struct track_info *old, struct track_info *new)
{
	struct playlist *pl;
	list_for_each_entry(pl, &pl_head, node)
		editable_update_track(&pl->editable, old, new);
}

int pl_get_cursor_in_track_window(void)
{
	return pl_cursor_in_track_window;
}

void pl_create(const char *name)
{
	if (!pl_check_new_pl_name(name))
		return;

	struct playlist *pl = pl_new(name);
	list_add_tail(&pl->node, &pl_head);
	pl_list_win->changed = 1;
}

int pl_visible_is_marked(void)
{
	return pl_visible == pl_marked;
}

const char *pl_marked_pl_name(void)
{
	return pl_marked->name;
}

void pl_set_marked_pl_by_name(const char *name)
{
	struct playlist *pl;
	list_for_each_entry(pl, &pl_head, node) {
		if (strcmp(pl->name, name) == 0) {
			pl_marked = pl;
			return;
		}
	}
}
