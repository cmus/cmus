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

#include <load_dir.h>
#include <xmalloc.h>
#include <xstrjoin.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

int load_dir(const char *dirname, char ***names, int *count, int dir_append_slash,
		int (*filter)(const char *name, const struct stat *s, void *data),
		int (*compare)(const void *a, const void *b),
		void *filter_data)
{
	DIR *d;
	struct dirent *dirent;
	char **ptrs;
	int nr_ptrs, ptrs_count;
	char *full_dir_name;

	d = opendir(dirname);
	if (d == NULL) {
		return -1;
	}
	full_dir_name = xstrjoin(dirname, "/");
	ptrs_count = 32;
	ptrs = xnew(char *, ptrs_count);
	nr_ptrs = 0;
	while ((dirent = readdir(d)) != NULL) {
		struct stat s;
		char *fullname;

		fullname = xstrjoin(full_dir_name, dirent->d_name);
		if (stat(fullname, &s) == 0 && filter(dirent->d_name, &s, filter_data)) {
			if (nr_ptrs == ptrs_count) {
				ptrs_count *= 2;
				ptrs = xrenew(char *, ptrs, ptrs_count);
			}
			if (dir_append_slash && S_ISDIR(s.st_mode)) {
				ptrs[nr_ptrs] = xstrjoin(dirent->d_name, "/");
			} else {
				ptrs[nr_ptrs] = xstrdup(dirent->d_name);
			}
			nr_ptrs++;
		}
		free(fullname);
	}
	free(full_dir_name);
	closedir(d);
	if (nr_ptrs) {
		qsort(ptrs, nr_ptrs, sizeof(char *), compare);
	} else {
		free(ptrs);
		ptrs = NULL;
	}
	*names = ptrs;
	*count = nr_ptrs;
	return 0;
}
