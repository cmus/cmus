/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2005 Timo Hirvonen
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

#include "glob.h"
#include "uchar.h"
#include "list.h"
#include "xmalloc.h"
#include "debug.h"

#include <string.h>

struct glob_item {
	struct list_head node;
	enum {
		GLOB_STAR,
		GLOB_QMARK,
		GLOB_TEXT
	} type;
	char text[];
};

/* simplification:
 * 
 *   ??*? => ???*
 *   *?*  => ?*
 *   *?   => ?*
 *   ...
 */
static void simplify(struct list_head *head)
{
	struct list_head *item;

	item = head->next;
	while (item != head) {
		struct list_head *i, *next;
		int qcount = 0;
		int scount = 0;

		i = item;
		do {
			struct glob_item *gi;

			gi = container_of(i, struct glob_item, node);
			if (gi->type == GLOB_STAR) {
				scount++;
			} else if (gi->type == GLOB_QMARK) {
				qcount++;
			} else {
				i = i->next;
				break;
			}
			i = i->next;
		} while (i != head);

		next = i;

		if (scount) {
			/* move all qmarks to front and
			 * if there are >1 stars remove all but the last */
			struct list_head *insert_after = item->prev;

			i = item;
			while (qcount) {
				struct glob_item *gi;

				gi = container_of(i, struct glob_item, node);
				i = i->next;
				if (gi->type == GLOB_QMARK) {
					list_del(&gi->node);
					list_add(&gi->node, insert_after);
					qcount--;
				}
			}

			i = item;
			while (scount > 1) {
				struct glob_item *gi;

				gi = container_of(i, struct glob_item, node);
				i = i->next;
				if (gi->type == GLOB_STAR) {
					list_del(&gi->node);
					free(gi);
					scount--;
				}
			}
		}

		item = next;
	}
}

void glob_compile(struct list_head *head, const char *pattern)
{
	int i = 0;

	list_init(head);
	while (pattern[i]) {
		struct glob_item *item;

		if (pattern[i] == '*') {
			item = xnew(struct glob_item, 1);
			item->type = GLOB_STAR;
			i++;
		} else if (pattern[i] == '?') {
			item = xnew(struct glob_item, 1);
			item->type = GLOB_QMARK;
			i++;
		} else {
			int start, len, j;
			char *str;

			start = i;
			len = 0;
			while (pattern[i]) {
				if (pattern[i] == '\\') {
					i++;
					len++;
					if (pattern[i])
						i++;
				} else if (pattern[i] == '*') {
					break;
				} else if (pattern[i] == '?') {
					break;
				} else {
					i++;
					len++;
				}
			}

			item = xmalloc(sizeof(struct glob_item) + len + 1);
			item->type = GLOB_TEXT;

			str = item->text;
			i = start;
			j = 0;
			while (j < len) {
				if (pattern[i] == '\\') {
					i++;
					if (pattern[i]) {
						str[j++] = pattern[i++];
					} else {
						str[j++] = '\\';
					}
				} else {
					str[j++] = pattern[i++];
				}
			}
			str[j] = 0;
		}
		list_add_tail(&item->node, head);
	}
	simplify(head);
}

void glob_free(struct list_head *head)
{
	struct list_head *item = head->next;

	while (item != head) {
		struct glob_item *gi;
		struct list_head *next = item->next;

		gi = container_of(item, struct glob_item, node);
		free(gi);
		item = next;
	}
}

static int do_glob_match(struct list_head *head, struct list_head *first, const char *text)
{
	struct list_head *item = first;

	while (item != head) {
		struct glob_item *gitem;

		gitem = container_of(item, struct glob_item, node);
		if (gitem->type == GLOB_TEXT) {
			int len = u_strlen(gitem->text);

			if (!u_strncase_equal_base(gitem->text, text, len))
				return 0;
			text += strlen(gitem->text);
		} else if (gitem->type == GLOB_QMARK) {
			uchar u;
			int idx = 0;

			u = u_get_char(text, &idx);
			if (u == 0)
				return 0;
			text += idx;
		} else if (gitem->type == GLOB_STAR) {
			/* after star there MUST be normal text (or nothing),
			 * question marks have been moved before this star and
			 * other stars have been sripped (see simplify)
			 */
			struct list_head *next;
			struct glob_item *next_gi;
			const char *t;
			int tlen;

			next = item->next;
			if (next == head) {
				/* this star was the last item => matched */
				return 1;
			}
			next_gi = container_of(next, struct glob_item, node);
			BUG_ON(next_gi->type != GLOB_TEXT);
			t = next_gi->text;
			tlen = strlen(t);
			while (1) {
				const char *pos;

				pos = u_strcasestr_base(text, t);
				if (pos == NULL)
					return 0;
				if (do_glob_match(head, next->next, pos + tlen))
					return 1;
				text = pos + 1;
			}
		}
		item = item->next;
	}
	return text[0] == 0;
}

int glob_match(struct list_head *head, const char *text)
{
	return do_glob_match(head, head->next, text);
}
