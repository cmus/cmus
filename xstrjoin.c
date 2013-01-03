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
#include "string.h"

char *xstrjoin(const char *a, const char *b)
{
	int a_len, b_len;
	char *joined;

	a_len = strlen(a);
	b_len = strlen(b);
	joined = xnew(char, a_len + b_len + 1);
	memcpy(joined, a, a_len);
	memcpy(joined + a_len, b, b_len + 1);
	return joined;
}
