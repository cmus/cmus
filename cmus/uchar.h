/* 
 * Copyright 2004-2005 Timo Hirvonen
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

#ifndef _UCHAR_H
#define _UCHAR_H

typedef unsigned int uchar;

/*
 * @byte  any byte in UTF-8 string
 *
 * Returns 1 if @byte is the first byte of unicode char, 0 otherwise
 */
static inline int u_is_first_byte(unsigned char byte)
{
	return byte >> 6 != 2;
}

/*
 * @uch  potential unicode character
 *
 * Returns 1 if @uch is valid unicode character, 0 otherwise
 */
static inline int u_is_unicode(uchar uch)
{
	return uch <= 0x0010ffffU;
}

/*
 * Returns size of @uch in bytes
 */
static inline int u_char_size(uchar uch)
{
	if (uch <= 0x0000007fU) {
		return 1;
	} else if (uch <= 0x000007ffU) {
		return 2;
	} else if (uch <= 0x0000ffffU) {
		return 3;
	} else if (uch <= 0x0010ffffU) {
		return 4;
	} else {
		return -1;
	}
}

/*
 * @str  any null-terminated string
 *
 * Returns 1 if @str is valid UTF-8 string, 0 otherwise.
 */
extern int u_is_valid(const char *str);

/*
 * @str  null-terminated UTF-8 string
 *
 * Retuns length of @str in UTF-8 characters.
 */
extern int u_strlen(const char *str);

/*
 * @str  null-terminated UTF-8 string
 * @idx  pointer to byte index in @str (not UTF-8 character index!)
 * @uch  pointer to returned unicode character
 */
extern void u_get_char(const char *str, int *idx, uchar *uch);

/*
 * @str  destination buffer
 * @idx  pointer to byte index in @str (not UTF-8 character index!)
 * @uch  unicode character
 */
extern void u_set_char(char *str, int *idx, uchar uch);

/*
 * @dst    destination buffer
 * @src    null-terminated UTF-8 string
 * @count  number of UTF-8 characters to copy
 *
 * Copies at most @count characters, less if null byte was hit.
 * Null byte is _never_ copied.
 *
 * Returns number of _bytes_ copied.
 */
extern int u_copy_chars(char *dst, const char *src, int count);

/*
 * @str    null-terminated UTF-8 string, length >= @count
 * @count  number of UTF-8 characters to skip
 *
 * Skips @count UTF-8 characters.
 *
 * Returns number of _bytes_ skipped.
 */
extern int u_skip_chars(const char *str, int count);

extern char *u_strcasestr(const char *text, const char *part);

#endif
