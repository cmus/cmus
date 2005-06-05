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
#include <utils.h>
#include <xmalloc.h>

#include <unistd.h>

/* get next non-empty line
 */
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

int pls_for_each(const char *contents, void (*cb)(void *data, const char *file, const char *title, int duration), void *cb_data)
{
	int pos = 0;
	char *line = NULL;
	size_t size = 0;

	if (!get_line(contents, &pos, &line, &size) || strncasecmp(line, "[playlist]", 10)) {
		free(line);
		return -1;
	}
	while (1) {
		/*
		 * FileN=...
		 * TitleN=...
		 * LengthN=...
		 */
		char *val;
		char *file;
		char *title;
		long int len;
		int rc;

		rc = get_key_val(contents, &pos, &line, &size, &val);
		if (rc == 0 || rc == -1) {
			free(line);
			return rc;
		}

		/* just ignore useless crap */
		if (strncasecmp(line, "File", 4))
			continue;
		file = xstrdup(val);

		if (get_key_val(contents, &pos, &line, &size, &val) != 1 || strncasecmp(line, "Title", 5)) {
			free(file);
			free(line);
			return -1;
		}
		title = xstrdup(val);

		if (get_key_val(contents, &pos, &line, &size, &val) != 1 || strncasecmp(line, "Length", 6) || str_to_int(val, &len)) {
			free(title);
			free(file);
			free(line);
			return -1;
		}

		cb(cb_data, file, title, len);
		free(title);
		free(file);
	}
}

struct cb_data {
	char **files;
	int size;
	int count;
};

static void cb(void *data, const char *file, const char *title, int duration)
{
	struct cb_data *d = data;

	if (d->count == d->size - 1) {
		d->size *= 2;
		d->files = xrenew(char *, d->files, d->size);
	}
	d->files[d->count++] = xstrdup(file);
}

char **pls_get_files(const char *contents)
{
	struct cb_data data;
	int rc;

	data.count = 0;
	data.size = 8;
	data.files = xnew0(char *, data.size);
	rc = pls_for_each(contents, cb, &data);
	if (rc) {
		free(data.files);
		return NULL;
	}
	data.files[data.count] = NULL;
	return data.files;
}
