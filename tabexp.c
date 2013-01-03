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

#include "tabexp.h"
#include "xmalloc.h"
#include "xstrjoin.h"
#include "debug.h"

#include <stdlib.h>

struct tabexp tabexp = {
	.head = NULL,
	.tails = NULL,
	.count = 0
};

char *tabexp_expand(const char *src, void (*load_matches)(const char *src), int direction)
{
	static int idx = -1;
	char *expanded;

	if (tabexp.tails == NULL) {
		load_matches(src);
		if (tabexp.tails == NULL) {
			BUG_ON(tabexp.head != NULL);
			return NULL;
		}
		BUG_ON(tabexp.head == NULL);
		idx = -1;
	}
	idx += direction;

	if (idx >= tabexp.count)
		idx = 0;
	else if (idx < 0)
		idx = tabexp.count - 1;

	expanded = xstrjoin(tabexp.head, tabexp.tails[idx]);
	if (tabexp.count == 1)
		tabexp_reset();
	return expanded;
}

void tabexp_reset(void)
{
	int i;
	for (i = 0; i < tabexp.count; i++)
		free(tabexp.tails[i]);
	free(tabexp.tails);
	free(tabexp.head);
	tabexp.tails = NULL;
	tabexp.head = NULL;
	tabexp.count = 0;
}
