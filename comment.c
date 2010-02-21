/* 
 * Copyright 2004-2007 Timo Hirvonen
 */

#include "comment.h"
#include "xmalloc.h"
#include "utils.h"
#include "uchar.h"

#include <string.h>

static int is_freeform_true(const char *c)
{
	return	!strcasecmp(c, "1")	||
		!strcasecmp(c, "y")	||
		!strcasecmp(c, "yes")	||
		!strcasecmp(c, "true");
}

int track_is_compilation(const struct keyval *comments)
{
	const char *c	= keyvals_get_val(comments, "compilation");
	const char *a	= keyvals_get_val(comments, "artist");
	const char *aa	= keyvals_get_val(comments, "albumartist");

	if (c && is_freeform_true(c))
		return 1;

	if (aa && !strcasecmp(aa, "Various Artists"))
		return 1;

	if (aa && a && u_strcasecmp(aa, a))
		return 1;

	return 0;
}

const char *comments_get_album(const struct keyval *comments)
{
	const char *val = keyvals_get_val(comments, "album");

	if (!val || strcmp(val, "") == 0)
		val = "<No Name>";

	return val;
}

const char *comments_get_albumartist(const struct keyval *comments)
{
	const char *val = keyvals_get_val(comments, "albumartist");

	if ((!val || !strcasecmp(val, "Various Artists")) && track_is_compilation(comments))
		val = "<Various Artists>";
	if (!val)
		val = keyvals_get_val(comments, "artist");
	if (!val || strcmp(val, "") == 0)
		val = "<No Name>";

	return val;
}

const char *comments_get_artistsort(const struct keyval *comments)
{
	const char *val = keyvals_get_val(comments, "albumartistsort");

	if (!val)
		val = keyvals_get_val(comments, "artistsort");

	return val;
}

int comments_get_int(const struct keyval *comments, const char *key)
{
	const char *val;
	long int ival;

	val = keyvals_get_val(comments, key);
	if (val == NULL)
		return -1;
	if (str_to_int(val, &ival) == -1)
		return -1;
	return ival;
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
	"replaygain_track_gain",
	"replaygain_track_peak",
	"replaygain_album_gain",
	"replaygain_album_peak",
	"comment",
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
	int i;

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

	/* don't add duplicates. can't use keyvals_get_val() */
	for (i = 0; i < c->count; i++) {
		if (!strcasecmp(key, c->keyvals[i].key) && !strcmp(val, c->keyvals[i].val)) {
			free(val);
			return 0;
		}
	}

	keyvals_add(c, key, val);
	return 1;
}

int comments_add_const(struct growing_keyvals *c, const char *key, const char *val)
{
	return comments_add(c, key, xstrdup(val));
}
