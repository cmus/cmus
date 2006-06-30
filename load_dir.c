/*
 * Copyright Timo Hirvonen
 */

#include "load_dir.h"
#include "xmalloc.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

int dir_open(struct directory *dir, const char *name)
{
	int len = strlen(name);

	if (len >= sizeof(dir->path) - 2) {
		errno = ENAMETOOLONG;
		return -1;
	}

	dir->d = opendir(name);
	if (!dir->d)
		return -1;

	memcpy(dir->path, name, len);
	dir->path[len++] = '/';
	dir->path[len] = 0;
	dir->len = len;
	return 0;
}

void dir_close(struct directory *dir)
{
	closedir(dir->d);
}

const char *dir_read(struct directory *dir, struct stat *st)
{
	DIR *d = dir->d;
	int len = dir->len;
	char *full = dir->path;
	struct dirent *de;

	while ((de = readdir(d))) {
		const char *name = de->d_name;
		int nlen = strlen(name);

		/* just ignore too long paths
		 * + 2 -> space for \0 or / and \0
		 */
		if (len + nlen + 2 >= sizeof(dir->path))
			continue;

		memcpy(full + len, name, nlen + 1);
		if (stat(full, st))
			continue;

		return full + len;
	}
	return NULL;
}

void ptr_array_add(struct ptr_array *array, void *ptr)
{
	void **ptrs = ptrs = array->ptrs;
	int alloc = array->alloc;

	if (alloc == array->count) {
		alloc = alloc * 3 / 2 + 16;
		ptrs = xrealloc(ptrs, alloc * sizeof(void *));
		array->ptrs = ptrs;
		array->alloc = alloc;
	}
	ptrs[array->count++] = ptr;
}
