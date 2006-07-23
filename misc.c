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

#include <misc.h>
#include <prog.h>
#include <xmalloc.h>
#include <xstrjoin.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdarg.h>

const char *cmus_config_dir = NULL;
const char *home_dir = NULL;
const char *user_name = NULL;

char **get_words(const char *text)
{
	char **words;
	int i, j, count;

	while (*text == ' ')
		text++;

	count = 0;
	i = 0;
	while (text[i]) {
		count++;
		while (text[i] && text[i] != ' ')
			i++;
		while (text[i] == ' ')
			i++;
	}
	words = xnew(char *, count + 1);

	i = 0;
	j = 0;
	while (text[i]) {
		int start = i;

		while (text[i] && text[i] != ' ')
			i++;
		words[j++] = xstrndup(text + start, i - start);
		while (text[i] == ' ')
			i++;
	}
	words[j] = NULL;
	return words;
}

int strptrcmp(const void *a, const void *b)
{
	const char *as = *(char **)a;
	const char *bs = *(char **)b;

	return strcmp(as, bs);
}

static int dir_exists(const char *dirname)
{
	DIR *dir;

	dir = opendir(dirname);
	if (dir == NULL) {
		if (errno == ENOENT)
			return 0;
		return -1;
	}
	closedir(dir);
	return 1;
}

static void make_dir(const char *dirname)
{
	int rc;

	rc = dir_exists(dirname);
	if (rc == 1)
		return;
	if (rc == -1)
		die_errno("error: opening `%s'", dirname);
	rc = mkdir(dirname, 0700);
	if (rc == -1)
		die_errno("error: creating directory `%s'", dirname);
}

static char *get_non_empty_env(const char *name)
{
	const char *val;

	val = getenv(name);
	if (val == NULL || val[0] == 0)
		return NULL;
	return xstrdup(val);
}

int misc_init(void)
{
	home_dir = get_non_empty_env("HOME");
	if (home_dir == NULL)
		die("error: environment variable HOME not set\n");

	user_name = get_non_empty_env("USER");
	if (user_name == NULL) {
		user_name = get_non_empty_env("USERNAME");
		if (user_name == NULL)
			die("error: neither USER or USERNAME environment variable set\n");
	}

	cmus_config_dir = get_non_empty_env("CMUS_HOME");
	if (cmus_config_dir == NULL)
		cmus_config_dir = xstrjoin(home_dir, "/.cmus");
	make_dir(cmus_config_dir);
	return 0;
}
