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
 * 0xxxxxxx
 * 110xxxxx 10xxxxxx
 * 1110xxxx 10xxxxxx 10xxxxxx
 * 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
 */

static inline int u_is_first_byte(unsigned char byte)
{
	return byte >> 6 != 2;
}

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

extern int u_strlen(const char *str);
extern int u_get_char(const char *str, int *idx, uchar *uch);
extern int u_set_char(char *str, int *idx, uchar uch);
extern void u_copy_char(char *dst, int *didx, const char *src, int *sidx);
extern void u_strncpy(char *dst, const char *src, int len);
extern void u_substrcpy(char *dst, int didx, const char *src, int sidx, int len);
extern int u_get_idx(const char *str, int pos);
extern char *u_strcasestr(const char *text, const char *part);

#endif
