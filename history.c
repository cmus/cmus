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

#include "history.h"
#include "xmalloc.h"
#include "file.h"
#include "uchar.h"
#include "list.h"
#include "prog.h"

#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

struct history_entry {
	struct list_head node;
	char *text;
};

static struct history_entry *history_entry_new(const char *text)
{
	struct history_entry *new;
	new = xnew(struct history_entry, 1);
	new->text = xstrdup(text);
	return new;
}

static void history_entry_free(struct history_entry *history)
{
	free(history->text);
	free(history);
}

void history_free(struct history *history)
{
	struct list_head *item, *temp;
	list_for_each_safe(item, temp, &history->head) {
		struct history_entry *history_entry;
		history_entry = list_entry(item, struct history_entry, node);
		history_entry_free(history_entry);
	}
}

static int history_add_tail(void *data, const char *line)
{
	struct history *history = data;

	if (history->lines < history->max_lines) {
		struct history_entry *new;

		new = history_entry_new(line);
		list_add_tail(&new->node, &history->head);
		history->lines++;
	}
	return 0;
}

void history_load(struct history *history, char *filename, int max_lines)
{
	list_init(&history->head);
	history->max_lines = max_lines;
	history->lines = 0;
	history->search_pos = NULL;
	history->filename = filename;
	file_for_each_line(filename, history_add_tail, history);
}

void history_save(struct history *history)
{
	char filename_tmp[512];
	struct list_head *item;
	int fd;
	ssize_t rc;

	snprintf(filename_tmp, sizeof(filename_tmp), "%s.tmp", history->filename);
	fd = open(filename_tmp, O_CREAT | O_WRONLY | O_TRUNC, 0666);
	if (fd == -1)
		return;
	list_for_each(item, &history->head) {
		struct history_entry *history_entry;
		const char nl = '\n';

		history_entry = list_entry(item, struct history_entry, node);

		rc = write(fd, history_entry->text, strlen(history_entry->text));
		if (rc == -1)
			goto out;

		rc = write(fd, &nl, 1);
		if (rc == -1)
			goto out;
	}
out:
	close(fd);

	rc = rename(filename_tmp, history->filename);
	if (rc)
		warn_errno("renaming %s to %s", filename_tmp, history->filename);
}

void history_add_line(struct history *history, const char *line)
{
	struct history_entry *new;
	struct list_head *item;

	new = history_entry_new(line);
	list_add(&new->node, &history->head);
	history->lines++;

	/* remove identical */
	item = history->head.next->next;
	while (item != &history->head) {
		struct list_head *next = item->next;
		struct history_entry *hentry;

		hentry = container_of(item, struct history_entry, node);
		if (strcmp(hentry->text, new->text) == 0) {
			list_del(item);
			history_entry_free(hentry);
			history->lines--;
		}
		item = next;
	}

	/* remove oldest if history is 'full' */
	if (history->lines > history->max_lines) {
		struct list_head *node;
		struct history_entry *hentry;

		node = history->head.prev;
		list_del(node);
		hentry = list_entry(node, struct history_entry, node);
		history_entry_free(hentry);
		history->lines--;
	}
}

void history_reset_search(struct history *history)
{
	history->search_pos = NULL;
}

const char *history_search_forward(struct history *history, const char *text)
{
	struct list_head *item;
	int search_len;

	if (history->search_pos == NULL) {
		/* first time to search. set search */
		item = history->head.next;
	} else {
		item = history->search_pos->next;
	}
	search_len = strlen(text);
	while (item != &history->head) {
		struct history_entry *hentry;

		hentry = list_entry(item, struct history_entry, node);
		if (strncmp(text, hentry->text, search_len) == 0) {
			history->search_pos = item;
			return hentry->text;
		}
		item = item->next;
	}
	return NULL;
}

const char *history_search_backward(struct history *history, const char *text)
{
	struct list_head *item;
	int search_len;

	if (history->search_pos == NULL)
		return NULL;
	item = history->search_pos->prev;
	search_len = strlen(text);
	while (item != &history->head) {
		struct history_entry *hentry;

		hentry = list_entry(item, struct history_entry, node);
		if (strncmp(text, hentry->text, search_len) == 0) {
			history->search_pos = item;
			return hentry->text;
		}
		item = item->prev;
	}
	history->search_pos = NULL;
	return NULL;
}
