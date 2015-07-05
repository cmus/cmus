/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2004-2007 Timo Hirvonen
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

#include "comment.h"
#include "xmalloc.h"
#include "utils.h"
#include "uchar.h"

#include <string.h>
#include <strings.h>

static int is_various_artists(const char *a)
{
	return strcasecmp(a, "Various Artists") == 0 ||
	       strcasecmp(a, "Various")         == 0 ||
	       strcasecmp(a, "VA")              == 0 ||
	       strcasecmp(a, "V/A")             == 0;
}

int track_is_compilation(const struct keyval *comments)
{
	const char *c, *a, *aa;

	c = keyvals_get_val(comments, "compilation");
	if (c && is_freeform_true(c))
		return 1;

	aa = keyvals_get_val(comments, "albumartist");
	if (aa && is_various_artists(aa))
		return 1;

	a = keyvals_get_val(comments, "artist");
	if (aa && a && !u_strcase_equal(aa, a))
		return 1;

	return 0;
}

int track_is_va_compilation(const struct keyval *comments)
{
	const char *c, *aa;

	aa = keyvals_get_val(comments, "albumartist");
	if (aa)
		return is_various_artists(aa);

	c = keyvals_get_val(comments, "compilation");

	return c && is_freeform_true(c);
}

const char *comments_get_albumartist(const struct keyval *comments)
{
	const char *val = keyvals_get_val(comments, "albumartist");

	if (!val || strcmp(val, "") == 0)
		val = keyvals_get_val(comments, "artist");

	return val;
}

const char *comments_get_artistsort(const struct keyval *comments)
{
	const char *val;

	if (track_is_va_compilation(comments))
		return NULL;

	val = keyvals_get_val(comments, "albumartistsort");
	if (!track_is_compilation(comments)) {
		if (!val || strcmp(val, "") == 0)
			val = keyvals_get_val(comments, "artistsort");
	}

	if (!val || strcmp(val, "") == 0)
		return NULL;

	return val;
}

int comments_get_int(const struct keyval *comments, const char *key)
{
	const char *val;
	long int ival;

	val = keyvals_get_val(comments, key);
	if (val == NULL)
		return -1;
	while (*val && !(*val >= '0' && *val <= '9'))
		val++;
	if (str_to_int(val, &ival) == -1)
		return -1;
	return ival;
}

double comments_get_double(const struct keyval *comments, const char *key)
{
	const char *val;
	char *end;
	double d;

	val = keyvals_get_val(comments, key);
	if (!val || strcmp(val, "") == 0)
		goto error;

	d = strtod(val, &end);
	if (val == end)
		goto error;

	return d;

error:
	return strtod("NAN", NULL);
}

/* Return date as an integer in the form YYYYMMDD, for sorting purposes.
 * This function is not year 10000 compliant. */
int comments_get_date(const struct keyval *comments, const char *key)
{
	const char *val;
	char *endptr;
	int year, month, day;
	long int ival;

	val = keyvals_get_val(comments, key);
	if (val == NULL)
		return -1;

	year = strtol(val, &endptr, 10);
	/* Looking for a four-digit number */
	if (year < 1000 || year > 9999)
		return -1;
	ival = year * 10000;

	if (*endptr == '-' || *endptr == ' ' || *endptr == '/') {
		month = strtol(endptr+1, &endptr, 10);
		if (month < 1 || month > 12)
			return ival;
		ival += month * 100;
	}

	if (*endptr == '-' || *endptr == ' ' || *endptr == '/') {
		day = strtol(endptr+1, &endptr, 10);
		if (day < 1 || day > 31)
			return ival;
		ival += day;
	}


	return ival;
}

static const char *interesting[] = {
	"artist", "album", "title", "tracknumber", "discnumber", "genre",
	"date", "compilation", "albumartist", "artistsort", "albumartistsort",
	"albumsort",
	"originaldate",
	"replaygain_track_gain",
	"replaygain_track_peak",
	"replaygain_album_gain",
	"replaygain_album_peak",
	"musicbrainz_trackid",
	"comment",
	"arranger", "composer", "conductor", "lyricist", "performer",
	"remixer", "label", "publisher", "work", "opus", "partnumber", "part",
	"subtitle", "media",
	NULL
};

static struct {
	const char *old;
	const char *new;
} key_map[] = {
	{ "album_artist", "albumartist" },
	{ "album artist", "albumartist" },
	{ "disc", "discnumber" },
	{ "track", "tracknumber" },
	{ "WM/Year", "date" },
	{ "WM/ArtistSortOrder", "artistsort" },
	{ "WM/AlbumArtistSortOrder", "albumartistsort" },
	{ "WM/AlbumSortOrder", "albumsort" },
	{ "WM/OriginalReleaseYear", "originaldate" },
	{ "WM/Media", "media" },
	{ "sourcemedia", "media" },
	{ "MusicBrainz Track Id", "musicbrainz_trackid" },
	{ "version", "subtitle" },
	{ NULL, NULL }
};

static const char *fix_key(const char *key)
{
	int i;

	for (i = 0; interesting[i]; i++) {
		if (!strcasecmp(key, interesting[i]))
			return interesting[i];
	}
	for (i = 0; key_map[i].old; i++) {
		if (!strcasecmp(key, key_map[i].old))
			return key_map[i].new;
	}
	return NULL;
}

int comments_add(struct growing_keyvals *c, const char *key, char *val)
{
	if (!strcasecmp(key, "songwriter")) {
		int r = comments_add_const(c, "lyricist", val);
		return comments_add(c, "composer", val) && r;
	}

	key = fix_key(key);
	if (!key) {
		free(val);
		return 0;
	}

	if (!strcmp(key, "tracknumber") || !strcmp(key, "discnumber")) {
		char *slash = strchr(val, '/');
		if (slash)
			*slash = 0;
	}

	/* don't add duplicates */
	if (keyvals_get_val_growing(c, key)) {
		free(val);
		return 0;
	}

	keyvals_add(c, key, val);
	return 1;
}

int comments_add_const(struct growing_keyvals *c, const char *key, const char *val)
{
	return comments_add(c, key, xstrdup(val));
}
