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

int is_cue(const char *filename)
{
	const char *ext = get_extension(filename);
	return ext != NULL && strcmp(ext, "cue") == 0;
}


int cue_get_track_nums(const char *filename, int **out_nums)
{
	struct cue_sheet *cd = cue_from_file(filename);
	if (!cd)
		return -1;

	int n = cd->num_tracks;
	*out_nums = xnew(int, n);

	int i;
	for (i = 0; i < n; i++)
		(*out_nums)[i] = cd->tracks[i].number;

	cue_free(cd);
	return n;
}


int cue_get_files(const char *filename, char ***out_files)
{
	struct cue_sheet *cd = cue_from_file(filename);
	if (!cd)
		return -1;

	int n = list_len(&cd->files);
	*out_files = xnew(char *, n);

	int i = 0;
	struct cue_track_file *tf;
	list_for_each_entry(tf, &cd->files, node) {
		(*out_files)[i] = tf->file;
		tf->file = NULL;
		i++;
	}

	cue_free(cd);
	return n;
}


char *construct_cue_url(const char *cue_filename, int track_n)
{
	char buf[4096] = {0};

	snprintf(buf, sizeof(buf), "cue://%s/%d", cue_filename, track_n);

	return xstrdup(buf);
}
