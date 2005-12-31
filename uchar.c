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

#include <uchar.h>
#include <compiler.h>

#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include <ctype.h>

const char hex_tab[16] = "0123456789abcdef";

/*
 * Byte Sequence                                             Min       Min        Max
 * ----------------------------------------------------------------------------------
 * 0xxxxxxx                                              0000000   0x00000   0x00007f
 * 110xxxxx 10xxxxxx                                000 10000000   0x00080   0x0007ff
 * 1110xxxx 10xxxxxx 10xxxxxx                  00001000 00000000   0x00800   0x00ffff
 * 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx   00001 00000000 00000000   0x10000   0x10ffff (not 0x1fffff)
 *
 * max: 100   001111   111111   111111  (0x10ffff)
 */

/* Length of UTF-8 byte sequence.
 * Table index is the first byte of UTF-8 sequence.
 */
static const signed char len_tab[256] = {
	/*   0-127  0xxxxxxx */
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,

	/* 128-191  10xxxxxx (invalid first byte) */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

	/* 192-223  110xxxxx */
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,

	/* 224-239  1110xxxx */
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,

	/* 240-244  11110xxx (000 - 100) */
	4, 4, 4, 4, 4,

	/* 11110xxx (101 - 111) (always invalid) */
	-1, -1, -1,

	/* 11111xxx (always invalid) */
	-1, -1, -1, -1, -1, -1, -1, -1
};

/* index is length of the UTF-8 sequence - 1 */
static int min_val[4] = { 0x000000, 0x000080, 0x000800, 0x010000 };
static int max_val[4] = { 0x00007f, 0x0007ff, 0x00ffff, 0x10ffff };

/* get value bits from the first UTF-8 sequence byte */
static unsigned int first_byte_mask[4] = { 0x7f, 0x1f, 0x0f, 0x07 };

int u_is_valid(const char *str)
{
	const unsigned char *s = (const unsigned char *)str;
	int i = 0;

	while (s[i]) {
		unsigned char ch = s[i++];
		int len = len_tab[ch];

		if (len <= 0)
			return 0;

		if (len > 1) {
			/* len - 1 10xxxxxx bytes */
			uchar u;
			int c;

			len--;
			u = ch & first_byte_mask[len];
			c = len;
			do {
				ch = s[i++];
				if (len_tab[ch] != 0)
					return 0;
				u = (u << 6) | (ch & 0x3f);
			} while (--c);

			if (u < min_val[len] || u > max_val[len])
				return 0;
		}
	}
	return 1;
}

int u_strlen(const char *str)
{
	const unsigned char *s = (const unsigned char *)str;
	int len = 0;

	while (*s) {
		int l = len_tab[*s];

		if (unlikely(l > 1)) {
			/* next l - 1 bytes must be 0x10xxxxxx */
			int c = 1;
			do {
				if (len_tab[s[c]] != 0) {
					/* invalid sequence */
					goto single_char;
				}
				c++;
			} while (c < l);

			/* valid sequence */
			s += l;
			len++;
			continue;
		}
single_char:
		/* l is -1, 0 or 1
		 * invalid chars counted as single characters */
		s++;
		len++;
	}
	return len;
}

int u_char_width(uchar u)
{
	if (u < 0x1100U)
		goto narrow;

	/* Hangul Jamo init. consonants */ 
	if (u <= 0x115fU)
		goto wide;

	/* angle brackets */
	if (u == 0x2329U || u == 0x232aU)
		goto wide;

	if (u < 0x2e80U)
		goto narrow;
	/* CJK ... Yi */
	if (u < 0x302aU)
		goto wide;
	if (u <= 0x302fU)
		goto narrow;
	if (u == 0x303fU)
		goto narrow;
	if (u == 0x3099U)
		goto narrow;
	if (u == 0x309aU)
		goto narrow;
	/* CJK ... Yi */
	if (u <= 0xa4cfU)
		goto wide;

	/* Hangul Syllables */
	if (u >= 0xac00U && u <= 0xd7a3U)
		goto wide;

	/* CJK Compatibility Ideographs */
	if (u >= 0xf900U && u <= 0xfaffU)
		goto wide;

	/* CJK Compatibility Forms */
	if (u >= 0xfe30U && u <= 0xfe6fU)
		goto wide;

	/* Fullwidth Forms */
	if (u >= 0xff00U && u <= 0xff60U)
		goto wide;

	/* Fullwidth Forms */
	if (u >= 0xffe0U && u <= 0xffe6U)
		goto wide;

	/* CJK extra stuff */
	if (u >= 0x20000U && u <= 0x2fffdU)
		goto wide;

	/* ? */
	if (u >= 0x30000U && u <= 0x3fffdU)
		goto wide;

	/* invalid bytes in unicode stream are rendered "<ff>" */
	if (u & U_INVALID_MASK)
		return 4;
narrow:
	return 1;
wide:
	return 2;
}

int u_str_width(const char *str)
{
	int idx = 0, w = 0;

	while (str[idx]) {
		uchar u;

		u_get_char(str, &idx, &u);
		w += u_char_width(u);
	}
	return w;
}

int u_str_nwidth(const char *str, int len)
{
	int idx = 0;
	int w = 0;
	uchar u;

	while (len > 0) {
		u_get_char(str, &idx, &u);
		if (u == 0)
			break;
		w += u_char_width(u);
		len--;
	}
	return w;
}

void u_prev_char_pos(const char *str, int *idx)
{
	const unsigned char *s = (const unsigned char *)str;
	int c, len, i = *idx;
	uchar ch;

	ch = s[--i];
	len = len_tab[ch];
	if (len != 0) {
		/* start of byte sequence or invelid uchar */
		goto one;
	}

	c = 1;
	while (1) {
		if (i == 0) {
			/* first byte of the sequence is missing */
			break;
		}

		ch = s[--i];
		len = len_tab[ch];
		c++;

		if (len == 0) {
			if (c < 4)
				continue;

			/* too long sequence */
			break;
		}
		if (len != c) {
			/* incorrect length */
			break;
		}

		/* ok */
		*idx = i;
		return;
	}
one:
	*idx = *idx - 1;
	return;
}

void u_get_char(const char *str, int *idx, uchar *uch)
{
	const unsigned char *s = (const unsigned char *)str;
	int len, i = *idx;
	uchar ch, u;

	ch = s[i++];
	len = len_tab[ch];
	if (unlikely(len < 1))
		goto invalid;

	len--;
	u = ch & first_byte_mask[len];
	while (len > 0) {
		ch = s[i++];
		if (unlikely(len_tab[ch] != 0))
			goto invalid;
		u = (u << 6) | (ch & 0x3f);
		len--;
	}
	*idx = i;
	*uch = u;
	return;
invalid:
	i = *idx;
	u = s[i++];
	*uch = u | U_INVALID_MASK;
	*idx = i;
}

void u_set_char_raw(char *str, int *idx, uchar uch)
{
	int i = *idx;

	if (uch <= 0x0000007fU) {
		str[i++] = uch;
		*idx = i;
	} else if (uch <= 0x000007ffU) {
		str[i + 1] = (uch & 63) | 0x80; uch >>= 6;
		str[i + 0] = uch | 0x000000c0U;
		i += 2;
		*idx = i;
	} else if (uch <= 0x0000ffffU) {
		str[i + 2] = (uch & 63) | 0x80; uch >>= 6;
		str[i + 1] = (uch & 63) | 0x80; uch >>= 6;
		str[i + 0] = uch | 0x000000e0U;
		i += 3;
		*idx = i;
	} else if (uch <= 0x0010ffffU) {
		str[i + 3] = (uch & 63) | 0x80; uch >>= 6;
		str[i + 2] = (uch & 63) | 0x80; uch >>= 6;
		str[i + 1] = (uch & 63) | 0x80; uch >>= 6;
		str[i + 0] = uch | 0x000000f0U;
		i += 4;
		*idx = i;
	} else {
		/* must be an invalid uchar */
		str[i++] = uch & 0xff;
		*idx = i;
	}
}

/*
 * Printing functions, these lose information
 */

void u_set_char(char *str, int *idx, uchar uch)
{
	int i = *idx;

	if (uch <= 0x0000007fU) {
		str[i++] = uch;
		*idx = i;
	} else if (uch <= 0x000007ffU) {
		str[i + 1] = (uch & 63) | 0x80; uch >>= 6;
		str[i + 0] = uch | 0x000000c0U;
		i += 2;
		*idx = i;
	} else if (uch <= 0x0000ffffU) {
		str[i + 2] = (uch & 63) | 0x80; uch >>= 6;
		str[i + 1] = (uch & 63) | 0x80; uch >>= 6;
		str[i + 0] = uch | 0x000000e0U;
		i += 3;
		*idx = i;
	} else if (uch <= 0x0010ffffU) {
		str[i + 3] = (uch & 63) | 0x80; uch >>= 6;
		str[i + 2] = (uch & 63) | 0x80; uch >>= 6;
		str[i + 1] = (uch & 63) | 0x80; uch >>= 6;
		str[i + 0] = uch | 0x000000f0U;
		i += 4;
		*idx = i;
	} else {
		/* must be an invalid uchar */
		str[i++] = '<';
		str[i++] = hex_tab[(uch >> 4) & 0xf];
		str[i++] = hex_tab[uch & 0xf];
		str[i++] = '>';
		*idx = i;
	}
}

int u_copy_chars(char *dst, const char *src, int *width)
{
	int w = *width;
	int si = 0, di = 0;
	int cw;
	uchar u;

	while (w > 0) {
		u_get_char(src, &si, &u);
		if (u == 0)
			break;

		cw = u_char_width(u);
		w -= cw;

		if (unlikely(w < 0)) {
			if (cw == 2)
				dst[di++] = ' ';
			if (cw == 4) {
				dst[di++] = '<';
				if (w >= -2)
					dst[di++] = hex_tab[(u >> 4) & 0xf];
				if (w >= -1)
					dst[di++] = hex_tab[u & 0xf];
			}
			w = 0;
			break;
		}
		u_set_char(dst, &di, u);
	}
	*width -= w;
	return di;
}

int u_skip_chars(const char *str, int *width)
{
	int w = *width;
	int idx = 0;

	while (w > 0) {
		uchar u;

		u_get_char(str, &idx, &u);
		w -= u_char_width(u);
	}
	/* add 1..3 if skipped 'too much' (the last char was double width or invalid (<xx>)) */
	*width -= w;
	return idx;
}

/*
 * Comparison functions
 */

static inline int chcasecmp(int a, int b)
{
	return towupper(a) - towupper(b);
}

int u_strcasecmp(const char *a, const char *b)
{
	int ai = 0;
	int bi = 0;
	int res;

	do {
		uchar au, bu;

		u_get_char(a, &ai, &au);
		u_get_char(b, &bi, &bu);
		res = chcasecmp(au, bu);
		if (res)
			break;
		if (au == 0) {
			/* bu is 0 too */
			break;
		}
	} while (1);
	return res;
}

int u_strncasecmp(const char *a, const char *b, int len)
{
	int ai = 0;
	int bi = 0;

	while (len > 0) {
		uchar au, bu;
		int res;

		u_get_char(a, &ai, &au);
		u_get_char(b, &bi, &bu);
		res = chcasecmp(au, bu);
		if (res)
			return res;
		if (au == 0) {
			/* bu is 0 too */
			return 0;
		}
		len--;
	}
	return 0;
}

char *u_strcasestr(const char *haystack, const char *needle)
{
	/* strlen is faster and works here */
	int haystack_len = strlen(haystack);
	int needle_len = u_strlen(needle);

	do {
		uchar u;
		int idx;

		if (haystack_len < needle_len)
			return NULL;
		if (u_strncasecmp(needle, haystack, needle_len) == 0)
			return (char *)haystack;

		/* skip one char */
		idx = 0;
		u_get_char(haystack, &idx, &u);
		haystack += idx;
		haystack_len -= idx;
	} while (1);
}
