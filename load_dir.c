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

const char *dir_read(struct directory *dir)
{
	DIR *d = dir->d;
	int len = dir->len;
	char *full = dir->path;
	struct dirent *de;

#if defined(__CYGWIN__)
	/* Fix for cygwin "hang" bug when browsing /
	 * Windows treats // as a network path.
	 */
	if (strcmp(full, "//") == 0)
		full++;
#endif

	while ((de = (struct dirent *) readdir(d))) {
		const char *name = de->d_name;
		int nlen = strlen(name);

		/* just ignore too long paths
		 * + 2 -> space for \0 or / and \0
		 */
		if (len + nlen + 2 >= sizeof(dir->path))
			continue;

		memcpy(full + len, name, nlen + 1);
		if (lstat(full, &dir->st))
			continue;

		dir->is_link = 0;
		if (S_ISLNK(dir->st.st_mode)) {
			/* argh. must stat the target */
			if (stat(full, &dir->st))
				continue;
			dir->is_link = 1;
		}

		return full + len;
	}
	return NULL;
}

void ptr_array_add(struct ptr_array *array, void *ptr)
{
	void **ptrs = array->ptrs;
	int alloc = array->alloc;

	if (alloc == array->count) {
		alloc = alloc * 3 / 2 + 16;
		ptrs = xrealloc(ptrs, alloc * sizeof(void *));
		array->ptrs = ptrs;
		array->alloc = alloc;
	}
	ptrs[array->count++] = ptr;
}
