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

#ifndef _TRACK_INFO_H
#define _TRACK_INFO_H

#include <time.h>

struct track_info {
	struct keyval *comments;

	// next track_info in the hash table (cache.c)
	struct track_info *next;

	time_t mtime;
	int duration;
	int ref;
	char *filename;
};

#define TI_MATCH_ARTIST       (1 << 0)
#define TI_MATCH_ALBUM        (1 << 1)
#define TI_MATCH_TITLE        (1 << 2)
#define TI_MATCH_ALBUMARTIST  (1 << 3)

/* initializes only filename and ref */
struct track_info *track_info_new(const char *filename);

void track_info_ref(struct track_info *ti);
void track_info_unref(struct track_info *ti);

/*
 * returns: 1 if @ti has any of the following tags: artist, album, title
 *          0 otherwise
 */
int track_info_has_tag(const struct track_info *ti);

/*
 * @flags: TI_MATCH_*
 *
 * returns: 1 if all words in @text are found to match defined fields (@flags) in @ti
 *          0 otherwise
 */
int track_info_matches(struct track_info *ti, const char *text, unsigned int flags);

int track_info_cmp(const struct track_info *a, const struct track_info *b, const char * const *keys);

#endif
