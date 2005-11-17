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

#include <tabexp.h>
#include <xmalloc.h>
#include <xstrjoin.h>
#include <debug.h>

#include <stdlib.h>

struct tabexp tabexp = { NULL, NULL, 0, -1 };

char *tabexp_expand(const char *src, void (*load_matches)(const char *src))
{
	if (tabexp.tails == NULL) {
		char *expanded;

		load_matches(src);
		if (tabexp.tails == NULL) {
			BUG_ON(tabexp.head != NULL);
			BUG_ON(tabexp.nr_tails != 0);
			BUG_ON(tabexp.index != -1);
			return NULL;
		}

		BUG_ON(tabexp.head == NULL);
		BUG_ON(tabexp.nr_tails < 1);
		BUG_ON(tabexp.index != 0);

		expanded = xstrjoin(tabexp.head, tabexp.tails[tabexp.index]);
		if (tabexp.nr_tails == 1)
			tabexp_reset();
		return expanded;
	} else {
		tabexp.index++;
		tabexp.index %= tabexp.nr_tails;
		return xstrjoin(tabexp.head, tabexp.tails[tabexp.index]);
	}
}

void tabexp_reset(void)
{
	int i;

	for (i = 0; i < tabexp.nr_tails; i++)
		free(tabexp.tails[i]);
	free(tabexp.tails);
	free(tabexp.head);
	tabexp.tails = NULL;
	tabexp.head = NULL;
	tabexp.nr_tails = 0;
	tabexp.index = -1;
}
