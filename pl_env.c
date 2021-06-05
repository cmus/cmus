/*
 * Copyright 2021-2023 Patrick Gaskin
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

#include "pl_env.h"
#include "options.h"
#include "xmalloc.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

extern char **environ;
static char **pl_env_cache = NULL;

static bool pl_env_contains_delimiter(const char *str)
{
	return !!strchr(str, PL_ENV_DELIMITER);
}

/**
 * pl_env_norm gets an env var and puts it into the required format for
 * pl_env_reduce and pl_env_expand, which does exact string matching/replacement
 * against the file paths. It converts backslashes to slashes on Windows,
 * removes consecutive slashes, removes './' path segments, simplifies '../'
 * path segments (and returns NULL if it would result in going above the
 * uppermost directory), and removes the trailing slash. In addition, it trims
 * the variable name before looking it up.
 */
static char *pl_env_norm(char *path)
{
#ifdef _WIN32
	/* convert backslashes to slashes */
	/* note: cmus uses forward slashes internally, but Windows accepts both */
	/* even though they will get normalized on windows, we need paths to match exactly */
	for (char *p = path; *p; p++)
		*p = *p == '\\' ? '/' : *p;
#endif

	/* canonicalize the path in-place */
	size_t r = 1, w = 1;
	while (path[r]) {

		/* handle the start of a segment */
		if (w == 0 || path[w-1] == '/') {

			/* handle empty segments */
			if (path[r] == '/') {

				/* skip the duplicate slashes */
				while (path[r] == '/') r++;

				continue;
			}

			/* handle '.' segments */
			if (path[r] == '.' && (path[r+1] == '/' || !path[r+1])) {

				/* skip them */
				if (path[r += 1]) r++;

				continue;
			}

			/* handle '..' segments */
			if (path[r] == '.' && path[r+1] == '.' && (path[r+2] == '/' || !path[r+2])) {

				/* if there aren't any parent directories left to skip, return NULL */
				if (!w) return NULL;

				/* remove the previous segment up to the '/' */
				for (w--; w && path[w-1] != '/'; ) w--;

				/* skip the '..' */
				if (path[r += 2]) r++;

				continue;
			}
		}

		/* write the next character */
		path[w++] = path[r++];
	}

	/* remove the trailing slash if the path isn't / */
	if (w >= 2 && path[w-1] == '/') {
		w--;
	}

	/* terminate the path */
	path[w] = '\0';

	return path;
}

/**
 * pl_env_get is like getenv, but it allows using non-null-terminated variable
 * names, trims the variable name, ensures the environment variable doesn't
 * contain the marker used by pl_env, and normalizes the paths in environment
 * variables with pl_env_norm.
 */
static const char *pl_env_get(const char *var, int var_len)
{
	if (!var)
		return NULL;

	size_t vl = var_len == -1
		? strlen(var)
		: var_len;

	const char *vs = var;
	const char *ve = var + vl;
	while (vs < ve && isspace(*vs)) vs++;
	while (ve > vs && isspace(*(ve-1))) ve--;
	vl = ve-vs;

	if (!vl)
		return NULL;

	for (const char *c = vs; c < ve; c++)
		if (*c == PL_ENV_DELIMITER || *c == '=')
			return NULL;

	for (char **x = pl_env_cache; x && *x; x++)
		if (strncmp(*x, vs, vl) == 0 && (*x)[vl] == '=')
			return *x + vl + 1;

	return NULL;
}

void pl_env_init(void)
{
	for (char **x = pl_env_cache; x && *x; x++)
		free(*x);
	free(pl_env_cache);

	size_t n = 0;
	for (char **x = environ; *x; x++)
		n++;

	char **new = pl_env_cache = xnew(char*, n+1);
	for (char **x = environ; *x; x++)
		if (!pl_env_contains_delimiter(*x))
			if (!pl_env_norm(strchr((*new++ = xstrdup(*x)), '=') + 1))
				free(*--new);
	*new = NULL;
}

char *pl_env_reduce(const char *path)
{
	if (!pl_env_vars || !*pl_env_vars || pl_env_var(path, NULL))
		return xstrdup(path);

	for (char **var = pl_env_vars; *var && **var; var++) {
		const char *val = pl_env_get(*var, -1);
		if (!val)
			continue;

		size_t val_len = strlen(val);

#ifdef _WIN32
		if (strncasecmp(path, val, val_len) != 0)
			continue;
#else
		if (strncmp(path, val, val_len) != 0)
			continue;
#endif

		const char *rem = path + val_len;

		/* always keep the slash at the beginning of the path, and only
		   use the env var if it replaces an entire path component (i.e.
		   it is a directory) */
		if (*rem != '/')
			continue;

		size_t var_len = strlen(*var);
		size_t rem_len = strlen(rem);

		char *new, *ptr;
		new = ptr = xmalloc(1+var_len+1+rem_len+1);
		*ptr++ = PL_ENV_DELIMITER;
		strncpy(ptr, *var, var_len);
		ptr += var_len;
		*ptr++ = PL_ENV_DELIMITER;
		strcpy(ptr, rem);
		ptr += rem_len;
		*ptr = '\0';

		return new;
	}

	return xstrdup(path);
}

char *pl_env_expand(const char *path)
{
	if (!path)
		return NULL;

	int len;
	const char *var;
	if (!(var = pl_env_var(path, &len)))
		return xstrdup(path);

	const char *val = pl_env_get(var, len);
	if (!val)
		return xstrdup(path);

	const char *rem = pl_env_var_remainder(path, len);
	size_t val_len = strlen(val);
	size_t rem_len = strlen(rem);

	char *new, *ptr;
	new = ptr = xmalloc(val_len+rem_len+1);
	strcpy(ptr, val);
	ptr += val_len;
	strcpy(ptr, rem);
	ptr += rem_len;
	*ptr = '\0';

	return new;
}

const char *pl_env_var(const char *path, int *out_length)
{
	const char *end;
	if (!path || *path++ != PL_ENV_DELIMITER)
		return NULL;
	if (!(end = strrchr(path, PL_ENV_DELIMITER)) || path == end)
		return NULL;
	if (out_length)
		*out_length = (int) (end-path);
	return path;
}

const char *pl_env_var_remainder(const char *path, int length)
{
	return path+length+2;
}

int pl_env_var_len(const char *path)
{
	int len;
	return pl_env_var(path, &len) ? len : 0;
}
