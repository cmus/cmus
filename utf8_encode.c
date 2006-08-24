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

#include "utf8_encode.h"
#include "xmalloc.h"

#include <iconv.h>
#include <string.h>
#include <errno.h>

int utf8_encode(const char *inbuf, const char *encoding, char **outbuf)
{
	const char *in;
	char *out;
	size_t inbuf_size, outbuf_size, i;
	iconv_t cd;
	int rc;

	cd = iconv_open("UTF-8", encoding);
	if (cd == (iconv_t)-1)
		return -1;
	inbuf_size = strlen(inbuf);
	outbuf_size = inbuf_size;
	for (i = 0; i < inbuf_size; i++) {
		unsigned char ch;
		
		ch = inbuf[i];
		if (ch > 127)
			outbuf_size++;
	}
	*outbuf = xnew(char, outbuf_size + 1);
	in = inbuf;
	out = *outbuf;
	rc = iconv(cd, (char **)&in, &inbuf_size, &out, &outbuf_size);
	*out = 0;
	if (rc == -1) {
		int save = errno;
		iconv_close(cd);
		free(*outbuf);
		*outbuf = NULL;
		errno = save;
		return -1;
	}
	rc = iconv_close(cd);
	if (rc == -1) {
		int save = errno;
		free(*outbuf);
		*outbuf = NULL;
		errno = save;
		return -1;
	}
	return 0;
}
