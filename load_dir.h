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

#ifndef _LOAD_DIR_H
#define _LOAD_DIR_H

#include <sys/stat.h>
#include <stdlib.h>
#include <dirent.h>

struct directory {
	DIR *d;
	int len;
	/* we need stat information for symlink targets */
	int is_link;
	/* stat() information. ie. for the symlink target */
	struct stat st;
	char path[1024];
};

int dir_open(struct directory *dir, const char *name);
void dir_close(struct directory *dir);
const char *dir_read(struct directory *dir);


struct ptr_array {
	/* allocated with malloc(). contains pointers */
	void *ptrs;
	int alloc;
	int count;
};

/* ptr_array.ptrs is either char ** or struct dir_entry ** */
struct dir_entry {
	mode_t mode;
	char name[];
};

#define PTR_ARRAY(name) struct ptr_array name = { NULL, 0, 0 }

void ptr_array_add(struct ptr_array *array, void *ptr);

static inline void ptr_array_plug(struct ptr_array *array)
{
	ptr_array_add(array, NULL);
	array->count--;
}

static inline void ptr_array_sort(struct ptr_array *array,
		int (*cmp)(const void *a, const void *b))
{
	int count = array->count;
	if (count)
		qsort(array->ptrs, count, sizeof(void *), cmp);
}

static inline void ptr_array_unique(struct ptr_array *array,
		int (*cmp)(const void *a, const void *b))
{
	void **ptrs = array->ptrs;
	int i, j = 0;

	for (i = 1; i < array->count; i++) {
		if (cmp(&ptrs[i-1], &ptrs[i]) != 0)
			ptrs[j++] = ptrs[i];
	}
	array->count = j;
}

#endif
