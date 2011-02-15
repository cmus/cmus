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
#include "u_collate.h"
#include "misc.h"
#include "xmalloc.h"
#include "utils.h"
#include "debug.h"

#include <string.h>

static void track_info_free(struct track_info *ti)
{
	keyvals_free(ti->comments);
	free(ti->filename);
	free(ti->collkey_artist);
	free(ti->collkey_album);
	free(ti->collkey_title);
	free(ti->collkey_genre);
	free(ti->collkey_comment);
	free(ti->collkey_albumartist);
	free(ti);
}

struct track_info *track_info_new(const char *filename)
{
	struct track_info *ti;
	ti = xnew(struct track_info, 1);
	ti->filename = xstrdup(filename);
	ti->ref = 1;
	ti->comments = NULL;
	return ti;
}

void track_info_set_comments(struct track_info *ti, struct keyval *comments) {
	ti->comments = comments;
	ti->artist = keyvals_get_val(comments, "artist");
	ti->album = keyvals_get_val(comments, "album");
	ti->title = keyvals_get_val(comments, "title");
	ti->tracknumber = comments_get_int(comments, "tracknumber");
	ti->discnumber = comments_get_int(comments, "discnumber");
	ti->date = comments_get_date(comments, "date");
	ti->genre = keyvals_get_val(comments, "genre");
	ti->comment = keyvals_get_val(comments, "comment");
	ti->albumartist = comments_get_albumartist(comments);
	ti->artistsort = comments_get_artistsort(comments);
	ti->is_va_compilation = track_is_va_compilation(comments);

	ti->collkey_artist = u_strcasecoll_key0(ti->artist);
	ti->collkey_album = u_strcasecoll_key0(ti->album);
	ti->collkey_title = u_strcasecoll_key0(ti->title);
	ti->collkey_genre = u_strcasecoll_key0(ti->genre);
	ti->collkey_comment = u_strcasecoll_key0(ti->comment);
	ti->collkey_albumartist = u_strcasecoll_key0(ti->albumartist);
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
	return ti->artist || ti->album || ti->title;
}

int track_info_matches(struct track_info *ti, const char *text, unsigned int flags)
{
	const char *artist = ti->artist;
	const char *album = ti->album;
	const char *title = ti->title;
	const char *albumartist = ti->albumartist;
	char **words;
	int i, matched = 1;

	words = get_words(text);
	if (words[0] == NULL)
		matched = 0;
	for (i = 0; words[i]; i++) {
		const char *word = words[i];

		if ((flags & TI_MATCH_ARTIST && artist) ||
		    (flags & TI_MATCH_ALBUM && album) ||
		    (flags & TI_MATCH_TITLE && title) ||
		    (flags & TI_MATCH_ALBUMARTIST && albumartist)) {
			if (flags & TI_MATCH_ARTIST && artist && u_strcasestr_base(artist, word))
				continue;
			if (flags & TI_MATCH_ALBUM && album && u_strcasestr_base(album, word))
				continue;
			if (flags & TI_MATCH_TITLE && title && u_strcasestr_base(title, word))
				continue;
			if (flags & TI_MATCH_ALBUMARTIST && albumartist && u_strcasestr_base(albumartist, word))
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

/* this function gets called *alot*, it must be very fast */
int track_info_cmp(const struct track_info *a, const struct track_info *b, const sort_key_t *keys)
{
	int i, res = 0;

	for (i = 0; keys[i] != SORT_INVALID; i++) {
		sort_key_t key = keys[i];
		const char *av, *bv;

		switch (key) {
		case SORT_TRACKNUMBER:
		case SORT_DISCNUMBER:
		case SORT_DATE:
			res = getentry(a, key, int) - getentry(b, key, int);
			break;
		case SORT_FILEMTIME:
			res = a->mtime - b->mtime;
			break;
		case SORT_FILENAME:
			/* NOTE: filenames are not necessarily UTF-8 */
			res = strcoll(a->filename, b->filename);
			break;
		default:
			av = getentry(a, key, const char *);
			bv = getentry(b, key, const char *);
			res = strcmp0(av, bv);
			break;
		}

		if (res)
			break;
	}
	return res;
}
