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

#include <history.h>
#include <xmalloc.h>
#include <file_load.h>
#include <uchar.h>
#include <list.h>
#include <debug.h>

#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#define SANITY_CHECK() \
	do { \
		BUG_ON(!u_is_first_byte(history->current[history->current_bpos])); \
		BUG_ON(history->current_bpos > history->current_blen); \
		BUG_ON(history->current_bpos < 0); \
		BUG_ON(history->current_cpos < 0); \
	} while (0)

void history_init(struct history *history, int max_lines)
{
	list_init(&history->head);
	history->max_lines = max_lines;
	history->lines = 0;
	history->search_pos = NULL;
	history->current_cpos = 0;
	history->current_bpos = 0;
	history->current_clen = 0;
	history->current_blen = 0;
	history->current_size = 128;
	history->current = xnew(char, history->current_size);
	history->current[0] = 0;
	history->search = NULL;
	history->save = NULL;
}

void history_insert_ch(struct history *history, uchar ch)
{
	int size;

	SANITY_CHECK();
	if (!u_is_unicode(ch))
		return;

	size = u_char_size(ch);
	if (history->current_blen + size > history->current_size) {
		history->current_size *= 2;
		history->current = xrenew(char, history->current, history->current_size);
	}
	memmove(
			history->current + history->current_bpos + size,
			history->current + history->current_bpos,
			history->current_blen - history->current_bpos + 1);
	u_set_char(history->current, &history->current_bpos, ch);
	history->current_cpos++;
	history->current_blen += size;
	history->current_clen++;
	history->search_pos = NULL;
}

int history_backspace(struct history *history)
{
	int bpos, size;

	SANITY_CHECK();

	history->search_pos = NULL;
	if (history->current_bpos == 0)
		return -1;
	bpos = history->current_bpos - 1;
	while (!u_is_first_byte(history->current[bpos]))
		bpos--;
	size = history->current_bpos - bpos;
	memmove(
			history->current + bpos,
			history->current + history->current_bpos,
			history->current_blen - history->current_bpos + 1);
	history->current_bpos -= size;
	history->current_cpos--;
	history->current_blen -= size;
	history->current_clen--;
	return 0;
}

int history_delete_ch(struct history *history)
{
	uchar ch;
	int size, bpos;

	SANITY_CHECK();

	history->search_pos = NULL;
	if (history->current_bpos == history->current_blen)
		return -1;
	bpos = history->current_bpos;
	u_get_char(history->current, &bpos, &ch);
	size = u_char_size(ch);
	history->current_blen -= size;
	history->current_clen--;
	memmove(
			history->current + history->current_bpos,
			history->current + history->current_bpos + size,
			history->current_blen - history->current_bpos + 1);
	return 0;
}

void history_current_save(struct history *history)
{
	struct history_entry *new;
	struct list_head *item;

	SANITY_CHECK();

	new = xnew(struct history_entry, 1);
	new->text = xstrdup(history->current);
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
			free(hentry->text);
			free(hentry);
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
		free(hentry->text);
		free(hentry);
		history->lines--;
	}

	history->current_cpos = 0;
	history->current_bpos = 0;
	history->current_blen = 0;
	history->current_clen = 0;
	history->current[0] = 0;
	history->search_pos = NULL;
}

void history_move_left(struct history *history)
{
	SANITY_CHECK();

	if (history->current_bpos > 0) {
		history->current_cpos--;
		history->current_bpos--;
		while (!u_is_first_byte(history->current[history->current_bpos]))
			history->current_bpos--;
	}
}

void history_move_right(struct history *history)
{
	SANITY_CHECK();

	if (history->current_bpos < history->current_blen) {
		uchar ch;

		u_get_char(history->current, &history->current_bpos, &ch);
		history->current_cpos++;
	}
}

void history_move_home(struct history *history)
{
	SANITY_CHECK();

	history->current_cpos = 0;
	history->current_bpos = 0;
}

void history_move_end(struct history *history)
{
	SANITY_CHECK();

	history->current_cpos = history->current_clen;
	history->current_bpos = history->current_blen;
}

void history_free(struct history *history)
{
	struct list_head *item;

	SANITY_CHECK();

	item = history->head.next;
	while (item != &history->head) {
		struct history_entry *hentry;
		
		hentry = list_entry(item, struct history_entry, node);
		item = item->next;
		free(hentry->text);
		free(hentry);
	}
	free(history->current);
	free(history->search);
	free(history->save);
}

int history_search_forward(struct history *history)
{
	struct list_head *item;
	int search_len;

	SANITY_CHECK();

	if (history->search_pos == NULL) {
		/* first time to search. set search */
		item = history->head.next;
		free(history->search);
		history->search = xstrdup(history->current);
		free(history->save);
		history->save = xstrdup(history->current);
	} else {
		item = history->search_pos->next;
	}
	search_len = strlen(history->search);
	while (item != &history->head) {
		struct history_entry *hentry;
		
		hentry = list_entry(item, struct history_entry, node);
		if (strncmp(history->search, hentry->text, search_len) == 0) {
			int len = strlen(hentry->text);

			if (len >= history->current_size) {
				while (len >= history->current_size)
					history->current_size *= 2;
				history->current = xrenew(char, history->current, history->current_size);
			}
			memcpy(history->current, hentry->text, len + 1);
			history->current_cpos = u_strlen(history->current);
			history->current_bpos = len;
			history->current_clen = history->current_cpos;
			history->current_blen = len;

			history->search_pos = item;
			return 1;
		}
		item = item->next;
	}
	return 0;
}

int history_search_backward(struct history *history)
{
	struct list_head *item;
	int search_len;

	SANITY_CHECK();

	if (history->search_pos == NULL)
		return 0;
	item = history->search_pos->prev;
	search_len = strlen(history->search);
	while (item != &history->head) {
		struct history_entry *hentry;
		
		hentry = list_entry(item, struct history_entry, node);
		if (strncmp(history->search, hentry->text, search_len) == 0) {
			int len = strlen(hentry->text);

			if (len >= history->current_size) {
				while (len >= history->current_size)
					history->current_size *= 2;
				history->current = xrenew(char, history->current, history->current_size);
			}
			memcpy(history->current, hentry->text, len + 1);
			history->current_cpos = u_strlen(history->current);
			history->current_bpos = len;
			history->current_clen = history->current_cpos;
			history->current_blen = len;

			history->search_pos = item;
			return 1;
		}
		item = item->prev;
	}
	strcpy(history->current, history->save);
	history->current_cpos = u_strlen(history->current);
	history->current_blen = strlen(history->current);
	history->current_clen = history->current_cpos;
	history->current_bpos = history->current_blen;

	history->search_pos = NULL;
	return 0;
}

static void history_add_tail(void *data, const char *line)
{
	struct history *history = data;

	if (history->lines < history->max_lines) {
		struct history_entry *new;

		new = xnew(struct history_entry, 1);
		new->text = xstrdup(line);
		list_add_tail(&new->node, &history->head);
		history->lines++;
	}
}

int history_load(struct history *history, const char *filename)
{
	return file_load(filename, history_add_tail, history);
}

int history_save(struct history *history, const char *filename)
{
	struct list_head *item;
	int fd;

	fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0666);
	if (fd == -1)
		return -1;
	list_for_each(item, &history->head) {
		struct history_entry *history_entry;
		const char nl = '\n';

		history_entry = list_entry(item, struct history_entry, node);
		write(fd, history_entry->text, strlen(history_entry->text));
		write(fd, &nl, 1);
	}
	close(fd);
	return 0;
}
