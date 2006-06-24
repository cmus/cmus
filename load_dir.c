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
