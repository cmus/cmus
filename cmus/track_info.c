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

#include <track_info.h>
#include <comment.h>
#include <uchar.h>
#include <misc.h>
#include <xmalloc.h>
#include <debug.h>

#include <string.h>

static void track_info_free(struct track_info *ti)
{
	comments_free(ti->comments);
	free(ti->filename);
	free(ti);
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
	return comments_get_val(ti->comments, "artist") ||
		comments_get_val(ti->comments, "album") ||
		comments_get_val(ti->comments, "title");
}

int track_info_matches(struct track_info *ti, const char *text, unsigned int flags)
{
	const char *artist = comments_get_val(ti->comments, "artist");
	const char *album = comments_get_val(ti->comments, "album");
	const char *title = comments_get_val(ti->comments, "title");
	char **words;
	int i, matched = 1;

	words = get_words(text);
	if (words[0] == NULL)
		matched = 0;
	for (i = 0; words[i]; i++) {
		const char *word = words[i];

		if ((flags & TI_MATCH_ARTIST && artist) || (flags & TI_MATCH_ALBUM && album) || (flags & TI_MATCH_TITLE && title)) {
			if (flags & TI_MATCH_ARTIST && artist && u_strcasestr(artist, word))
				continue;
			if (flags & TI_MATCH_ALBUM && album && u_strcasestr(album, word))
				continue;
			if (flags & TI_MATCH_TITLE && title && u_strcasestr(title, word))
				continue;
		} else {
			/* compare with filename (without path) */
			char *filename = strrchr(ti->filename, '/');

			if (filename == NULL) {
				filename = ti->filename;
			} else {
				filename++;
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
