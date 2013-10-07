/*
 * Copyright 2008-2013 Various Authors
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "convert.h"
#include "xmalloc.h"
#include "uchar.h"
#ifdef HAVE_CONFIG
#include "config/iconv.h"
#endif

#ifdef HAVE_ICONV
#include <iconv.h>
#endif
#include <string.h>
#include <errno.h>

ssize_t convert(const char *inbuf, ssize_t inbuf_size,
		char **outbuf, ssize_t outbuf_estimate,
		const char *tocode, const char *fromcode)
{
#ifdef HAVE_ICONV
	const char *in;
	char *out;
	size_t rc, outbuf_size, inbytesleft, outbytesleft;
	iconv_t cd;
	int finished = 0, err_save;

	cd = iconv_open(tocode, fromcode);
	if (cd == (iconv_t) -1)
		return -1;

	if (inbuf_size < 0)
		inbuf_size = strlen(inbuf);
	inbytesleft = inbuf_size;

	if (outbuf_estimate < 0)
		outbuf_size = inbuf_size;
	else
		outbuf_size = outbuf_estimate;
	outbytesleft = outbuf_size;

	in = inbuf;
	out = *outbuf = xnew(char, outbuf_size + 1);

	while (!finished) {
		finished = 1;
		rc = iconv(cd, (char **)&in, &inbytesleft, &out, &outbytesleft);
		if (rc == (size_t) -1) {
			if (errno == E2BIG) {
				size_t used = out - *outbuf;
				outbytesleft += outbuf_size;
				outbuf_size *= 2;
				*outbuf = xrenew(char, *outbuf, outbuf_size + 1);
				out = *outbuf + used;
				continue;
			} else if (errno != EINVAL)
				goto error;
		}
	}
	/* NUL-terminate for safety reasons */
	*out = '\0';
	iconv_close(cd);
	return outbuf_size - outbytesleft;

error:
	err_save = errno;
	free(*outbuf);
	*outbuf = NULL;
	iconv_close(cd);
	errno = err_save;
	return -1;

#else
	if (inbuf_size < 0)
		inbuf_size = strlen(inbuf);
	*outbuf = xnew(char, inbuf_size + 1);
	memcpy(*outbuf, inbuf, inbuf_size);
	(*outbuf)[inbuf_size] = '\0';
	return inbuf_size;
#endif
}

int utf8_encode(const char *inbuf, const char *encoding, char **outbuf)
{
	size_t inbuf_size, outbuf_size, i;
	int rc;

	inbuf_size = strlen(inbuf);
	outbuf_size = inbuf_size;
	for (i = 0; i < inbuf_size; i++) {
		unsigned char ch;

		ch = inbuf[i];
		if (ch > 127)
			outbuf_size++;
	}

	rc = convert(inbuf, inbuf_size, outbuf, outbuf_size, "UTF-8", encoding);

	return rc < 0 ? -1 : 0;
}

char *to_utf8(const char *str, const char *enc)
{
	char *outbuf = NULL;
	int rc;

	if (u_is_valid(str)) {
		return xstrdup(str);
	} else {
		rc = utf8_encode(str, enc, &outbuf);
		return rc < 0 ? xstrdup(str) : outbuf;
	}
}
