/*
 * Copyright 2008-2011 Various Authors
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

#include "tabexp_file.h"
#include "tabexp.h"
#include "load_dir.h"
#include "misc.h"
#include "xmalloc.h"
#include "xstrjoin.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <dirent.h>

static char *get_home(const char *user)
{
	struct passwd *passwd;
	char *home;
	int len;

	if (user[0] == 0) {
		passwd = getpwuid(getuid());
	} else {
		passwd = getpwnam(user);
	}
	if (passwd == NULL)
		return NULL;
	len = strlen(passwd->pw_dir);
	home = xnew(char, len + 2);
	memcpy(home, passwd->pw_dir, len);
	home[len] = '/';
	home[len + 1] = 0;
	return home;
}

static char *get_full_dir_name(const char *dir)
{
	char *full;

	if (dir[0] == 0) {
		full = xstrdup("./");
	} else if (dir[0] == '~') {
		char *first_slash, *tmp, *home;

		first_slash = strchr(dir, '/');
		tmp = xstrndup(dir, first_slash - dir);
		home = get_home(tmp + 1);
		free(tmp);
		if (home == NULL)
			return NULL;
		full = xstrjoin(home, first_slash);
		free(home);
	} else {
		full = xstrdup(dir);
	}
	return full;
}

/*
 * load all directory entries from directory 'dir' starting with 'start' and
 * filtered with 'filter'
 */
static void tabexp_load_dir(const char *dirname, const char *start,
		int (*filter)(const char *, const struct stat *))
{
	int start_len = strlen(start);
	struct directory dir;
	PTR_ARRAY(array);
	const char *name;
	char *full_dir_name;

	/* tabexp is reseted */
	full_dir_name = get_full_dir_name(dirname);
	if (!full_dir_name)
		return;

	if (dir_open(&dir, full_dir_name))
		goto out;

	while ((name = dir_read(&dir))) {
		char *str;

		if (!start_len) {
			if (name[0] == '.')
				continue;
		} else {
			if (strncmp(name, start, start_len))
				continue;
		}

		if (!filter(name, &dir.st))
			continue;

		if (S_ISDIR(dir.st.st_mode)) {
			int len = strlen(name);

			str = xnew(char, len + 2);
			memcpy(str, name, len);
			str[len++] = '/';
			str[len] = 0;
		} else {
			str = xstrdup(name);
		}
		ptr_array_add(&array, str);
	}
	dir_close(&dir);
	if (array.count) {
		ptr_array_sort(&array, strptrcmp);

		tabexp.head = xstrdup(dirname);
		tabexp.tails = array.ptrs;
		tabexp.count = array.count;
	}
out:
	free(full_dir_name);
}

void expand_files_and_dirs(const char *src,
		int (*filter)(const char *name, const struct stat *s))
{
	char *slash;

	/* split src to dir and file */
	slash = strrchr(src, '/');
	if (slash) {
		char *dir;
		const char *file;

		/* split */
		dir = xstrndup(src, slash - src + 1);
		file = slash + 1;
		/* get all dentries starting with file from dir */
		tabexp_load_dir(dir, file, filter);
		free(dir);
	} else {
		if (src[0] == '~') {
			char *home = get_home(src + 1);

			if (home) {
				tabexp.head = xstrdup("");
				tabexp.tails = xnew(char *, 1);
				tabexp.tails[0] = home;
				tabexp.count = 1;
			}
		} else {
			tabexp_load_dir("", src, filter);
		}
	}
}
