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

#include "xstrjoin.h"
#include "xmalloc.h"
#include "utils.h"

char *xstrjoin_slice(struct slice slice)
{
	const char **str = slice.ptr;
	size_t i, pos = 0, len = 0;
	char *joined;
	size_t *lens;

	lens = xnew(size_t, slice.len);
	for (i = 0; i < slice.len; i++) {
		lens[i] = strlen(str[i]);
		len += lens[i];
	}

	joined = xnew(char, len + 1);
	for (i = 0; i < slice.len; i++) {
		memcpy(joined + pos, str[i], lens[i]);
		pos += lens[i];
	}
	joined[len] = 0;

	free(lens);

	return joined;
}
