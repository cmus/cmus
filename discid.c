/*
 * Copyright 2011-2013 Various Authors
 * Copyright 2011 Johannes Weißl
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

#include "discid.h"
#include "xmalloc.h"
#include "path.h"
#include "utils.h"
#include "debug.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

#ifdef HAVE_DISCID
#include <discid/discid.h>
#endif

char *get_default_cdda_device(void)
{
	const char *dev = NULL;
#ifdef HAVE_DISCID
	dev = discid_get_default_device();
#endif
	if (!dev)
		dev = "/dev/cdrom";
	return xstrdup(dev);
}

int parse_cdda_url(const char *url, char **disc_id, int *start_track, int *end_track)
{
	char *slash, *dash;
	long int t;

	if (!is_cdda_url(url))
		return 0;
	url += 7;

	slash = strrchr(url, '/');
	if (slash) {
		*disc_id = xstrndup(url, slash - url);
		url = slash + 1;
	}
	dash = strchr(url, '-');
	if (dash) {
		char *tmp = xstrndup(url, dash - url);
		if (str_to_int(tmp, &t) == 0)
			*start_track = t;
		if (end_track) {
			if (str_to_int(dash + 1, &t) == 0)
				*end_track = t;
			else
				*end_track = INT_MAX;
		}
		free(tmp);
	} else {
		if (str_to_int(url, &t) == 0)
			*start_track = t;
	}

	return 1;
}

char *gen_cdda_url(const char *disc_id, int start_track, int end_track)
{
	char buf[256];
	if (end_track != -1)
		snprintf(buf, sizeof(buf), "cdda://%s/%d-%d", disc_id, start_track, end_track);
	else
		snprintf(buf, sizeof(buf), "cdda://%s/%d", disc_id, start_track);
	return xstrdup(buf);
}

char *complete_cdda_url(const char *device, const char *url)
{
	char *new_url, *url_disc_id = NULL, *disc_id = NULL;
	int is_range, start_track = -1, end_track = -1, num_tracks = -1;

	parse_cdda_url(url, &url_disc_id, &start_track, &end_track);
	is_range = (start_track == -1 && end_track == -1) || end_track == INT_MAX;
	if (!url_disc_id || is_range) {
		if (url_disc_id && strchr(url_disc_id, '/'))
			device = url_disc_id;
		get_disc_id(device, &disc_id, &num_tracks);
		if (is_range)
			end_track = num_tracks;
		if (!url_disc_id)
			url_disc_id = disc_id;
	}
	if (start_track == -1)
		start_track = 1;

	new_url = gen_cdda_url(url_disc_id, start_track, end_track);
	free(disc_id);

	return new_url;
}

static int get_device_disc_id(const char *device, char **disc_id, int *num_tracks)
{
#ifdef HAVE_DISCID
	DiscId *disc = discid_new();
	if (!disc)
		return 0;

	if (!discid_read(disc, device)) {
		d_print("%s\n", discid_get_error_msg(disc));
		discid_free(disc);
		return 0;
	}

	*disc_id = xstrdup(discid_get_id(disc));
	if (num_tracks)
		*num_tracks = discid_get_last_track_num(disc);

	discid_free(disc);
	return 1;
#else
	return 0;
#endif
}

int get_disc_id(const char *device, char **disc_id, int *num_tracks)
{
	struct stat st;

	if (stat(device, &st) == -1)
		return 0;

	if (S_ISBLK(st.st_mode))
		return get_device_disc_id(device, disc_id, num_tracks);

	*disc_id = path_absolute(device);
	return 1;
}
