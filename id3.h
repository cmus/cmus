/*
 * Copyright 2005 Timo Hirvonen
 */

#ifndef _ID3_H
#define _ID3_H

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
	ID3_TRACK
};
#define NUM_ID3_KEYS (ID3_TRACK + 1)

typedef struct ID3 ID3;

extern int id3_tag_size(const char *buf, int buf_size);

extern ID3 *id3_new(void);
extern void id3_free(ID3 *id3);
extern int id3_read_tags(ID3 *id3, int fd, unsigned int flags);
extern char *id3_get_comment(ID3 *id3, enum id3_key key);

#endif
