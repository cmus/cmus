/*
 * Copyright (C) 2008-2013 Various Authors
 * Copyright (C) 2011 Gregory Petrosyan
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "path.h"
#include "utils.h"
#include "cue_utils.h"
#include "xmalloc.h"

#include <stdio.h>

int is_cue_url(const char *name)
{
	return strncmp(name, "cue://", 6) == 0;
}

int is_cue_filename(const char *name)
{
	const char *ext = get_extension(name);
	if (ext != NULL && strcmp(ext, "cue") == 0)
		return 1;
	else return 0;
}

char *associated_cue(const char *filename)
{
	FILE *fp;
	char buf[4096] = {0};
	const char *dot;

	if (is_cue_filename(filename)) return NULL;

	dot = strrchr(filename, '.');
	if (dot == NULL)
		return NULL;

	snprintf(buf, sizeof buf, "%.*s.cue", (int) (dot - filename), filename);
	fp = fopen(buf, "r");
	if (!fp)
		snprintf(buf, sizeof buf, "%s.cue", filename);
	else
		fclose(fp);

	return xstrdup(buf);
}


int cue_get_ntracks(const char *filename)
{
	int n;
	FILE *cue;
	Cd *cd;

	cue = fopen(filename, "r");
	if (cue == NULL)
		return -1;

	disable_stdio();
	cd = cue_parse_file(cue);
	enable_stdio();
	if (cd == NULL) {
		fclose(cue);
		return -1;
	}

	n = cd_get_ntrack(cd);

	cd_delete(cd);
	fclose(cue);

	return n;
}


char *construct_cue_url(const char *cue_filename, int track_n)
{
	char buf[4096] = {0};

	snprintf(buf, sizeof buf, "cue://%s/%d", cue_filename, track_n);

	return xstrdup(buf);
}

int add_file_cue(const char *filename, void (*add_file)(const char*, int))
{
	int n_tracks;
	char *url;
	char *cue_filename;

	cue_filename = associated_cue(filename);
	if (cue_filename == NULL)
		return 0;

	n_tracks = cue_get_ntracks(cue_filename);
	if (n_tracks <= 0) {
		free(cue_filename);
		return 0;
	}

	for (int i = 1; i <= n_tracks; ++i) {
		url = construct_cue_url(cue_filename, i);
		add_file(url, 0);
		free(url);
	}

	free(cue_filename);
	return 1;
}
