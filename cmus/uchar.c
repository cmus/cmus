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

static int u_get_char_bytes(const char *str)
{
	int i = 0;
	int bit = 7;
	int mask = (1 << 7);
	int c, count;
	uchar ch;

	ch = (unsigned char)str[i++];
	while (bit > 0 && ch & mask) {
		mask >>= 1;
		bit--;
	}

	if (bit == 7)
		return 1;
	count = 6 - bit;
	if (count == 0 || count > 3)
		return -1;
	for (c = 0; c < count; c++) {
		if (u_is_first_byte(str[i++]))
			return -1;
	}
	return count + 1;
}

int u_is_valid(const char *str)
{
	while (*str) {
		int skip = u_get_char_bytes(str);

		if (skip == -1)
			return 0;
		str += skip;
	}
	return 1;
}

int u_strlen(const char *str)
{
	int i = 0;
	int len = 0;

	while (str[i]) {
		unsigned char ch;

		ch = str[i++];
		if (ch & (1 << 7)) {
			do {
				ch = str[i++];
			} while (!u_is_first_byte(ch));
			i--;
		}
		len++;
	}
	return len;
}

void u_get_char(const char *str, int *idx, uchar *uch)
{
	int i = *idx;
	int bit = 7;
	uchar u, ch;
	int mask = (1 << 7);

	ch = (unsigned char)str[i++];
	while (bit > 0 && ch & mask) {
		mask >>= 1;
		bit--;
	}
	if (bit == 7) {
		/* ascii */
		u = ch;
		*uch = u;
		*idx = i;
	} else {
		int count;

		u = ch & ((1 << bit) - 1);
		count = 6 - bit;
		while (count) {
			ch = (unsigned char)str[i++];
			u = (u << 6) | (ch & 63);
			count--;
		}
		*uch = u;
		*idx = i;
	}
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

int u_copy_chars(char *dst, const char *src, int len)
{
	unsigned char ch = src[0];
	int i = 0;

	while (len) {
		if (ch == 0)
			break;
		do {
			dst[i++] = ch;
			ch = src[i];
		} while (!u_is_first_byte(ch));
		len--;
	}
	return i;
}

int u_skip_chars(const char *str, int len)
{
	int i = 0;

	while (len) {
		do {
			i++;
		} while (!u_is_first_byte(str[i]));
		len--;
	}
	return i;
}

// FIXME
#include <ctype.h>
static inline int chcasecmp(char a, char b)
{
	return toupper(a) - toupper(b);
}

char *u_strcasestr(const char *text, const char *part)
{
	int i, j, save;

	i = 0;
	do {
		save = i;
		j = 0;
		while (chcasecmp(part[j], text[i]) == 0) {
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
