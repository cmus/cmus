/*
 * Copyright 2008-2013 Various Authors
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CMUS_TRACK_INFO_H
#define CMUS_TRACK_INFO_H

#include <time.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

struct track_info {
	uint64_t uid;
	struct keyval *comments;

	// next track_info in the hash table (cache.c)
	struct track_info *next;

	time_t mtime;
	int duration;
	long bitrate;
	char *codec;
	char *codec_profile;
	char *filename;

	int tracknumber;
	int discnumber;
	int date;
	int originaldate;
	double rg_track_gain;
	double rg_track_peak;
	double rg_album_gain;
	double rg_album_peak;
	const char *artist;
	const char *album;
	const char *title;
	const char *genre;
	const char *comment;
	const char *albumartist;
	const char *artistsort;
	const char *albumsort;
	const char *media;

	char *collkey_artist;
	char *collkey_album;
	char *collkey_title;
	char *collkey_genre;
	char *collkey_comment;
	char *collkey_albumartist;

	unsigned int play_count;

	int is_va_compilation : 1;
	int bpm;
};

typedef size_t sort_key_t;

#define SORT_INVALID            ((sort_key_t) (-1))
#define SORT_ARTIST        	offsetof(struct track_info, collkey_artist)
#define SORT_ALBUM         	offsetof(struct track_info, collkey_album)
#define SORT_TITLE         	offsetof(struct track_info, collkey_title)
#define SORT_TRACKNUMBER   	offsetof(struct track_info, tracknumber)
#define SORT_DISCNUMBER    	offsetof(struct track_info, discnumber)
#define SORT_DATE          	offsetof(struct track_info, date)
#define SORT_ORIGINALDATE  	offsetof(struct track_info, originaldate)
#define SORT_RG_TRACK_GAIN 	offsetof(struct track_info, rg_track_gain)
#define SORT_RG_TRACK_PEAK 	offsetof(struct track_info, rg_track_peak)
#define SORT_RG_ALBUM_GAIN 	offsetof(struct track_info, rg_album_gain)
#define SORT_RG_ALBUM_PEAK 	offsetof(struct track_info, rg_album_peak)
#define SORT_GENRE         	offsetof(struct track_info, collkey_genre)
#define SORT_COMMENT       	offsetof(struct track_info, collkey_comment)
#define SORT_ALBUMARTIST   	offsetof(struct track_info, collkey_albumartist)
#define SORT_PLAY_COUNT   	offsetof(struct track_info, play_count)
#define SORT_FILENAME      	offsetof(struct track_info, filename)
#define SORT_FILEMTIME     	offsetof(struct track_info, mtime)
#define SORT_BITRATE       	offsetof(struct track_info, bitrate)
#define SORT_CODEC         	offsetof(struct track_info, codec)
#define SORT_CODEC_PROFILE 	offsetof(struct track_info, codec_profile)
#define SORT_MEDIA		offsetof(struct track_info, media)
#define SORT_BPM		offsetof(struct track_info, bpm)
#define REV_SORT__START		sizeof(struct track_info)
#define REV_SORT_ARTIST		(REV_SORT__START + offsetof(struct track_info, collkey_artist))
#define REV_SORT_ALBUM          (REV_SORT__START + offsetof(struct track_info, collkey_album))
#define REV_SORT_TITLE          (REV_SORT__START + offsetof(struct track_info, collkey_title))
#define REV_SORT_PLAY_COUNT   	(REV_SORT__START + offsetof(struct track_info, play_count))
#define REV_SORT_TRACKNUMBER    (REV_SORT__START + offsetof(struct track_info, tracknumber))
#define REV_SORT_DISCNUMBER     (REV_SORT__START + offsetof(struct track_info, discnumber))
#define REV_SORT_DATE           (REV_SORT__START + offsetof(struct track_info, date))
#define REV_SORT_ORIGINALDATE   (REV_SORT__START + offsetof(struct track_info, originaldate))
#define REV_SORT_RG_TRACK_GAIN  (REV_SORT__START + offsetof(struct track_info, rg_track_gain))
#define REV_SORT_RG_TRACK_PEAK  (REV_SORT__START + offsetof(struct track_info, rg_track_peak))
#define REV_SORT_RG_ALBUM_GAIN  (REV_SORT__START + offsetof(struct track_info, rg_album_gain))
#define REV_SORT_RG_ALBUM_PEAK  (REV_SORT__START + offsetof(struct track_info, rg_album_peak))
#define REV_SORT_GENRE          (REV_SORT__START + offsetof(struct track_info, collkey_genre))
#define REV_SORT_COMMENT        (REV_SORT__START + offsetof(struct track_info, collkey_comment))
#define REV_SORT_ALBUMARTIST    (REV_SORT__START + offsetof(struct track_info, collkey_albumartist))
#define REV_SORT_FILENAME       (REV_SORT__START + offsetof(struct track_info, filename))
#define REV_SORT_FILEMTIME      (REV_SORT__START + offsetof(struct track_info, mtime))
#define REV_SORT_BITRATE        (REV_SORT__START + offsetof(struct track_info, bitrate))
#define REV_SORT_CODEC          (REV_SORT__START + offsetof(struct track_info, codec))
#define REV_SORT_CODEC_PROFILE  (REV_SORT__START + offsetof(struct track_info, codec_profile))
#define REV_SORT_MEDIA          (REV_SORT__START + offsetof(struct track_info, media))
#define REV_SORT_BPM            (REV_SORT__START + offsetof(struct track_info, bpm))

#define TI_MATCH_ARTIST       (1 << 0)
#define TI_MATCH_ALBUM        (1 << 1)
#define TI_MATCH_TITLE        (1 << 2)
#define TI_MATCH_ALBUMARTIST  (1 << 3)
#define TI_MATCH_ALL          (~0)

/* initializes only filename and ref */
struct track_info *track_info_new(const char *filename);
void track_info_set_comments(struct track_info *ti, struct keyval *comments);

void track_info_ref(struct track_info *ti);
void track_info_unref(struct track_info *ti);
bool track_info_unique_ref(struct track_info *ti);

/*
 * returns: 1 if @ti has any of the following tags: artist, album, title
 *          0 otherwise
 */
int track_info_has_tag(const struct track_info *ti);

/*
 * @flags  fields to search in (TI_MATCH_*)
 *
 * returns: 1 if all words in @text are found to match defined fields (@flags) in @ti
 *          0 otherwise
 */
int track_info_matches(const struct track_info *ti, const char *text, unsigned int flags);

/*
 * @flags            fields to search in (TI_MATCH_*)
 * @exclude_flags    fields which must not match (TI_MATCH_*)
 * @match_all_words  if true, all words must be found in @ti
 *
 * returns: 1 if all/any words in @text are found to match defined fields (@flags) in @ti
 *          0 otherwise
 */
int track_info_matches_full(const struct track_info *ti, const char *text, unsigned int flags,
		unsigned int exclude_flags, int match_all_words);

int track_info_cmp(const struct track_info *a, const struct track_info *b, const sort_key_t *keys);

sort_key_t *parse_sort_keys(const char *value);
const char *sort_key_to_str(sort_key_t key);
void sort_keys_to_str(const sort_key_t *keys, char *buf, size_t bufsize);

#endif
