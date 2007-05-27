/*
 * Copyright 2007 Johannes Weißl
 */

#ifndef _APE_H
#define _APE_H

#include <inttypes.h>
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

extern int ape_read_tags(struct apetag *ape, int fd, int slow);
extern char *ape_get_comment(struct apetag *ape, char **val);

static inline void ape_free(struct apetag *ape)
{
	free(ape->buf);
}

#endif
