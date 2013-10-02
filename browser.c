/*
 * Copyright 2008-2013 Various Authors
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "browser.h"
#include "load_dir.h"
#include "cmus.h"
#include "xmalloc.h"
#include "ui_curses.h"
#include "file.h"
#include "misc.h"
#include "options.h"
#include "uchar.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>

struct window *browser_win;
struct searchable *browser_searchable;
char *browser_dir;

static LIST_HEAD(browser_head);

static inline void browser_entry_to_iter(struct browser_entry *e, struct iter *iter)
{
	iter->data0 = &browser_head;
	iter->data1 = e;
	iter->data2 = NULL;
}

/* filter out names starting with '.' except '..' */
static int normal_filter(const char *name, const struct stat *s)
{
	if (name[0] == '.') {
		if (name[1] == '.' && name[2] == 0)
			return 1;
		return 0;
	}
	if (S_ISDIR(s->st_mode))
		return 1;
	return cmus_is_supported(name);
}

/* filter out '.' */
static int hidden_filter(const char *name, const struct stat *s)
{
	if (name[0] == '.' && name[1] == 0)
		return 0;
	return 1;
}

/* only works for BROWSER_ENTRY_DIR and BROWSER_ENTRY_FILE */
static int entry_cmp(const struct browser_entry *a, const struct browser_entry *b)
{
	if (a->type == BROWSER_ENTRY_DIR) {
		if (b->type == BROWSER_ENTRY_FILE)
			return -1;
		if (!strcmp(a->name, "../"))
			return -1;
		if (!strcmp(b->name, "../"))
			return 1;
		return strcmp(a->name, b->name);
	}
	if (b->type == BROWSER_ENTRY_DIR)
		return 1;
	return strcmp(a->name, b->name);
}

static char *fullname(const char *path, const char *name)
{
	int l1, l2;
	char *full;

	l1 = strlen(path);
	l2 = strlen(name);
	if (path[l1 - 1] == '/')
		l1--;
	full = xnew(char, l1 + 1 + l2 + 1);
	memcpy(full, path, l1);
	full[l1] = '/';
	memcpy(full + l1 + 1, name, l2 + 1);
	return full;
}

static void free_browser_list(void)
{
	struct list_head *item;

	item = browser_head.next;
	while (item != &browser_head) {
		struct list_head *next = item->next;
		struct browser_entry *entry;

		entry = list_entry(item, struct browser_entry, node);
		free(entry);
		item = next;
	}
	list_init(&browser_head);
}

static int add_pl_line(void *data, const char *line)
{
	struct browser_entry *e;
	int name_size = strlen(line) + 1;

	e = xmalloc(sizeof(struct browser_entry) + name_size);
	memcpy(e->name, line, name_size);
	e->type = BROWSER_ENTRY_PLLINE;
	list_add_tail(&e->node, &browser_head);
	return 0;
}

static int do_browser_load(const char *name)
{
	struct stat st;

	if (stat(name, &st))
		return -1;

	if (S_ISREG(st.st_mode) && cmus_is_playlist(name)) {
		char *buf;
		int size;

		buf = mmap_file(name, &size);
		if (size == -1)
			return -1;

		free_browser_list();

		if (buf) {
			struct browser_entry *parent_dir_e = xmalloc(sizeof(struct browser_entry) + 4);
			strcpy(parent_dir_e->name, "../");
			parent_dir_e->type = BROWSER_ENTRY_DIR;
			list_add_tail(&parent_dir_e->node, &browser_head);

			cmus_playlist_for_each(buf, size, 0, add_pl_line, NULL);
			munmap(buf, size);
		}
	} else if (S_ISDIR(st.st_mode)) {
		int (*filter)(const char *, const struct stat *) = normal_filter;
		struct directory dir;
		const char *str;
		int root = !strcmp(name, "/");

		if (show_hidden)
			filter = hidden_filter;

		if (dir_open(&dir, name))
			return -1;

		free_browser_list();
		while ((str = dir_read(&dir))) {
			struct browser_entry *e;
			struct list_head *item;
			int len;

			if (!filter(str, &dir.st))
				continue;

			/* ignore .. if we are in the root dir */
			if (root && !strcmp(str, ".."))
				continue;

			len = strlen(str);
			e = xmalloc(sizeof(struct browser_entry) + len + 2);
			e->type = BROWSER_ENTRY_FILE;
			memcpy(e->name, str, len);
			if (S_ISDIR(dir.st.st_mode)) {
				e->type = BROWSER_ENTRY_DIR;
				e->name[len++] = '/';
			}
			e->name[len] = 0;

			item = browser_head.prev;
			while (item != &browser_head) {
				struct browser_entry *other;

				other = container_of(item, struct browser_entry, node);
				if (entry_cmp(e, other) >= 0)
					break;
				item = item->prev;
			}
			/* add after item */
			list_add(&e->node, item);
		}
		dir_close(&dir);

		/* try to update currect working directory */
		if (chdir(name))
			return -1;
	} else {
		errno = ENOTDIR;
		return -1;
	}
	return 0;
}

static int browser_load(const char *name)
{
	int rc;

	rc = do_browser_load(name);
	if (rc)
		return rc;

	window_set_contents(browser_win, &browser_head);
	free(browser_dir);
	browser_dir = xstrdup(name);
	return 0;
}

static GENERIC_ITER_PREV(browser_get_prev, struct browser_entry, node)
static GENERIC_ITER_NEXT(browser_get_next, struct browser_entry, node)

static int browser_search_get_current(void *data, struct iter *iter)
{
	return window_get_sel(browser_win, iter);
}

static int browser_search_matches(void *data, struct iter *iter, const char *text)
{
	char **words = get_words(text);
	int matched = 0;

	if (words[0] != NULL) {
		struct browser_entry *e;
		int i;

		e = iter_to_browser_entry(iter);
		for (i = 0; ; i++) {
			if (words[i] == NULL) {
				window_set_sel(browser_win, iter);
				matched = 1;
				break;
			}
			if (u_strcasestr_filename(e->name, words[i]) == NULL)
				break;
		}
	}
	free_str_array(words);
	return matched;
}

static const struct searchable_ops browser_search_ops = {
	.get_prev = browser_get_prev,
	.get_next = browser_get_next,
	.get_current = browser_search_get_current,
	.matches = browser_search_matches
};

void browser_init(void)
{
	struct iter iter;
	char cwd[1024];
	char *dir;

	if (getcwd(cwd, sizeof(cwd)) == NULL) {
		dir = xstrdup("/");
	} else {
		dir = xstrdup(cwd);
	}
	if (do_browser_load(dir)) {
		free(dir);
		do_browser_load("/");
		browser_dir = xstrdup("/");
	} else {
		browser_dir = dir;
	}

	browser_win = window_new(browser_get_prev, browser_get_next);
	window_set_contents(browser_win, &browser_head);
	window_changed(browser_win);

	iter.data0 = &browser_head;
	iter.data1 = NULL;
	iter.data2 = NULL;
	browser_searchable = searchable_new(NULL, &iter, &browser_search_ops);
}

void browser_exit(void)
{
	searchable_free(browser_searchable);
	free_browser_list();
	window_free(browser_win);
	free(browser_dir);
}

int browser_chdir(const char *dir)
{
	if (browser_load(dir)) {
	}
	return 0;
}

void browser_up(void)
{
	char *new, *ptr, *pos;
	struct browser_entry *e;
	int len;

	if (strcmp(browser_dir, "/") == 0)
		return;

	ptr = strrchr(browser_dir, '/');
	if (ptr == browser_dir) {
		new = xstrdup("/");
	} else {
		new = xstrndup(browser_dir, ptr - browser_dir);
	}

	if (browser_load(new)) {
		error_msg("could not open directory '%s': %s\n", new, strerror(errno));
		free(new);
		return;
	}
	free(new);

	/* remember last position */
	ptr++;
	len = strlen(ptr);
	pos = xstrdup(ptr);

	/* select */
	list_for_each_entry(e, &browser_head, node) {
		if (strncmp(e->name, pos, len) == 0 &&
		    (e->name[len] == '/' || e->name[len] == '\0')) {
			struct iter iter;

			browser_entry_to_iter(e, &iter);
			window_set_sel(browser_win, &iter);
			break;
		}
	}
	free(pos);
}

static void browser_cd(const char *dir)
{
	char *new;
	int len;

	if (strcmp(dir, "../") == 0) {
		browser_up();
		return;
	}

	new = fullname(browser_dir, dir);
	len = strlen(new);
	if (new[len - 1] == '/')
		new[len - 1] = 0;
	if (browser_load(new))
		error_msg("could not open directory '%s': %s\n", dir, strerror(errno));
	free(new);
}

static void browser_cd_playlist(const char *filename)
{
	if (browser_load(filename))
		error_msg("could not read playlist '%s': %s\n", filename, strerror(errno));
}

void browser_enter(void)
{
	struct browser_entry *e;
	struct iter sel;
	int len;

	if (!window_get_sel(browser_win, &sel))
		return;
	e = iter_to_browser_entry(&sel);
	len = strlen(e->name);
	if (len == 0)
		return;
	if (e->type == BROWSER_ENTRY_DIR) {
		browser_cd(e->name);
	} else {
		if (e->type == BROWSER_ENTRY_PLLINE) {
			cmus_play_file(e->name);
		} else {
			char *filename;

			filename = fullname(browser_dir, e->name);
			if (cmus_is_playlist(filename)) {
				browser_cd_playlist(filename);
			} else {
				cmus_play_file(filename);
			}
			free(filename);
		}
	}
}

char *browser_get_sel(void)
{
	struct browser_entry *e;
	struct iter sel;

	if (!window_get_sel(browser_win, &sel))
		return NULL;

	e = iter_to_browser_entry(&sel);
	if (e->type == BROWSER_ENTRY_PLLINE)
		return xstrdup(e->name);

	return fullname(browser_dir, e->name);
}

void browser_delete(void)
{
	struct browser_entry *e;
	struct iter sel;
	int len;

	if (!window_get_sel(browser_win, &sel))
		return;
	e = iter_to_browser_entry(&sel);
	len = strlen(e->name);
	if (len == 0)
		return;
	if (e->type == BROWSER_ENTRY_FILE) {
		char *name;

		name = fullname(browser_dir, e->name);
		if (yes_no_query("Delete file '%s'? [y/N]", e->name)) {
			if (unlink(name) == -1) {
				error_msg("deleting '%s': %s", e->name, strerror(errno));
			} else {
				window_row_vanishes(browser_win, &sel);
				list_del(&e->node);
				free(e);
			}
		}
		free(name);
	}
}

void browser_reload(void)
{
	char *tmp = xstrdup(browser_dir);
	char *sel = NULL;
	struct iter iter;
	struct browser_entry *e;

	/* remember selection */
	if (window_get_sel(browser_win, &iter)) {
		e = iter_to_browser_entry(&iter);
		sel = xstrdup(e->name);
	}

	/* have to use tmp  */
	if (browser_load(tmp)) {
		error_msg("could not update contents '%s': %s\n", tmp, strerror(errno));
		free(tmp);
		free(sel);
		return;
	}

	if (sel) {
		/* set selection */
		list_for_each_entry(e, &browser_head, node) {
			if (strcmp(e->name, sel) == 0) {
				browser_entry_to_iter(e, &iter);
				window_set_sel(browser_win, &iter);
				break;
			}
		}
	}

	free(tmp);
	free(sel);
}
