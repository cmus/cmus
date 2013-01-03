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

#include "xmalloc.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

extern char *program_name;

void malloc_fail(void)
{
	fprintf(stderr, "%s: could not allocate memory: %s\n", program_name, strerror(errno));
	exit(42);
}

#ifndef HAVE_STRNDUP
char *xstrndup(const char *str, size_t n)
{
	size_t len;
	char *s;

	for (len = 0; len < n && str[len]; len++)
		;
	s = xmalloc(len + 1);
	memcpy(s, str, len);
	s[len] = 0;
	return s;
}
#endif
