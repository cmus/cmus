/* 
 * Copyright 2004 Timo Hirvonen
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

#ifndef _BUFFER_H
#define _BUFFER_H

#include <xmalloc.h>
#include <locking.h>
#include <debug.h>

#define CHUNK_SIZE (64 * 1024)

/*
 * chunk can be accessed by either consumer OR producer, not both at same time
 * -> no need to lock
 */
struct chunk {
	char data[CHUNK_SIZE];

	/* index to data, first filled byte */
	unsigned int l;

	/* index to data, last filled byte + 1
	 *
	 * there are h - l bytes available (filled)
	 */
	unsigned int h : 31;

	/* if chunk is marked filled it can only be accessed by consumer
	 * otherwise only producer is allowed to access the chunk
	 */
	unsigned int filled : 1;
};

struct buffer {
	struct chunk *chunks;
	unsigned int nr_chunks;
	unsigned int ridx;
	unsigned int widx;
	pthread_mutex_t mutex;
};

static inline void buffer_init(struct buffer *buf, unsigned int nr_chunks)
{
	int i;

	cmus_mutex_init(&buf->mutex);
	buf->nr_chunks = nr_chunks;
	buf->chunks = xnew(struct chunk, buf->nr_chunks);
	buf->ridx = 0;
	buf->widx = 0;
	for (i = 0; i < buf->nr_chunks; i++) {
		buf->chunks[i].l = 0;
		buf->chunks[i].h = 0;
		buf->chunks[i].filled = 0;
	}
}

static inline void buffer_free(struct buffer *buf)
{
	free(buf->chunks);
	buf->chunks = NULL;
	buf->nr_chunks = 0;
	buf->ridx = 0;
	buf->widx = 0;
}

static inline void buffer_resize(struct buffer *buf, unsigned int nr_chunks)
{
	int i;

	cmus_mutex_lock(&buf->mutex);
	free(buf->chunks);
	buf->nr_chunks = nr_chunks;
	buf->chunks = xnew(struct chunk, buf->nr_chunks);
	buf->ridx = 0;
	buf->widx = 0;
	for (i = 0; i < buf->nr_chunks; i++) {
		buf->chunks[i].l = 0;
		buf->chunks[i].h = 0;
		buf->chunks[i].filled = 0;
	}
	cmus_mutex_unlock(&buf->mutex);
}

/*
 * @buf:  the buffer
 * @rpos: returned pointer to available data
 * @size: number of bytes available at @rpos
 *
 * After reading bytes mark them consumed calling buffer_consume().
 */
static inline void buffer_get_rpos(struct buffer *buf, char **rpos, int *size)
{
	struct chunk *c;
	
	cmus_mutex_lock(&buf->mutex);
	c = &buf->chunks[buf->ridx];
	if (c->filled) {
		*size = c->h - c->l;
		*rpos = c->data + c->l;
	} else {
		*size = 0;
		*rpos = NULL;
	}
	cmus_mutex_unlock(&buf->mutex);
}

/*
 * @buf:   the buffer
 * @count: number of bytes consumed
 */
static inline void buffer_consume(struct buffer *buf, int count)
{
	struct chunk *c;
	
	BUG_ON(count <= 0);
	cmus_mutex_lock(&buf->mutex);
	c = &buf->chunks[buf->ridx];
	BUG_ON(!c->filled);
	c->l += count;
	if (c->l == c->h) {
		c->l = 0;
		c->h = 0;
		c->filled = 0;
		buf->ridx++;
		buf->ridx %= buf->nr_chunks;
	}
	cmus_mutex_unlock(&buf->mutex);
}

/*
 * @buf:  the buffer
 * @wpos: pointer to buffer position where data can be written
 * @size: how many bytes can be written to @wpos
 *
 * If @size == 0 buffer is full otherwise @size is guaranteed to be >=1024.
 * After writing bytes mark them filled calling buffer_fill().
 */
static inline void buffer_get_wpos(struct buffer *buf, char **wpos, int *size)
{
	struct chunk *c;
	
	cmus_mutex_lock(&buf->mutex);
	c = &buf->chunks[buf->widx];
	if (c->filled) {
		*size = 0;
		*wpos = NULL;
	} else {
		*size = CHUNK_SIZE - c->h;
		*wpos = c->data + c->h;
	}
	cmus_mutex_unlock(&buf->mutex);
}

/*
 * @buf:   the buffer
 * @count: how many bytes were written to the buffer
 *
 * chunk is marked filled if free bytes < 1024 or count == 0
 */
static inline void buffer_fill(struct buffer *buf, int count)
{
	struct chunk *c;
	
	cmus_mutex_lock(&buf->mutex);
	c = &buf->chunks[buf->widx];
	BUG_ON(c->filled);
	c->h += count;

	if (CHUNK_SIZE - c->h < 1024) {
		c->filled = 1;
		buf->widx++;
		buf->widx %= buf->nr_chunks;
	} else if (count == 0 && c->h > 0) {
		/* count == 0 -> just update the filled bit */
		c->filled = 1;
		buf->widx++;
		buf->widx %= buf->nr_chunks;
	}

	cmus_mutex_unlock(&buf->mutex);
}

/*
 * set buffer empty
 */
static inline void buffer_reset(struct buffer *buf)
{
	int i;

	cmus_mutex_lock(&buf->mutex);
	buf->ridx = 0;
	buf->widx = 0;
	for (i = 0; i < buf->nr_chunks; i++) {
		buf->chunks[i].l = 0;
		buf->chunks[i].h = 0;
		buf->chunks[i].filled = 0;
	}
	cmus_mutex_unlock(&buf->mutex);
}

static inline int buffer_get_filled_chunks(struct buffer *buf)
{
	int c;

	cmus_mutex_lock(&buf->mutex);
	if (buf->ridx < buf->widx) {
		/*
		 * |__##########____|
		 *    r         w
		 *
		 * |############____|
		 *  r           w
		 */
		c = buf->widx - buf->ridx;
	} else if (buf->ridx > buf->widx) {
		/*
		 * |#######______###|
		 *         w     r
		 *
		 * |_____________###|
		 *  w            r
		 */
		c = buf->nr_chunks - buf->ridx + buf->widx;
	} else {
		/*
		 * |################|
		 *     r
		 *     w
		 *
		 * |________________|
		 *     r
		 *     w
		 */
		if (buf->chunks[buf->ridx].filled) {
			c = buf->nr_chunks;
		} else {
			c = 0;
		}
	}
	cmus_mutex_unlock(&buf->mutex);
	return c;
}

static inline int buffer_get_free_chunks(struct buffer *buf)
{
	int c;

	cmus_mutex_lock(&buf->mutex);
	if (buf->ridx < buf->widx) {
		/*
		 * |__##########____|
		 *    r         w
		 *
		 * |############____|
		 *  r           w
		 */
		c = buf->nr_chunks - buf->widx + buf->ridx;
	} else if (buf->ridx > buf->widx) {
		/*
		 * |#######______###|
		 *         w     r
		 *
		 * |_____________###|
		 *  w            r
		 */
		c = buf->ridx - buf->widx;
	} else {
		/*
		 * |################|
		 *     r
		 *     w
		 *
		 * |________________|
		 *     r
		 *     w
		 */
		if (buf->chunks[buf->ridx].filled) {
			c = 0;
		} else {
			c = buf->nr_chunks;
		}
	}
	cmus_mutex_unlock(&buf->mutex);
	return c;
}

static inline int buffer_get_nr_chunks(struct buffer *buf)
{
	int c;

	cmus_mutex_lock(&buf->mutex);
	c = buf->nr_chunks;
	cmus_mutex_unlock(&buf->mutex);
	return c;
}

#if DEBUG > 1
#define buffer_debug(buf) \
	do { \
		int _filled = buffer_get_filled_chunks((buf)); \
		struct chunk *_rc; \
		struct chunk *_wc; \
		cmus_mutex_lock(&(buf)->mutex); \
		_rc = &(buf)->chunks[(buf)->ridx]; \
		_wc = &(buf)->chunks[(buf)->widx]; \
		d_print("r=%2d (%d), w=%2d (%d), %2d / %2d, chunk_size=%7d\n", \
				(buf)->ridx, \
				_rc->filled, \
				(buf)->widx, \
				_wc->filled, \
				_filled, \
				(buf)->nr_chunks, \
				CHUNK_SIZE); \
		cmus_mutex_unlock(&(buf)->mutex); \
	} while (0)
#else
#define buffer_debug(buf) \
	do { \
	} while (0)
#endif

#endif
