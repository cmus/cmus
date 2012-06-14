/*
 * Copyright (C) 2008-2011 Various Authors
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
#include "cue_utils.h"
#include "xmalloc.h"


#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>


Cd *cue_parse_file__no_stderr_garbage(FILE *f)
{
	int stderr_fd;
	int devnull_fd;
	Cd *ret;

	stderr_fd = dup(2);
	devnull_fd = open("/dev/null", O_WRONLY);

	if (devnull_fd != -1)
		dup2(devnull_fd, 2);

	ret = cue_parse_file(f);

	if (stderr_fd != -1) {
		dup2(stderr_fd, 2);
		close(stderr_fd);
	}

	if (devnull_fd != -1)
		close(devnull_fd);

	return ret;
}


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
	int n;
	FILE *cue;
	Cd *cd;

	cue = fopen(filename, "r");
	if (cue == NULL)
		return -1;

	cd = cue_parse_file__no_stderr_garbage(cue);
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
