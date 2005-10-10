/* 
 * Copyright 2005 Timo Hirvonen
 */

#include <glob.h>
#include <uchar.h>
#include <list.h>
#include <xmalloc.h>

struct glob_item {
	struct list_head node;
	enum {
		GLOB_STAR,
		GLOB_QMARK,
		GLOB_TEXT
	} type;
	char *text;
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
			struct glob_item *ri;

			ri = container_of(i, struct glob_item, node);
			if (ri->type == GLOB_STAR) {
				scount++;
			} else if (ri->type == GLOB_QMARK) {
				qcount++;
			} else {
				/* */
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
				struct glob_item *ri;

				ri = container_of(i, struct glob_item, node);
				i = i->next;
				if (ri->type == GLOB_QMARK) {
					list_del(&ri->node);
					list_add(&ri->node, insert_after);
					insert_after = &ri->node;
					qcount--;
				}
			}

			i = item;
			while (scount > 1) {
				struct glob_item *ri;

				ri = container_of(i, struct glob_item, node);
				i = i->next;
				if (ri->type == GLOB_STAR) {
					list_del(&ri->node);
					free(ri->text);
					free(ri);
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

		item = xnew(struct glob_item, 1);
		if (pattern[i] == '*') {
			item->type = GLOB_STAR;
			item->text = NULL;
			i++;
		} else if (pattern[i] == '?') {
			item->type = GLOB_QMARK;
			item->text = NULL;
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
			str = xnew(char, len + 1);
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

			item->type = GLOB_TEXT;
			item->text = str;
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
		free(gi->text);
		free(gi);
		item = next;
	}
}

static int do_glob_match(struct list_head *head, struct list_head *first, const char *text)
{
	struct list_head *item = first;

	while (item != head) {
		struct glob_item *ritem;

		ritem = container_of(item, struct glob_item, node);
		if (ritem->type == GLOB_TEXT) {
			int len = strlen(ritem->text);
			if (strncmp(ritem->text, text, len))
				return 0;
			text += len;
		} else if (ritem->type == GLOB_QMARK) {
			if (text[0] == 0)
				return 0;
			text++;
		} else if (ritem->type == GLOB_STAR) {
			/* after star there MUST be normal text (or nothing),
			 * question marks have been moved before this star and
			 * other stars have been sripped (see simplify)
			 */
			struct list_head *i;
			struct glob_item *gi;
			const char *t;
			int tlen;

			i = item->next;
			if (i == head) {
				/* this star was the last item => matched */
				return 1;
			}
			gi = container_of(i, struct glob_item, node);
			if (gi->type != GLOB_TEXT) {
				return 0;
			}
			t = gi->text;
			tlen = strlen(t);
			while (1) {
				const char *pos;

				pos = u_strcasestr(text, t);
				if (pos == NULL)
					return 0;
				if (do_glob_match(head, i->next, pos + tlen))
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
