/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2007 Johannes Weißl
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

#ifndef _APE_H
#define _APE_H

#include <stdint.h>
#include <stdlib.h>

struct ape_header {
	/* 1000 or 2000 (1.0, 2.0) */
	uint32_t version;

	/* tag size (header + tags, excluding footer) */
	uint32_t size;

	/* number of items */
	uint32_t count;

	/* global flags for each tag
	 * there are also private flags for every tag
	 * NOTE: 0 for version 1.0 (1000)
	 */
	uint32_t flags;
};

/* ape flags */
#define AF_IS_UTF8(f)		(((f) & 6) == 0)
#define AF_IS_FOOTER(f)		(((f) & (1 << 29)) == 0)

struct apetag {
	char *buf;
	int pos;
	struct ape_header header;
};

#define APETAG(name) struct apetag name = { .buf = NULL, .pos = 0, }

int ape_read_tags(struct apetag *ape, int fd, int slow);
char *ape_get_comment(struct apetag *ape, char **val);

static inline void ape_free(struct apetag *ape)
{
	free(ape->buf);
}

#endif
