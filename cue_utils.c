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
#include "cue.h"

#include <stdio.h>

char *associated_cue(const char *filename)
{
	FILE *fp;
	const char *ext;
	char buf[4096] = {0};
	const char *dot;

	ext = get_extension(filename);
	if (ext != NULL && strcmp(ext, "cue") == 0)
		return NULL;

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
	struct cue_sheet *cd = cue_from_file(filename);
	if (!cd)
		return -1;
	size_t n = cd->num_tracks;
	cue_free(cd);
	return n;
}


char *construct_cue_url(const char *cue_filename, int track_n)
{
	char buf[4096] = {0};

	snprintf(buf, sizeof buf, "cue://%s/%d", cue_filename, track_n);

	return xstrdup(buf);
}
