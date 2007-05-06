/* 
 * Copyright 2004-2007 Timo Hirvonen
 */

#include "comment.h"
#include "xmalloc.h"
#include "utils.h"

#include <string.h>

struct keyval *comments_dup(const struct keyval *comments)
{
	struct keyval *c;
	int i;

	for (i = 0; comments[i].key; i++)
		; /* nothing */
	c = xnew(struct keyval, i + 1);
	for (i = 0; comments[i].key; i++) {
		c[i].key = xstrdup(comments[i].key);
		c[i].val = xstrdup(comments[i].val);
	}
	c[i].key = NULL;
	c[i].val = NULL;
	return c;
}

void comments_free(struct keyval *comments)
{
	int i;

	for (i = 0; comments[i].key; i++) {
		free(comments[i].key);
		free(comments[i].val);
	}
	free(comments);
}

const char *comments_get_val(const struct keyval *comments, const char *key)
{
	int i;

	for (i = 0; comments[i].key; i++) {
		if (strcasecmp(comments[i].key, key) == 0)
			return comments[i].val;
	}
	return NULL;
}

int comments_get_int(const struct keyval *comments, const char *key)
{
	const char *val;
	long int ival;

	val = comments_get_val(comments, key);
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

	val = comments_get_val(comments, key);
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
	int n = c->count + 1;

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

	if (n > c->alloc) {
		n = (n + 3) & ~3;
		c->comments = xrenew(struct keyval, c->comments, n);
		c->alloc = n;
	}

	c->comments[c->count].key = xstrdup(key);
	c->comments[c->count].val = val;
	c->count++;
	return 1;
}

int comments_add_const(struct growing_keyvals *c, const char *key, const char *val)
{
	return comments_add(c, key, xstrdup(val));
}

void comments_terminate(struct growing_keyvals *c)
{
	int alloc = c->count + 1;

	if (alloc > c->alloc) {
		c->comments = xrenew(struct keyval, c->comments, alloc);
		c->alloc = alloc;
	}
	c->comments[c->count].key = NULL;
	c->comments[c->count].val = NULL;
}
