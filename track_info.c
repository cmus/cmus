/* 
 * Copyright 2004-2005 Timo Hirvonen
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

#include "track_info.h"
#include "comment.h"
#include "uchar.h"
#include "misc.h"
#include "xmalloc.h"
#include "utils.h"
#include "debug.h"

#include <string.h>

static void track_info_free(struct track_info *ti)
{
	keyvals_free(ti->comments);
	free(ti);
}

struct track_info *track_info_new(const char *filename)
{
	struct track_info *ti;
	int size = strlen(filename) + 1;

	ti = xmalloc(sizeof(struct track_info) + size);
	memcpy(ti->filename, filename, size);
	ti->ref = 1;
	return ti;
}

struct track_info *track_info_url_new(const char *url)
{
	struct track_info *ti = track_info_new(url);
	ti->comments = xnew0(struct keyval, 1);
	ti->duration = -1;
	ti->mtime = -1;
	return ti;
}

void track_info_ref(struct track_info *ti)
{
	BUG_ON(ti->ref < 1);
	ti->ref++;
}

void track_info_unref(struct track_info *ti)
{
	BUG_ON(ti->ref < 1);
	ti->ref--;
	if (ti->ref == 0)
		track_info_free(ti);
}

int track_info_has_tag(const struct track_info *ti)
{
	return keyvals_get_val(ti->comments, "artist") ||
		keyvals_get_val(ti->comments, "album") ||
		keyvals_get_val(ti->comments, "title");
}

int track_info_matches(struct track_info *ti, const char *text, unsigned int flags)
{
	const char *artist = keyvals_get_val(ti->comments, "artist");
	const char *album = keyvals_get_val(ti->comments, "album");
	const char *title = keyvals_get_val(ti->comments, "title");
	char **words;
	int i, matched = 1;

	words = get_words(text);
	if (words[0] == NULL)
		matched = 0;
	for (i = 0; words[i]; i++) {
		const char *word = words[i];

		if ((flags & TI_MATCH_ARTIST && artist) ||
		    (flags & TI_MATCH_ALBUM && album) ||
		    (flags & TI_MATCH_TITLE && title)) {
			if (flags & TI_MATCH_ARTIST && artist && u_strcasestr(artist, word))
				continue;
			if (flags & TI_MATCH_ALBUM && album && u_strcasestr(album, word))
				continue;
			if (flags & TI_MATCH_TITLE && title && u_strcasestr(title, word))
				continue;
		} else {
			/* compare with url or filename without path */
			char *filename = ti->filename;

			if (!is_url(filename)) {
				char *slash = strrchr(ti->filename, '/');
				if (slash)
					filename = slash + 1;
			}
			if (u_strcasestr_filename(filename, word))
				continue;
		}
		matched = 0;
		break;
	}
	free_str_array(words);
	return matched;
}

static int xstrcasecmp(const char *a, const char *b)
{
	if (a == NULL) {
		if (b == NULL)
			return 0;
		return -1;
	} else if (b == NULL) {
		return 1;
	}
	return u_strcasecmp(a, b);
}

int track_info_cmp(const struct track_info *a, const struct track_info *b, const char * const *keys)
{
	int i, res = 0;

	for (i = 0; keys[i]; i++) {
		const char *key = keys[i];
		const char *av, *bv;

		/* numeric compare for tracknumber and discnumber */
		if (strcmp(key, "tracknumber") == 0) {
			res = comments_get_int(a->comments, key) -
				comments_get_int(b->comments, key);
			if (res)
				break;
			continue;
		}
		if (strcmp(key, "discnumber") == 0) {
			res = comments_get_int(a->comments, key) -
				comments_get_int(b->comments, key);
			if (res)
				break;
			continue;
		}
		if (strcmp(key, "filename") == 0) {
			/* NOTE: filenames are not necessarily UTF-8 */
			res = strcasecmp(a->filename, b->filename);
			if (res)
				break;
			continue;
		}
		if (strcmp(key, "albumartist") == 0) {
			av = comments_get_albumartist(a->comments);
			bv = comments_get_albumartist(b->comments);
			res = xstrcasecmp(av, bv);
			if (res)
				break;
			continue;
		}
		if (strcmp(key, "filemtime") == 0) {
			res = a->mtime - b->mtime;
			if (res)
				break;
			continue;
		}

		av = keyvals_get_val(a->comments, key);
		bv = keyvals_get_val(b->comments, key);
		res = xstrcasecmp(av, bv);
		if (res)
			break;
	}
	return res;
}
