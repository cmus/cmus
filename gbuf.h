/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2008 Timo Hirvonen
 *
 * This code is largely based on strbuf in the GIT version control system.
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

#ifndef CMUS_GBUF_H
#define CMUS_GBUF_H

#include "compiler.h"

#include <stddef.h> /* size_t */

struct gbuf {
	char *buffer;
	size_t alloc;
	size_t len;
};

extern char gbuf_empty_buffer[];

#define GBUF(name) struct gbuf name = { gbuf_empty_buffer, 0, 0 }

static inline void gbuf_clear(struct gbuf *buf)
{
	buf->len = 0;
	buf->buffer[0] = 0;
}

static inline size_t gbuf_avail(struct gbuf *buf)
{
	if (buf->alloc)
		return buf->alloc - buf->len - 1;
	return 0;
}

void gbuf_grow(struct gbuf *buf, size_t more);
void gbuf_free(struct gbuf *buf);
void gbuf_add_ch(struct gbuf *buf, char ch);
void gbuf_add_bytes(struct gbuf *buf, const void *data, size_t len);
void gbuf_add_str(struct gbuf *buf, const char *str);
void gbuf_addf(struct gbuf *buf, const char *fmt, ...) CMUS_FORMAT(2, 3);
void gbuf_set(struct gbuf *buf, int c, size_t count);
char *gbuf_steal(struct gbuf *buf);

#endif
