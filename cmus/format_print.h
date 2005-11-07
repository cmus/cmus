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

#ifndef _FORMAT_PRINT_H
#define _FORMAT_PRINT_H

struct format_option {
	union {
		/* NULL is treated like "" */
		const char *fo_str;
		int fo_int;
		/* [h:]mm:ss. can be negative */
		int fo_time;
	};
	/* set to 1 if you want to disable printing */
	unsigned int empty : 1;
	enum { FO_STR, FO_INT, FO_TIME } type;
	char ch;
};

#define DEF_FO_STR(ch)	{ { .fo_str  = NULL }, 0, FO_STR,  ch }
#define DEF_FO_INT(ch)	{ { .fo_int  = 0    }, 0, FO_INT,  ch }
#define DEF_FO_TIME(ch)	{ { .fo_time = 0    }, 0, FO_TIME, ch }
#define DEF_FO_END	{ { .fo_str  = NULL }, 0, 0,       0  }

int format_print(char *str, int width, const char *format, const struct format_option *fopts);
int format_valid(const char *format);

#endif
