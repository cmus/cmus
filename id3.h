/*
 * Copyright 2005 Timo Hirvonen
 */

#ifndef _ID3_H
#define _ID3_H

#include <string.h>

/* flags for id3_read_tags */
#define ID3_V1	(1 << 0)
#define ID3_V2	(1 << 1)

enum id3_key {
	ID3_ARTIST,
	ID3_ALBUM,
	ID3_TITLE,
	ID3_DATE,
	ID3_GENRE,
	ID3_DISC,
	ID3_TRACK,
	ID3_ALBUMARTIST,
	ID3_ARTISTSORT,
	ID3_ALBUMARTISTSORT,
	ID3_COMPILATION,
	ID3_RG_TRACK_GAIN,
	ID3_RG_TRACK_PEAK,
	ID3_RG_ALBUM_GAIN,
	ID3_RG_ALBUM_PEAK,
	NUM_ID3_KEYS
};

struct id3tag {
	char v1[128];
	char *v2[NUM_ID3_KEYS];

	unsigned int has_v1 : 1;
	unsigned int has_v2 : 1;
};

#define UTF16_IS_LSURROGATE(uch) (0xdc00 <= uch && 0xdfff >= uch)
#define UTF16_IS_HSURROGATE(uch) (0xd800 <= uch && 0xdbff >= uch)
#define UTF16_IS_BOM(uch) (uch == 0xfeff)

extern const char * const id3_key_names[NUM_ID3_KEYS];

int id3_tag_size(const char *buf, int buf_size);

static inline void id3_init(struct id3tag *id3)
{
	memset(id3, 0, sizeof(*id3));
}

void id3_free(struct id3tag *id3);
int id3_read_tags(struct id3tag *id3, int fd, unsigned int flags);
char *id3_get_comment(struct id3tag *id3, enum id3_key key);

#endif
