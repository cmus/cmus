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

#ifndef _XMALLOC_H
#define _XMALLOC_H

#include "compiler.h"
#ifdef HAVE_CONFIG
#include "config/xmalloc.h"
#endif

#include <stdlib.h>
#include <string.h>

void malloc_fail(void) __NORETURN;

#define xnew(type, n)		(type *)xmalloc(sizeof(type) * (n))
#define xnew0(type, n)		(type *)xmalloc0(sizeof(type) * (n))
#define xrenew(type, mem, n)	(type *)xrealloc(mem, sizeof(type) * (n))

static inline void * __MALLOC xmalloc(size_t size)
{
	void *ptr = malloc(size);

	if (unlikely(ptr == NULL))
		malloc_fail();
	return ptr;
}

static inline void * __MALLOC xmalloc0(size_t size)
{
	void *ptr = calloc(1, size);

	if (unlikely(ptr == NULL))
		malloc_fail();
	return ptr;
}

static inline void * __MALLOC xrealloc(void *ptr, size_t size)
{
	ptr = realloc(ptr, size);
	if (unlikely(ptr == NULL))
		malloc_fail();
	return ptr;
}

static inline char * __MALLOC xstrdup(const char *str)
{
#ifdef HAVE_STRDUP
	char *s = strdup(str);
	if (unlikely(s == NULL))
		malloc_fail();
	return s;
#else
	size_t size = strlen(str) + 1;
	void *ptr = xmalloc(size);
	return (char *) memcpy(ptr, str, size);
#endif
}

#ifdef HAVE_STRNDUP
static inline char * __MALLOC xstrndup(const char *str, size_t n)
{
	char *s = strndup(str, n);
	if (unlikely(s == NULL))
		malloc_fail();
	return s;
}
#else
char * __MALLOC xstrndup(const char *str, size_t n);
#endif

static inline void free_str_array(char **array)
{
	int i;

	if (array == NULL)
		return;
	for (i = 0; array[i]; i++)
		free(array[i]);
	free(array);
}

#endif
