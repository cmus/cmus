/*
 * Copyright 2008-2013 Various Authors
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _UCHAR_H
#define _UCHAR_H

#include <stddef.h> /* size_t */

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
int u_char_width(uchar uch);

/*
 * @str  any null-terminated string
 *
 * Returns 1 if @str is valid UTF-8 string, 0 otherwise.
 */
int u_is_valid(const char *str);

/*
 * @str  valid, null-terminated UTF-8 string
 *
 * Returns position of next unicode character in @str.
 */
extern const char * const utf8_skip;
static inline char *u_next_char(const char *str)
{
	return (char *) (str + utf8_skip[*((const unsigned char *) str)]);
}

/*
 * @str  valid, null-terminated UTF-8 string
 *
 * Retuns length of @str in UTF-8 characters.
 */
size_t u_strlen(const char *str);

/*
 * @str  null-terminated UTF-8 string
 *
 * Retuns length of @str in UTF-8 characters.
 * Invalid chars are counted as single characters.
 */
size_t u_strlen_safe(const char *str);

/*
 * @str  null-terminated UTF-8 string
 *
 * Retuns width of @str.
 */
int u_str_width(const char *str);

/*
 * @str  null-terminated UTF-8 string
 * @len  number of characters to measure
 *
 * Retuns width of the first @len characters in @str.
 */
int u_str_nwidth(const char *str, int len);

/*
 * @str  null-terminated UTF-8 string
 * @uch  unicode character
 *
 * Returns a pointer to the first occurrence of @uch in the @str.
 */
char *u_strchr(const char *str, uchar uch);

void u_prev_char_pos(const char *str, int *idx);

/*
 * @str  null-terminated UTF-8 string
 * @idx  pointer to byte index in @str (not UTF-8 character index!) or NULL
 *
 * Returns unicode character at @str[*@idx] or @str[0] if @idx is NULL.
 * Stores byte index of the next char back to @idx if set.
 */
uchar u_get_char(const char *str, int *idx);

/*
 * @str  destination buffer
 * @idx  pointer to byte index in @str (not UTF-8 character index!)
 * @uch  unicode character
 */
void u_set_char_raw(char *str, int *idx, uchar uch);
void u_set_char(char *str, int *idx, uchar uch);

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
int u_copy_chars(char *dst, const char *src, int *width);

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
int u_skip_chars(const char *str, int *width);

/*
 * @str  valid null-terminated UTF-8 string
 *
 * Converts a string into a form that is independent of case.
 *
 * Returns a newly allocated string
 */
char *u_casefold(const char *str);

/*
 * @str1  valid, normalized, null-terminated UTF-8 string
 * @str2  valid, normalized, null-terminated UTF-8 string
 *
 * Returns 1 if @str1 is equal to @str2, ignoring the case of the characters.
 */
int u_strcase_equal(const char *str1, const char *str2);

/*
 * @str1    valid, normalized, null-terminated UTF-8 string
 * @str2    valid, normalized, null-terminated UTF-8 string
 * @len  number of characters to consider for comparison
 *
 * Returns 1 if the first @len characters of @str1 and @str2 are equal,
 * ignoring the case of the characters (0 otherwise).
 */
int u_strncase_equal(const char *str1, const char *str2, size_t len);

/*
 * @str1    valid, normalized, null-terminated UTF-8 string
 * @str2    valid, normalized, null-terminated UTF-8 string
 * @len  number of characters to consider for comparison
 *
 * Like u_strncase_equal(), but uses only base characters for comparison
 * (e.g. "Trentemöller" matches "Trentemøller")
 */
int u_strncase_equal_base(const char *str1, const char *str2, size_t len);

/*
 * @haystack  valid, normalized, null-terminated UTF-8 string
 * @needle    valid, normalized, null-terminated UTF-8 string
 *
 * Returns position of @needle in @haystack (case insensitive comparison).
 */
char *u_strcasestr(const char *haystack, const char *needle);

/*
 * @haystack  valid, normalized, null-terminated UTF-8 string
 * @needle    valid, normalized, null-terminated UTF-8 string
 *
 * Like u_strcasestr(), but uses only base characters for comparison
 * (e.g. "Trentemöller" matches "Trentemøller")
 */
char *u_strcasestr_base(const char *haystack, const char *needle);

/*
 * @haystack  null-terminated string in local encoding
 * @needle    valid, normalized, null-terminated UTF-8 string
 *
 * Like u_strcasestr_base(), but converts @haystack to UTF-8 if necessary.
 */
char *u_strcasestr_filename(const char *haystack, const char *needle);

#endif
