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

#ifndef _XMALLOC_H
#define _XMALLOC_H

#include <stdlib.h>
#include <string.h>

#if !defined(likely)

#if defined(__GNUC__) && (__GNUC__ > 2)

#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)
#define __noreturn	__attribute__((__noreturn__))

#else

#define likely(x)	(x)
#define unlikely(x)	(x)
#define __noreturn

#endif

#endif /* !defined(likely) */

extern void malloc_fail(void) __noreturn;

#define xnew(type, n)		(type *)xmalloc(sizeof(type) * (n))
#define xnew0(type, n)		(type *)xmalloc0(sizeof(type) * (n))
#define xrenew(type, mem, n)	(type *)xrealloc(mem, sizeof(type) * (n))

static inline void *xmalloc(size_t size)
{
	void *ptr = malloc(size);

	if (unlikely(ptr == NULL))
		malloc_fail();
	return ptr;
}

static inline void *xmalloc0(size_t size)
{
	void *ptr = calloc(1, size);

	if (unlikely(ptr == NULL))
		malloc_fail();
	return ptr;
}

static inline void *xrealloc(void *ptr, size_t size)
{
	ptr = realloc(ptr, size);
	if (unlikely(ptr == NULL))
		malloc_fail();
	return ptr;
}

static inline char *xstrdup(const char *str)
{
	char *s = strdup(str);

	if (unlikely(s == NULL))
		malloc_fail();
	return s;
}

extern char *xstrndup(const char *str, size_t n);

static inline char *xxstrdup(const char *str)
{
	if (str == NULL)
		return NULL;
	return xstrdup(str);
}

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
