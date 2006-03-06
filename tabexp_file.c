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

#include <tabexp_file.h>
#include <tabexp.h>
#include <load_dir.h>
#include <misc.h>
#include <xmalloc.h>
#include <xstrjoin.h>

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

static const char *starting_with;

static int name_filter(const char *name)
{
	int len = strlen(starting_with);

	if (len == 0) {
		if (name[0] == '.')
			return 0;
	} else {
		if (strncmp(name, starting_with, len))
			return 0;
	}
	return 1;
}

static int (*user_filter)(const char *name, const struct stat *s);

static int load_dir_filter(const char *name, const struct stat *s)
{
	return name_filter(name) && user_filter(name, s);
}

/*
 * load all directory entries from directory 'dir' starting with 'start' and
 * filtered with 'filter'
 */
static void tabexp_load_dir(const char *dir, const char *start)
{
	char **names;
	int count;
	char *full_dir_name;

	/* for name_filter() */
	starting_with = start;

	/* tabexp is resetted */
	full_dir_name = get_full_dir_name(dir);
	if (full_dir_name == NULL)
		return;

	count = load_dir(full_dir_name, &names, load_dir_filter, strptrcmp);
	free(full_dir_name);
	if (count <= 0) {
		/* opendir failed or no matches */
		return;
	}

	tabexp.head = xstrdup(dir);
	tabexp.tails = names;
	tabexp.nr_tails = count;
}

void expand_files_and_dirs(const char *src,
		int (*filter)(const char *name, const struct stat *s))
{
	char *slash;

	/* for load_dir_filter() */
	user_filter = filter;

	/* split src to dir and file */
	slash = strrchr(src, '/');
	if (slash) {
		char *dir;
		const char *file;

		/* split */
		dir = xstrndup(src, slash - src + 1);
		file = slash + 1;
		/* get all dentries starting with file from dir */
		tabexp_load_dir(dir, file);
		free(dir);
	} else {
		if (src[0] == '~') {
			char *home = get_home(src + 1);

			if (home) {
				tabexp.head = xstrdup("");
				tabexp.nr_tails = 1;
				tabexp.tails = xnew(char *, 2);
				tabexp.tails[0] = home;
				tabexp.tails[1] = NULL;
			}
		} else {
			tabexp_load_dir("", src);
		}
	}
}
