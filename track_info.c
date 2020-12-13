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

#include "track_info.h"
#include "comment.h"
#include "uchar.h"
#include "u_collate.h"
#include "misc.h"
#include "xmalloc.h"
#include "utils.h"
#include "debug.h"
#include "path.h"
#include "ui_curses.h"

#include <string.h>
#include <stdatomic.h>
#include <math.h>

struct track_info_priv {
	struct track_info ti;
	_Atomic uint32_t ref_count;
};

static struct track_info_priv *track_info_to_priv(struct track_info *ti)
{
	return container_of(ti, struct track_info_priv, ti);
}

struct track_info *track_info_new(const char *filename)
{
	static _Atomic uint64_t cur_uid = ATOMIC_VAR_INIT(1);
	uint64_t uid = atomic_fetch_add_explicit(&cur_uid, 1, memory_order_relaxed);
	BUG_ON(uid == 0);

	struct track_info_priv *priv = xnew(struct track_info_priv, 1);
	atomic_init(&priv->ref_count, 1);

	struct track_info *ti = &priv->ti;
	ti->uid = uid;
	ti->filename = xstrdup(filename);
	ti->play_count = 0;
	ti->comments = NULL;
	ti->bpm = -1;
	ti->codec = NULL;
	ti->codec_profile = NULL;

	return ti;
}

void track_info_set_comments(struct track_info *ti, struct keyval *comments) {
	long int r128_track_gain;
	long int r128_album_gain;

	ti->comments = comments;
	ti->artist = keyvals_get_val(comments, "artist");
	ti->album = keyvals_get_val(comments, "album");
	ti->title = keyvals_get_val(comments, "title");
	ti->tracknumber = comments_get_int(comments, "tracknumber");
	ti->discnumber = comments_get_int(comments, "discnumber");
	ti->date = comments_get_date(comments, "date");
	ti->originaldate = comments_get_date(comments, "originaldate");
	ti->genre = keyvals_get_val(comments, "genre");
	ti->comment = keyvals_get_val(comments, "comment");
	ti->albumartist = comments_get_albumartist(comments);
	ti->artistsort = comments_get_artistsort(comments);
	ti->albumsort = keyvals_get_val(comments, "albumsort");
	ti->is_va_compilation = track_is_va_compilation(comments);
	ti->media = keyvals_get_val(comments, "media");

	int bpm = comments_get_int(comments, "bpm");
	if (ti->bpm == 0 || ti->bpm == -1) {
		ti->bpm = bpm;
	}

	if (ti->artist == NULL && ti->albumartist != NULL) {
		/* best guess */
		ti->artist = ti->albumartist;
	}

	if (track_info_has_tag(ti) && ti->title == NULL) {
		/* best guess */
		ti->title = path_basename(ti->filename);
	}

	ti->rg_track_gain = comments_get_double(comments, "replaygain_track_gain");
	ti->rg_track_peak = comments_get_double(comments, "replaygain_track_peak");
	ti->rg_album_gain = comments_get_double(comments, "replaygain_album_gain");
	ti->rg_album_peak = comments_get_double(comments, "replaygain_album_peak");

	if (comments_get_signed_int(comments, "r128_track_gain", &r128_track_gain) != -1) {
		ti->rg_track_gain = (r128_track_gain / 256.0) + 5;
	}

	if (comments_get_signed_int(comments, "r128_album_gain", &r128_album_gain) != -1) {
		ti->rg_album_gain = (r128_album_gain / 256.0) + 5;
	}

	ti->collkey_artist = u_strcasecoll_key0(ti->artist);
	ti->collkey_album = u_strcasecoll_key0(ti->album);
	ti->collkey_title = u_strcasecoll_key0(ti->title);
	ti->collkey_genre = u_strcasecoll_key0(ti->genre);
	ti->collkey_comment = u_strcasecoll_key0(ti->comment);
	ti->collkey_albumartist = u_strcasecoll_key0(ti->albumartist);
}

void track_info_ref(struct track_info *ti)
{
	struct track_info_priv *priv = track_info_to_priv(ti);
	atomic_fetch_add_explicit(&priv->ref_count, 1, memory_order_relaxed);
}

void track_info_unref(struct track_info *ti)
{
	struct track_info_priv *priv = track_info_to_priv(ti);
	uint32_t prev = atomic_fetch_sub_explicit(&priv->ref_count, 1,
			memory_order_acq_rel);
	if (prev == 1) {
		keyvals_free(ti->comments);
		free(ti->filename);
		free(ti->codec);
		free(ti->codec_profile);
		free(ti->collkey_artist);
		free(ti->collkey_album);
		free(ti->collkey_title);
		free(ti->collkey_genre);
		free(ti->collkey_comment);
		free(ti->collkey_albumartist);
		free(priv);
	}
}

bool track_info_unique_ref(struct track_info *ti)
{
	struct track_info_priv *priv = track_info_to_priv(ti);
	return atomic_load_explicit(&priv->ref_count, memory_order_relaxed) == 1;
}

int track_info_has_tag(const struct track_info *ti)
{
	return ti->artist || ti->album || ti->title;
}

static inline int match_word(const struct track_info *ti, const char *word, unsigned int flags)
{
	return ((flags & TI_MATCH_ARTIST) && ti->artist && u_strcasestr_base(ti->artist, word)) ||
	       ((flags & TI_MATCH_ALBUM) && ti->album && u_strcasestr_base(ti->album, word)) ||
	       ((flags & TI_MATCH_TITLE) && ti->title && u_strcasestr_base(ti->title, word)) ||
	       ((flags & TI_MATCH_ALBUMARTIST) && ti->albumartist && u_strcasestr_base(ti->albumartist, word));
}

static inline int flags_set(const struct track_info *ti, unsigned int flags)
{
	return ((flags & TI_MATCH_ARTIST) && ti->artist) ||
	       ((flags & TI_MATCH_ALBUM) && ti->album) ||
	       ((flags & TI_MATCH_TITLE) && ti->title) ||
	       ((flags & TI_MATCH_ALBUMARTIST) && ti->albumartist);
}

int track_info_matches_full(const struct track_info *ti, const char *text,
		unsigned int flags, unsigned int exclude_flags, int match_all_words)
{
	char **words;
	int i, matched = 0;

	words = get_words(text);
	for (i = 0; words[i]; i++) {
		const char *word = words[i];

		matched = 0;
		if (flags_set(ti, flags) && match_word(ti, word, flags)) {
			matched = 1;
		} else {
			/* compare with url or filename without path */
			const char *filename = ti->filename;

			if (!is_url(filename))
				filename = path_basename(filename);

			if (u_strcasestr_filename(filename, word))
				matched = 1;
		}

		if (flags_set(ti, exclude_flags) && match_word(ti, word, exclude_flags))
			matched = 0;

		if (match_all_words ? !matched : matched)
			break;

	}
	free_str_array(words);
	return matched;
}

int track_info_matches(const struct track_info *ti, const char *text, unsigned int flags)
{
	return track_info_matches_full(ti, text, flags, 0, 1);
}

static int doublecmp0(double a, double b)
{
	double x;
	/* fast check for NaN */
	int r = (b != b) - (a != a);
	if (r)
		return r;
	x = a - b;
	return (x > 0) - (x < 0);
}

/* this function gets called *a lot*, it must be very fast */
int track_info_cmp(const struct track_info *a, const struct track_info *b, const sort_key_t *keys)
{
	int i, rev = 0, res = 0;

	for (i = 0; keys[i] != SORT_INVALID; i++) {
		sort_key_t key = keys[i];
		const char *av, *bv;

		rev = 0;
		if (key >= REV_SORT__START) {
			rev = 1;
			key -= REV_SORT__START;
		}

		switch (key) {
		case SORT_TRACKNUMBER:
		case SORT_DISCNUMBER:
		case SORT_DATE:
		case SORT_ORIGINALDATE:
		case SORT_PLAY_COUNT:
		case SORT_BPM:
			res = getentry(a, key, int) - getentry(b, key, int);
			break;
		case SORT_FILEMTIME:
			res = a->mtime - b->mtime;
			break;
		case SORT_FILENAME:
			/* NOTE: filenames are not necessarily UTF-8 */
			res = strcoll(a->filename, b->filename);
			break;
		case SORT_RG_TRACK_GAIN:
		case SORT_RG_TRACK_PEAK:
		case SORT_RG_ALBUM_GAIN:
		case SORT_RG_ALBUM_PEAK:
			res = doublecmp0(getentry(a, key, double), getentry(b, key, double));
			break;
		case SORT_BITRATE:
			res = getentry(a, key, long) - getentry(b, key, long);
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
	return rev ? -res : res;
}

static const struct {
	const char *str;
	sort_key_t key;
} sort_key_map[] = {
	{ "artist",		SORT_ARTIST		},
	{ "album",		SORT_ALBUM		},
	{ "title",		SORT_TITLE		},
	{ "play_count",		SORT_PLAY_COUNT		},
	{ "tracknumber",	SORT_TRACKNUMBER	},
	{ "discnumber",		SORT_DISCNUMBER		},
	{ "date",		SORT_DATE		},
	{ "originaldate",	SORT_ORIGINALDATE	},
	{ "genre",		SORT_GENRE		},
	{ "comment",		SORT_COMMENT		},
	{ "albumartist",	SORT_ALBUMARTIST	},
	{ "filename",		SORT_FILENAME		},
	{ "filemtime",		SORT_FILEMTIME		},
	{ "rg_track_gain",	SORT_RG_TRACK_GAIN	},
	{ "rg_track_peak",	SORT_RG_TRACK_PEAK	},
	{ "rg_album_gain",	SORT_RG_ALBUM_GAIN	},
	{ "rg_album_peak",	SORT_RG_ALBUM_PEAK	},
	{ "bitrate",		SORT_BITRATE		},
	{ "codec",		SORT_CODEC		},
	{ "codec_profile",	SORT_CODEC_PROFILE	},
	{ "media",		SORT_MEDIA		},
	{ "bpm",		SORT_BPM		},
	{ "-artist",		REV_SORT_ARTIST		},
	{ "-album",		REV_SORT_ALBUM		},
	{ "-title",		REV_SORT_TITLE		},
	{ "-play_count", 	REV_SORT_PLAY_COUNT	},
	{ "-tracknumber",	REV_SORT_TRACKNUMBER	},
	{ "-discnumber",	REV_SORT_DISCNUMBER	},
	{ "-date",		REV_SORT_DATE		},
	{ "-originaldate",	REV_SORT_ORIGINALDATE	},
	{ "-genre",		REV_SORT_GENRE		},
	{ "-comment",		REV_SORT_COMMENT	},
	{ "-albumartist",	REV_SORT_ALBUMARTIST	},
	{ "-filename",		REV_SORT_FILENAME	},
	{ "-filemtime",		REV_SORT_FILEMTIME	},
	{ "-rg_track_gain",	REV_SORT_RG_TRACK_GAIN	},
	{ "-rg_track_peak",	REV_SORT_RG_TRACK_PEAK	},
	{ "-rg_album_gain",	REV_SORT_RG_ALBUM_GAIN	},
	{ "-rg_album_peak",	REV_SORT_RG_ALBUM_PEAK	},
	{ "-bitrate",		REV_SORT_BITRATE	},
	{ "-codec",		REV_SORT_CODEC		},
	{ "-codec_profile",	REV_SORT_CODEC_PROFILE	},
	{ "-media",		REV_SORT_MEDIA		},
	{ "-bpm",		REV_SORT_BPM		},
	{ NULL,                 SORT_INVALID            }
};

sort_key_t *parse_sort_keys(const char *value)
{
	sort_key_t *keys;
	const char *s, *e;
	int size = 4;
	int pos = 0;

	keys = xnew(sort_key_t, size);

	s = value;
	while (1) {
		char buf[32];
		int i, len;

		while (*s == ' ')
			s++;

		e = s;
		while (*e && *e != ' ')
			e++;

		len = e - s;
		if (len == 0)
			break;
		if (len > 31)
			len = 31;

		memcpy(buf, s, len);
		buf[len] = 0;
		s = e;

		for (i = 0; ; i++) {
			if (sort_key_map[i].str == NULL) {
				error_msg("invalid sort key '%s'", buf);
				free(keys);
				return NULL;
			}

			if (strcmp(buf, sort_key_map[i].str) == 0)
				break;
		}
		if (pos == size - 1) {
			size *= 2;
			keys = xrenew(sort_key_t, keys, size);
		}
		keys[pos++] = sort_key_map[i].key;
	}
	keys[pos] = SORT_INVALID;
	return keys;
}

const char *sort_key_to_str(sort_key_t key)
{
	int i;
	for (i = 0; sort_key_map[i].str; i++) {
		if (sort_key_map[i].key == key)
			return sort_key_map[i].str;
	}
	return NULL;
}

void sort_keys_to_str(const sort_key_t *keys, char *buf, size_t bufsize)
{
	int i, pos = 0;

	for (i = 0; keys[i] != SORT_INVALID; i++) {
		const char *key = sort_key_to_str(keys[i]);
		int len = strlen(key);

		if ((int)bufsize - pos - len - 2 < 0)
			break;

		memcpy(buf + pos, key, len);
		pos += len;
		buf[pos++] = ' ';
	}
	if (pos > 0)
		pos--;
	buf[pos] = 0;
}
