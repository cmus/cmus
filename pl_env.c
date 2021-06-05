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

static bool pl_env_contains_delimiter(const char *str)
{
	return !!strchr(str, PL_ENV_DELIMITER);
}

/**
 * pl_getenv_normalized gets an env var and puts it into the required format for
 * pl_env_reduce and pl_env_expand, which does exact string matching/replacement
 * against the file paths. It converts backslashes to slashes on Windows,
 * removes consecutive slashes, removes './' path segments, simplifies '../'
 * path segments (and returns NULL if it would result in going above the
 * uppermost directory), and removes the trailing slash. In addition, it trims
 * the variable name before looking it up.
 */
static char *pl_env_getenv_normalized(const char *var)
{
	/* duplicate the var */
	char *var_d = xstrdup(var);

	/* trim the var */
	char *var_s = var_d;
	char *var_e = var_d + strlen(var_d);
	while (var_s < var_e && isspace(*var_s)) var_s++;
	while (var_e > var_s && isspace(*(var_e-1))) var_e--;
	*var_e = '\0';

	/* if it's empty, return NULL */
	if (var_e-var_s == 0) return NULL;

	/* get the var */
	char *val = getenv(var_s);
	free(var_d);

	/* if it's nonexistent or empty, return NULL */
	if (!val || !*val) return NULL;

	/* duplicate the value */
	char *new = xstrdup(val);

#ifdef _WIN32
	/* convert backslashes to slashes */
	/* note: cmus uses forward slashes internally, but Windows accepts both */
	/* even though they will get normalized on windows, we need paths to match exactly */
	for (char *p = path; *p; p++)
		*p = *p == '\\' ? '/' : *p;
#endif

	/* canonicalize the path in-place */
	size_t r = 1, w = 1;
	while (new[r]) {

		/* handle the start of a segment */
		if (w == 0 || new[w-1] == '/') {

			/* handle empty segments */
			if (new[r] == '/') {

				/* skip the duplicate slashes */
				while (new[r] == '/') r++;

				continue;
			}

			/* handle '.' segments */
			if (new[r] == '.' && (new[r+1] == '/' || !new[r+1])) {

				/* skip them */
				if (new[r += 1]) r++;

				continue;
			}

			/* handle '..' segments */
			if (new[r] == '.' && new[r+1] == '.' && (new[r+2] == '/' || !new[r+2])) {

				/* if there aren't any parent directories left to skip, return NULL */
				if (!w) return NULL;

				/* remove the previous segment up to the '/' */
				for (w--; w && new[w-1] != '/'; ) w--;

				/* skip the '..' */
				if (new[r += 2]) r++;

				continue;
			}
		}

		/* write the next character */
		new[w++] = new[r++];
	}

	/* remove the trailing slash if the path isn't / */
	if (w >= 2 && new[w-1] == '/') {
		w--;
	}

	/* terminate the path */
	new[w] = '\0';

	return new;
}

char *pl_env_reduce(const char *path)
{
	if (!pl_env_vars || !*pl_env_vars || pl_env_var(path, NULL))
		return xstrdup(path);

	for (char **var = pl_env_vars; *var && **var; var++) {
		char *val = pl_env_getenv_normalized(*var);
		if (!val)
			continue;
		if (!*val || pl_env_contains_delimiter(val)) {
			free(val);
			continue;
		}

		size_t val_len = strlen(val);

#ifdef _WIN32
		if (strncasecmp(path, val, val_len) != 0) {
			free(val);
			continue;
		}
#else
		if (strncmp(path, val, val_len) != 0) {
			free(val);
			continue;
		}
#endif

		const char *rem = path + val_len;

		/* always keep the slash at the beginning of the path, and only
		   use the env var if it replaces an entire path component (i.e.
		   it is a directory) */
		if (*rem != '/') {
			free(val);
			continue;
		}

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

		free(val);
		return new;
	}

	return xstrdup(path);
}

char *pl_env_expand(const char *path)
{
	if (!path)
		return NULL;

	const char *rem;
	char *var = pl_env_var_str(path, &rem);
	if (!var)
		return xstrdup(path);

	char *val = pl_env_getenv_normalized(var);
	free(var);
	if (!val)
		return xstrdup(path);
	if (!*val || pl_env_contains_delimiter(val)) {
		free(val);
		return xstrdup(path);
	}

	size_t val_len = strlen(val);
	size_t rem_len = strlen(rem);

	char *new, *ptr;
	new = ptr = xmalloc(val_len+rem_len+1);
	strcpy(ptr, val);
	ptr += val_len;
	strcpy(ptr, rem);
	ptr += rem_len;
	*ptr = '\0';

	free(val);
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

char *pl_env_var_str(const char *path, const char **remainder)
{
	int len;
	const char *var;
	if (!(var = pl_env_var(path, &len)))
		return NULL;
	if (remainder)
		*remainder = pl_env_var_remainder(path, len);
	return xstrndup(var, len);
}

int pl_env_var_len(const char *path)
{
	int len;
	return pl_env_var(path, &len) ? len : 0;
}
