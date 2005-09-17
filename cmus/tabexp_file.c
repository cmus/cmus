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
#include <xmalloc.h>
#include <xstrjoin.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <dirent.h>

struct tabexp_file_private {
	char **extensions;
	unsigned int flags;
	const char *start;
};

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

static int strptrcmp(const void *a, const void *b)
{
	const char *as = *(char **)a;
	const char *bs = *(char **)b;

	return strcmp(as, bs);
}

static int filter(const char *name, const struct stat *s, void *data)
{
	struct tabexp_file_private *priv = data;
	const char *start = priv->start;
	const char *ext;
	int start_len, i;

	start_len = strlen(start);
	if (start_len == 0) {
		if (name[0] == '.')
			return 0;
	} else {
		if (strncmp(name, start, start_len))
			return 0;
	}
	if (S_ISDIR(s->st_mode))
		return 1;

	if ((priv->flags & TABEXP_FILE_FLAG_FILES) == 0)
		return 0;

	if (priv->extensions == NULL) {
		/* no extensions => accept all */
		return 1;
	}
	ext = strrchr(name, '.');
	if (ext == NULL)
		return 0;
	ext++;
	for (i = 0; priv->extensions[i]; i++) {
		if (strcmp(priv->extensions[i], ext) == 0)
			return 1;
	}
	return 0;
}

/*
 * load all directory entries from directory 'dir' starting with 'start' and
 * filtered with 'filter'
 */
static void tabexp_load_dir(struct tabexp *tabexp, const char *dir, const char *start)
{
	struct tabexp_file_private *priv = tabexp->private_data;
	char **ptrs;
	int nr_ptrs, rc;
	char *full_dir_name;

	/* tabexp is resetted */
	full_dir_name = get_full_dir_name(dir);
	if (full_dir_name == NULL)
		return;

	priv->start = start;
	rc = load_dir(full_dir_name, &ptrs, &nr_ptrs, 1, filter, strptrcmp, priv);
	free(full_dir_name);
	if (rc) {
		/* opendir failed, usually permission denied */
		return;
	}
	if (nr_ptrs == 0) {
		/* no matches */
		return;
	}

	tabexp->head = xstrdup(dir);
	tabexp->tails = ptrs;
	tabexp->nr_tails = nr_ptrs;
	tabexp->index = 0;
}

static void load_matching_files(struct tabexp *tabexp, const char *src)
{
	char *slash;

	/* tabexp is resetted */

	/* split src to dir and file */
	slash = strrchr(src, '/');
	if (slash) {
		char *dir;
		const char *file;

		/* split */
		dir = xstrndup(src, slash - src + 1);
		file = slash + 1;
		/* get all dentries starting with file from dir */
		tabexp_load_dir(tabexp, dir, file);
		free(dir);
	} else {
		if (src[0] == '~') {
			char *home = get_home(src + 1);

			if (home) {
				tabexp->head = xstrdup("");
				tabexp->nr_tails = 1;
				tabexp->index = 0;
				tabexp->tails = xnew(char *, 2);
				tabexp->tails[0] = home;
				tabexp->tails[1] = NULL;
			}
		} else {
			tabexp_load_dir(tabexp, "", src);
		}
	}
}

struct tabexp *tabexp_file_new(unsigned int flags, char **extensions)
{
	struct tabexp_file_private *priv;

	priv = xnew(struct tabexp_file_private, 1);
	priv->flags = flags;
	priv->extensions = extensions;
	return tabexp_new(load_matching_files, priv);
}

void tabexp_file_free(struct tabexp *tabexp)
{
	struct tabexp_file_private *priv = tabexp->private_data;

	free(priv);
	tabexp_free(tabexp);
}
