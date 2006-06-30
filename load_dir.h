/* 
 * Copyright Timo Hirvonen
 */

#ifndef _LOAD_DIR_H
#define _LOAD_DIR_H

#include <sys/stat.h>
#include <stdlib.h>
#include <dirent.h>

struct directory {
	DIR *d;
	int len;
	char path[1024];
};

int dir_open(struct directory *dir, const char *name);
void dir_close(struct directory *dir);
const char *dir_read(struct directory *dir, struct stat *st);


struct ptr_array {
	/* allocated with malloc(). contains pointers */
	void *ptrs;
	int alloc;
	int count;
};

/* ptr_array.ptrs is either char ** or struct dir_entry ** */
struct dir_entry {
	mode_t mode;
	char name[0];
};

#define PTR_ARRAY(name) struct ptr_array name = { NULL, 0, 0 }

void ptr_array_add(struct ptr_array *array, void *ptr);

static inline void ptr_array_sort(struct ptr_array *array,
		int (*cmp)(const void *a, const void *b))
{
	int count = array->count;
	if (count)
		qsort(array->ptrs, count, sizeof(void *), cmp);
}

#endif
