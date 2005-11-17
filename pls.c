/* 
 * Copyright 2005 Timo Hirvonen
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

#include <pls.h>
#include <xmalloc.h>

/* get next non-empty line */
static int get_line(const char *contents, int *posp, char **linep, size_t *sizep)
{
	int pos = *posp;
	char *line = *linep;
	size_t size = *sizep;
	int start = pos;

	while (1) {
		int len;

		if (contents[pos] == 0) {
			if (pos == start)
				return 0;
		} else if (contents[pos] == '\n' || contents[pos] == '\r') {
			if (pos == start) {
				pos++;
				start = pos;
				continue;
			}
		} else {
			pos++;
			continue;
		}

		len = pos - start;
		if (len + 1 > size) {
			size = len + 1;
			line = xrenew(char, line, size);
			*linep = line;
			*sizep = size;
		}
		memcpy(line, contents + start, len);
		line[len] = 0;
		*posp = pos + 1;
		return 1;
	}
}

static int get_key_val(const char *contents, int *posp, char **linep, size_t *sizep, char **valp)
{
	char *val;

	if (!get_line(contents, posp, linep, sizep))
		return 0;
	val = strchr(*linep, '=');
	if (val == NULL)
		return -1;
	*val = 0;
	*valp = val + 1;
	return 1;
}

char **pls_get_files(const char *contents)
{
	int pos = 0;
	char *line = NULL;
	size_t size = 0;
	int fcount, falloc;
	char **files;

	if (!get_line(contents, &pos, &line, &size) || strncasecmp(line, "[playlist]", 10)) {
		free(line);
		return NULL;
	}

	fcount = 0;
	falloc = 8;
	files = xnew(char *, falloc);

	while (1) {
		/*
		 * FileN=...
		 * TitleN=...
		 * LengthN=...
		 */
		char *val;
		int rc;

		rc = get_key_val(contents, &pos, &line, &size, &val);
		if (rc == 0) {
			free(line);
			files[fcount] = NULL;
			return files;
		}
		if (rc == -1) {
			free(line);
			files[fcount] = NULL;
			free_str_array(files);
			return NULL;
		}

		/* just ignore useless crap */
		if (strncasecmp(line, "File", 4))
			continue;

		if (fcount == falloc - 1) {
			falloc *= 2;
			files = xrenew(char *, files, falloc);
		}
		files[fcount++] = xstrdup(val);
	}
}
