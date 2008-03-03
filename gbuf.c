/*
 * This code is largely based on strbuf in the GIT version control system.
 */

#include "gbuf.h"
#include "xmalloc.h"

#include <stdarg.h>

char gbuf_empty_buffer[1];

static inline void gbuf_init(struct gbuf *buf)
{
	buf->buffer = gbuf_empty_buffer;
	buf->alloc = 0;
	buf->len = 0;
}

void gbuf_grow(struct gbuf *buf, size_t more)
{
	size_t align = 16 - 1;
	size_t alloc = (buf->len + more + 1 + align) & ~align;

	if (alloc > buf->alloc) {
		if (!buf->alloc)
			buf->buffer = NULL;
		buf->alloc = alloc;
		buf->buffer = xrealloc(buf->buffer, buf->alloc);
		// gbuf is not NUL terminated if this was first alloc
		buf->buffer[buf->len] = 0;
	}
}

void gbuf_free(struct gbuf *buf)
{
	if (buf->alloc)
		free(buf->buffer);
	gbuf_init(buf);
}

void gbuf_add_ch(struct gbuf *buf, char ch)
{
	gbuf_grow(buf, 1);
	buf->buffer[buf->len++] = ch;
	buf->buffer[buf->len] = 0;
}

void gbuf_add_bytes(struct gbuf *buf, const void *data, size_t len)
{
	gbuf_grow(buf, len);
	memcpy(buf->buffer + buf->len, data, len);
	buf->len += len;
	buf->buffer[buf->len] = 0;
}

void gbuf_add_str(struct gbuf *buf, const char *str)
{
	int len = strlen(str);

	if (!len)
		return;
	gbuf_grow(buf, len);
	memcpy(buf->buffer + buf->len, str, len + 1);
	buf->len += len;
}

void gbuf_addf(struct gbuf *buf, const char *fmt, ...)
{
	va_list ap;
	size_t avail = gbuf_avail(buf);
	int slen;

	va_start(ap, fmt);
	slen = vsnprintf(buf->buffer + buf->len, avail, fmt, ap);
	va_end(ap);

	if (slen > avail) {
		gbuf_grow(buf, slen);

		va_start(ap, fmt);
		slen = vsnprintf(buf->buffer + buf->len, gbuf_avail(buf), fmt, ap);
		va_end(ap);
	}

	buf->len += slen;
	buf->buffer[buf->len] = 0;
}

void gbuf_set(struct gbuf *buf, int c, size_t count)
{
	gbuf_grow(buf, count);
	memset(buf->buffer + buf->len, c, count);
	buf->len += count;
	buf->buffer[buf->len] = 0;
}

char *gbuf_steal(struct gbuf *buf)
{
	char *b = buf->buffer;
	if (!buf->alloc)
		b = xnew0(char, 1);
	gbuf_init(buf);
	return b;
}
