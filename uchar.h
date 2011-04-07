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

extern const char hex_tab[16];

/*
 * Invalid bytes are or'ed with this
 * for example 0xff -> 0x100000ff
 */
#define U_INVALID_MASK 0x10000000U

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
		return 1;
	}
}

/*
 * Returns width of @uch (normally 1 or 2, 4 for invalid chars (<xx>))
 */
extern int u_char_width(uchar uch);

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
 *
 * Retuns width of @str.
 */
extern int u_str_width(const char *str);

/*
 * @str  null-terminated UTF-8 string
 * @len  number of characters to measure
 *
 * Retuns width of the first @len characters in @str.
 */
extern int u_str_nwidth(const char *str, int len);

extern void u_prev_char_pos(const char *str, int *idx);

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
extern void u_set_char_raw(char *str, int *idx, uchar uch);
extern void u_set_char(char *str, int *idx, uchar uch);

/*
 * @dst    destination buffer
 * @src    null-terminated UTF-8 string
 * @width  how much to copy
 *
 * Copies at most @count characters, less if null byte was hit.
 * Null byte is _never_ copied.
 * Actual width of copied characters is stored to @width.
 *
 * Returns number of _bytes_ copied.
 */
extern int u_copy_chars(char *dst, const char *src, int *width);

/*
 * @dst    destination buffer
 * @src    null-terminated UTF-8 string
 * @len    how many bytes are available in @dst
 *
 * Copies at most @len bytes, less if null byte was hit. Replaces every
 * non-ascii character by '?'. Null byte is _never_ copied.
 *
 * Returns number of bytes written to @dst.
 */
int u_to_ascii(char *dst, const char *src, int len);

/*
 * @str    null-terminated UTF-8 string, must be long enough
 * @width  how much to skip
 *
 * Skips @count UTF-8 characters.
 * Total width of skipped characters is stored to @width.
 * Returned @width can be the given @width + 1 if the last skipped
 * character was double width.
 *
 * Returns number of _bytes_ skipped.
 */
extern int u_skip_chars(const char *str, int *width);

extern int u_strcasecmp(const char *a, const char *b);
extern int u_strncasecmp(const char *a, const char *b, int len);
extern char *u_strcasestr(const char *haystack, const char *needle);

/*
 * @haystack  null-terminated string in local encoding
 * @needle    valid, normalized, null-terminated UTF-8 string
 *
 * Like u_strcasestr_base(), but converts @haystack to UTF-8 if necessary.
 */
char *u_strcasestr_filename(const char *haystack, const char *needle);

#endif
