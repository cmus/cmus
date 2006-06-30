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

int load_dir(const char *dirname, char ***names,
		int (*filter)(const char *name, const struct stat *s),
		int (*compare)(const void *a, const void *b))
{
	struct dirent *dirent;
	char full_path[1024];
	char **ptrs = NULL;
	int count = 0;
	int alloc = 0;
	int len = strlen(dirname);
	DIR *d;

	d = opendir(dirname);
	if (d == NULL)
		return -1;

	if (len >= sizeof(full_path) - 2) {
		free(ptrs);
		errno = ENAMETOOLONG;
		return -1;
	}

	memcpy(full_path, dirname, len);
	full_path[len++] = '/';

	while ((dirent = readdir(d)) != NULL) {
		const char *name = dirent->d_name;
		struct stat s;
		int nlen = strlen(name);

		/* ignore .. if we are in the root dir */
		if (strcmp(dirname,"/") == 0 && strcmp(name,"..") == 0)
			continue;

		/* just ignore too long paths */
		if (len + nlen + 2 >= sizeof(full_path))
			continue;

		memcpy(full_path + len, name, nlen + 1);
		if (stat(full_path, &s) == 0 && filter(name, &s)) {
			if (S_ISDIR(s.st_mode)) {
				full_path[len + nlen] = '/';
				full_path[len + nlen + 1] = 0;
			}
			ptrs = str_array_add(ptrs, &alloc, &count, xstrdup(full_path + len));
		}
	}
	closedir(d);

	if (count)
		qsort(ptrs, count, sizeof(char *), compare);

	*names = ptrs;
	return count;
}

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
