/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2006 Chun-Yu Shei <cshei AT cs.indiana.edu>
 *
 * Cleaned up by Timo Hirvonen <tihirvon@gmail.com>
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

#include "ape.h"
#include "file.h"
#include "xmalloc.h"
#include "utils.h"

#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <strings.h>

/* http://www.personal.uni-jena.de/~pfk/mpp/sv8/apetag.html */

#define PREAMBLE_SIZE (8)
static const char preamble[PREAMBLE_SIZE] = { 'A', 'P', 'E', 'T', 'A', 'G', 'E', 'X' };

/* NOTE: not sizeof(struct ape_header)! */
#define HEADER_SIZE (32)

/* returns position of APE header or -1 if not found */
static int find_ape_tag_slow(int fd)
{
	char buf[4096];
	int match = 0;
	int pos = 0;

	/* seek to start of file */
	if (lseek(fd, pos, SEEK_SET) == -1)
		return -1;

	while (1) {
		int i, got = read(fd, buf, sizeof(buf));

		if (got == -1) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			break;
		}
		if (got == 0)
			break;

		for (i = 0; i < got; i++) {
			if (buf[i] != preamble[match]) {
				match = 0;
				continue;
			}

			match++;
			if (match == PREAMBLE_SIZE)
				return pos + i + 1 - PREAMBLE_SIZE;
		}
		pos += got;
	}
	return -1;
}

static int ape_parse_header(const char *buf, struct ape_header *h)
{
	if (memcmp(buf, preamble, PREAMBLE_SIZE))
		return 0;

	h->version = read_le32(buf + 8);
	h->size = read_le32(buf + 12);
	h->count = read_le32(buf + 16);
	h->flags = read_le32(buf + 20);
	return 1;
}

static int read_header(int fd, struct ape_header *h)
{
	char buf[HEADER_SIZE];

	if (read_all(fd, buf, sizeof(buf)) != sizeof(buf))
		return 0;
	return ape_parse_header(buf, h);
}

/* sets fd right after the header and returns 1 if found,
 * otherwise returns 0
 */
static int find_ape_tag(int fd, struct ape_header *h, int slow)
{
	int pos;

	if (lseek(fd, -HEADER_SIZE, SEEK_END) == -1)
		return 0;
	if (read_header(fd, h))
		return 1;

	/* try to skip ID3v1 tag at the end of the file */
	if (lseek(fd, -(HEADER_SIZE + 128), SEEK_END) == -1)
		return 0;
	if (read_header(fd, h))
		return 1;

	if (!slow)
		return 0;

	pos = find_ape_tag_slow(fd);
	if (pos == -1)
		return 0;
	if (lseek(fd, pos, SEEK_SET) == -1)
		return 0;
	return read_header(fd, h);
}

/*
 * All keys are ASCII and length is 2..255
 *
 * UTF-8:	Artist, Album, Title, Genre
 * Integer:	Track (N or N/M)
 * Date:	Year (release), "Record Date"
 *
 * UTF-8 strings are NOT zero terminated.
 *
 * Also support "discnumber" (vorbis) and "disc" (non-standard)
 */
static int ape_parse_one(const char *buf, int size, char **keyp, char **valp)
{
	int pos = 0;

	while (size - pos > 8) {
		uint32_t val_len, flags;
		char *key, *val;
		int max_key_len, key_len;

		val_len = read_le32(buf + pos); pos += 4;
		flags = read_le32(buf + pos); pos += 4;

		max_key_len = size - pos - val_len - 1;
		if (max_key_len < 0) {
			/* corrupt */
			break;
		}

		for (key_len = 0; key_len < max_key_len && buf[pos + key_len]; key_len++)
			; /* nothing */
		if (buf[pos + key_len]) {
			/* corrupt */
			break;
		}

		if (!AF_IS_UTF8(flags)) {
			/* ignore binary data */
			pos += key_len + 1 + val_len;
			continue;
		}

		key = xstrdup(buf + pos);
		pos += key_len + 1;

		/* should not be NUL-terminated */
		val = xstrndup(buf + pos, val_len);
		pos += val_len;

		/* could be moved to comment.c but I don't think anyone else would use it */
		if (!strcasecmp(key, "record date") || !strcasecmp(key, "year")) {
			free(key);
			key = xstrdup("date");
		}

		if (!strcasecmp(key, "date")) {
			/* Date format
			 *
			 * 1999-08-11 12:34:56
			 * 1999-08-11 12:34
			 * 1999-08-11
			 * 1999-08
			 * 1999
			 * 1999-W34	(week 34, totally crazy)
			 *
			 * convert to year, pl.c supports only years anyways
			 *
			 * FIXME: which one is the most common tag (year or record date)?
			 */
			if (strlen(val) > 4)
				val[4] = 0;
		}

		*keyp = key;
		*valp = val;
		return pos;
	}
	return -1;
}

/* return the number of comments, or -1 */
int ape_read_tags(struct apetag *ape, int fd, int slow)
{
	struct ape_header *h = &ape->header;
	int rc = -1;
	off_t old_pos;

	/* save position */
	old_pos = lseek(fd, 0, SEEK_CUR);

	if (!find_ape_tag(fd, h, slow))
		goto fail;

	if (AF_IS_FOOTER(h->flags)) {
		/* seek back right after the header */
		if (lseek(fd, -((int)h->size), SEEK_CUR) == -1)
			goto fail;
	}

	/* ignore insane tags */
	if (h->size > 1024 * 1024)
		goto fail;

	ape->buf = xnew(char, h->size);
	if (read_all(fd, ape->buf, h->size) != h->size)
		goto fail;

	rc = h->count;

fail:
	lseek(fd, old_pos, SEEK_SET);
	return rc;
}

/* returned key-name must be free'd */
char *ape_get_comment(struct apetag *ape, char **val)
{
	struct ape_header *h = &ape->header;
	char *key;
	int rc;

	if (ape->pos >= h->size)
		return NULL;

	rc = ape_parse_one(ape->buf + ape->pos, h->size - ape->pos, &key, val);
	if (rc < 0)
		return NULL;
	ape->pos += rc;

	return key;
}
