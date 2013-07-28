/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2004 Timo Hirvonen
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

#include "mergesort.h"
#include "list.h"

void list_mergesort(struct list_head *head,
	int (*compare)(const struct list_head *, const struct list_head *))
{
	LIST_HEAD(empty);
	struct list_head *unsorted_head, *sorted_head, *p, *q, *tmp;
	int psize, qsize, K, count;

	if (list_empty(head))
		return;

	unsorted_head = head;
	sorted_head = &empty;
	K = 1;
	while (1) {
		p = unsorted_head->next;
		count = 0;
		do {
			q = p;
			psize = 0;
			while (psize < K) {
				if (q == unsorted_head)
					break;
				psize++;
				q = q->next;
			}
			qsize = K;
			while (1) {
				struct list_head *e;

				if (q == unsorted_head)
					qsize = 0;
				if (psize == 0 && qsize == 0)
					break;
				if (!psize || (qsize && compare(p, q) > 0)) {
					e = q;
					q = q->next;
					qsize--;
				} else {
					e = p;
					p = p->next;
					psize--;
				}
				list_del(e);
				list_add_tail(e, sorted_head);
			}
			count++;
			p = q;
		} while (p != unsorted_head);

		if (count == 1) {
			head->next = sorted_head->next;
			head->prev = sorted_head->prev;
			sorted_head->prev->next = head;
			sorted_head->next->prev = head;
			return;
		}
		tmp = unsorted_head;
		unsorted_head = sorted_head;
		sorted_head = tmp;
		K *= 2;
	}
}
