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

#ifndef CONVERT_H
#define CONVERT_H

#include <sys/types.h> /* ssize_t */

/* Returns length of *outbuf in bytes (without closing '\0'), -1 on error. */
ssize_t convert(const char *inbuf, ssize_t inbuf_size,
		char **outbuf, ssize_t outbuf_estimate,
		const char *tocode, const char *fromcode);

int utf8_encode(const char *inbuf, const char *encoding, char **outbuf);

char *to_utf8(const char *str, const char *enc);

#endif
