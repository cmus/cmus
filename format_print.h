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

#ifndef _FORMAT_PRINT_H
#define _FORMAT_PRINT_H

struct format_option {
	union {
		/* NULL is treated like "" */
		const char *fo_str;
		int fo_int;
		/* [h:]mm:ss. can be negative */
		int fo_time;
		double fo_double;
	};
	/* set to 1 if you want to disable printing */
	unsigned int empty : 1;
	/* set to 1 if zero padding is allowed */
	unsigned int pad_zero : 1;
	enum { FO_STR = 1, FO_INT, FO_TIME, FO_DOUBLE } type;
	char ch;
	const char *str;
};

/* gcc < 4.6 and icc < 12.0 can't properly initialize anonymous unions */
#if (defined(__GNUC__) && defined(__GNUC_MINOR__) && (__GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 6))) || \
	(defined(__INTEL_COMPILER) && __INTEL_COMPILER < 1200)
#define UNION_INIT(f, v) { .f = v }
#else
#define UNION_INIT(f, v) .f = v
#endif

#define DEF_FO_STR(c, s, z)    { UNION_INIT(fo_str,  NULL), .type = FO_STR,    .pad_zero = z, .ch = c, .str = s }
#define DEF_FO_INT(c, s, z)    { UNION_INIT(fo_int,  0),    .type = FO_INT,    .pad_zero = z, .ch = c, .str = s }
#define DEF_FO_TIME(c, s, z)   { UNION_INIT(fo_time, 0),    .type = FO_TIME,   .pad_zero = z, .ch = c, .str = s }
#define DEF_FO_DOUBLE(c, s, z) { UNION_INIT(fo_double, 0.), .type = FO_DOUBLE, .pad_zero = z, .ch = c, .str = s }
#define DEF_FO_END             { .type = 0 }

int format_print(char *str, int width, const char *format, const struct format_option *fopts);
int format_valid(const char *format, const struct format_option *fopts);

#endif
