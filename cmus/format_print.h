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

#ifndef _FORMAT_PRINT_H
#define _FORMAT_PRINT_H

struct format_option {
	union {
		/* NULL is treated like "" */
		const char *fo_str;
		int fo_int;
		/* in seconds. fo_time >= 3600 ? "h:mm:ss" : "mm:ss"
		 * can be negative */
/* 		unsigned int fo_time; */
		int fo_time;
	} u;
	/* set to 1 if you want to disable printing */
	unsigned int empty : 1;
	enum { FO_STR, FO_INT, FO_TIME } type;
	char ch;
};

extern int format_print(char *str, int str_size, int width, const char *format,
		struct format_option *fopts);
extern int format_valid(const char *format);

#endif
