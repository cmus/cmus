#include "buffer.h"
#include "xmalloc.h"
#include "locking.h"
#include "debug.h"

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

unsigned int buffer_nr_chunks;

static pthread_mutex_t buffer_mutex = CMUS_MUTEX_INITIALIZER;
static struct chunk *buffer_chunks = NULL;
static unsigned int buffer_ridx;
static unsigned int buffer_widx;

void buffer_init(void)
{
	free(buffer_chunks);
	buffer_chunks = xnew(struct chunk, buffer_nr_chunks);
	buffer_reset();
}

void buffer_free(void)
{
	free(buffer_chunks);
}

/*
 * @pos: returned pointer to available data
 *
 * Returns number of bytes available at @pos
 *
 * After reading bytes mark them consumed calling buffer_consume().
 */
int buffer_get_rpos(char **pos)
{
	struct chunk *c;
	int size = 0;

	cmus_mutex_lock(&buffer_mutex);
	c = &buffer_chunks[buffer_ridx];
	if (c->filled) {
		size = c->h - c->l;
		*pos = c->data + c->l;
	}
	cmus_mutex_unlock(&buffer_mutex);

	return size;
}

/*
 * @pos: pointer to buffer position where data can be written
 *
 * Returns number of bytes can be written to @pos.  If the return value is
 * non-zero it is guaranteed to be >= 1024.
 *
 * After writing bytes mark them filled calling buffer_fill().
 */
int buffer_get_wpos(char **pos)
{
	struct chunk *c;
	int size = 0;

	cmus_mutex_lock(&buffer_mutex);
	c = &buffer_chunks[buffer_widx];
	if (!c->filled) {
		size = CHUNK_SIZE - c->h;
		*pos = c->data + c->h;
	}
	cmus_mutex_unlock(&buffer_mutex);

	return size;
}

void buffer_consume(int count)
{
	struct chunk *c;

	BUG_ON(count < 0);
	cmus_mutex_lock(&buffer_mutex);
	c = &buffer_chunks[buffer_ridx];
	BUG_ON(!c->filled);
	c->l += count;
	if (c->l == c->h) {
		c->l = 0;
		c->h = 0;
		c->filled = 0;
		buffer_ridx++;
		buffer_ridx %= buffer_nr_chunks;
	}
	cmus_mutex_unlock(&buffer_mutex);
}

/* chunk is marked filled if free bytes < 1024 or count == 0 */
int buffer_fill(int count)
{
	struct chunk *c;
	int filled = 0;

	cmus_mutex_lock(&buffer_mutex);
	c = &buffer_chunks[buffer_widx];
	BUG_ON(c->filled);
	c->h += count;

	if (CHUNK_SIZE - c->h < 1024 || (count == 0 && c->h > 0)) {
		c->filled = 1;
		buffer_widx++;
		buffer_widx %= buffer_nr_chunks;
		filled = 1;
	}

	cmus_mutex_unlock(&buffer_mutex);
	return filled;
}

void buffer_reset(void)
{
	int i;

	cmus_mutex_lock(&buffer_mutex);
	buffer_ridx = 0;
	buffer_widx = 0;
	for (i = 0; i < buffer_nr_chunks; i++) {
		buffer_chunks[i].l = 0;
		buffer_chunks[i].h = 0;
		buffer_chunks[i].filled = 0;
	}
	cmus_mutex_unlock(&buffer_mutex);
}

int buffer_get_filled_chunks(void)
{
	int c;

	cmus_mutex_lock(&buffer_mutex);
	if (buffer_ridx < buffer_widx) {
		/*
		 * |__##########____|
		 *    r         w
		 *
		 * |############____|
		 *  r           w
		 */
		c = buffer_widx - buffer_ridx;
	} else if (buffer_ridx > buffer_widx) {
		/*
		 * |#######______###|
		 *         w     r
		 *
		 * |_____________###|
		 *  w            r
		 */
		c = buffer_nr_chunks - buffer_ridx + buffer_widx;
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
		if (buffer_chunks[buffer_ridx].filled) {
			c = buffer_nr_chunks;
		} else {
			c = 0;
		}
	}
	cmus_mutex_unlock(&buffer_mutex);
	return c;
}
