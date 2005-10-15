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

#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include <ctype.h>

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
		s += len_tab[*s];
		len++;
	}
	return len;
}

int u_char_width(uchar u)
{
	if (u < 0x1100)
		goto narrow;

	/* Hangul Jamo init. consonants */ 
	if (u <= 0x115f)
		goto wide;

	/* angle brackets */
	if (u == 0x2329 || u == 0x232a)
		goto wide;

	if (u < 0x2e80)
		goto narrow;
	/* CJK ... Yi */
	if (u < 0x302a)
		goto wide;
	if (u <= 0x302f)
		goto narrow;
	if (u == 0x303f)
		goto narrow;
	if (u == 0x3099)
		goto narrow;
	if (u == 0x309a)
		goto narrow;
	/* CJK ... Yi */
	if (u <= 0xa4cf)
		goto wide;

	/* Hangul Syllables */
	if (u >= 0xac00 && u <= 0xd7a3)
		goto wide;

	/* CJK Compatibility Ideographs */
	if (u >= 0xf900 && u <= 0xfaff)
		goto wide;

	/* CJK Compatibility Forms */
	if (u >= 0xfe30 && u <= 0xfe6f)
		goto wide;

	/* Fullwidth Forms */
	if (u >= 0xff00 && u <= 0xff60)
		goto wide;

	/* Fullwidth Forms */
	if (u >= 0xffe0 && u <= 0xffe6)
		goto wide;

	/* CJK extra stuff */
	if (u >= 0x20000 && u <= 0x2fffd)
		goto wide;

	/* ? */
	if (u >= 0x30000 && u <= 0x3fffd)
		goto wide;
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

void u_get_char(const char *str, int *idx, uchar *uch)
{
	const unsigned char *s = (const unsigned char *)str;
	int len, i = *idx;
	unsigned char ch;
	uchar u;

	ch = s[i++];
	len = len_tab[ch] - 1;
	u = ch & first_byte_mask[len];
	while (len > 0) {
		ch = s[i++];
		u = (u << 6) | (ch & 0x3f);
		len--;
	}
	*idx = i;
	*uch = u;
}

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
	}
}

int u_copy_chars(char *dst, const char *src, int *width)
{
	int w = *width;
	int len = 0;
	int idx = 0;

	while (w > 0) {
		unsigned char ch = src[idx];
		uchar u;
		int i;

		if (ch == 0)
			break;

		dst[idx++] = ch;
		len = len_tab[ch];
		u = ch & first_byte_mask[len - 1];
		for (i = 1; i < len; i++) {
			ch = src[idx];
			u = (u << 6) | (ch & 0x3f);
			dst[idx++] = ch;
		}
		w -= u_char_width(u);
	}
	if (w < 0) {
		/* the last char was double width and didn't fit */
		idx -= len;
		dst[idx++] = ' ';
		w = 0;
	}
	*width -= w;
	return idx;
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
	/* add 1 if skipped 'too much' (the last char was double width) */
	*width -= w;
	return idx;
}

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

char *u_strcasestr(const char *text, const char *part)
{
	/* strlen is faster and works here */
	int text_len = strlen(text);
	int part_len = u_strlen(part);

	do {
		unsigned char ch;
		int clen;

		if (text_len < part_len)
			return NULL;
		if (u_strncasecmp(part, text, part_len) == 0)
			return (char *)text;

		ch = *text;
		clen = len_tab[ch];
		text += clen;
		text_len -= clen;
	} while (1);
}

static inline int ascii_chcasecmp(char a, char b)
{
	return toupper(a) - toupper(b);
}

static char *strcasestr(const char *text, const char *part)
{
	int i, j, save;

	i = 0;
	do {
		save = i;
		j = 0;
		while (ascii_chcasecmp(part[j], text[i]) == 0) {
			if (part[j] == 0)
				return (char *)text + i - j;
			i++;
			j++;
		}
		if (part[j] == 0)
			return (char *)text + i - j;
		if (text[i] == 0)
			return NULL;
		i = save + 1;
	} while (1);
}

char *u_strcasestr_filename(const char *text, const char *part)
{
	/* FIXME: implement and use slow & safe u_strcasestr_safe if locale is UTF-8 */
	if (u_is_valid(text))
		return u_strcasestr(text, part);
	return strcasestr(text, part);
}
