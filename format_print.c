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

#include "format_print.h"
#include "uchar.h"
#include "xmalloc.h"
#include "debug.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

static int width;
static int align_left;
static int pad;

static int numlen(int num)
{
	int digits;

	if (num < 0)
		return 1; /* '?' */
	digits = 0;
	do {
		num /= 10;
		digits++;
	} while (num);
	return digits;
}

static int stack_print(char *buf, char *stack, int stack_len)
{
	int i = 0;

	if (width) {
		if (align_left) {
			while (i < width && stack_len)
				buf[i++] = stack[--stack_len];
			while (i < width)
				buf[i++] = pad;
		} else {
			int pad_len;

			if (stack_len > width)
				stack_len = width;
			pad_len = width - stack_len;
			while (i < pad_len)
				buf[i++] = pad;
			while (i < width)
				buf[i++] = stack[--stack_len];
		}
	} else {
		while (stack_len)
			buf[i++] = stack[--stack_len];
	}
	return i;
}

static int print_num(char *buf, int num)
{
	char stack[20];
	int i, p;

	if (num < 0) {
		if (width == 0)
			width = 1;
		for (i = 0; i < width; i++)
			buf[i] = '?';
		return width;
	}
	p = 0;
	do {
		stack[p++] = num % 10 + '0';
		num /= 10;
	} while (num);

	return stack_print(buf, stack, p);
}

#define DBL_MAX_LEN (20)

static int format_double(char *buf, int buflen, double num)
{
	int len = snprintf(buf, buflen, "%f", num);
	/* skip trailing zeros */
	while (len > 0 && buf[len-1] == '0')
		len--;
	return len;
}

static int double_len(double num)
{
	char buf[DBL_MAX_LEN];
	return format_double(buf, DBL_MAX_LEN, num);
}

static int print_double(char *buf, double num)
{
	char stack[DBL_MAX_LEN], b[DBL_MAX_LEN];
	int i, p = 0;

	i = format_double(b, DBL_MAX_LEN, num) - 1;
	while (i >= 0) {
		stack[p++] = b[i];
		i--;
	}
	return stack_print(buf, stack, p);
}

/* print '{,-}{h:,}mm:ss' */
static int print_time(char *buf, int t)
{
	int h, m, s;
	char stack[32];
	int neg = 0;
	int p = 0;

	if (t < 0) {
		neg = 1;
		t *= -1;
	}
	h = t / 3600;
	t = t % 3600;
	m = t / 60;
	s = t % 60;

	/* put all chars to stack in reverse order ;) */
	stack[p++] = s % 10 + '0';
	stack[p++] = s / 10 + '0';
	stack[p++] = ':';
	stack[p++] = m % 10 + '0';
	stack[p++] = m / 10 + '0';
	if (h) {
		stack[p++] = ':';
		do {
			stack[p++] = h % 10 + '0';
			h /= 10;
		} while (h);
	}
	if (neg)
		stack[p++] = '-';

	return stack_print(buf, stack, p);
}

static void print_str(char *buf, int *idx, const char *str)
{
	int d = *idx;

	if (width) {
		int ws_len;
		int i = 0;

		if (align_left) {
			i = width;
			d += u_copy_chars(buf + d, str, &i);

			ws_len = width - i;
			memset(buf + d, ' ', ws_len);
			d += ws_len;
		} else {
			int s = 0;

			ws_len = width - u_str_width(str);

			if (ws_len > 0) {
				memset(buf + d, ' ', ws_len);
				d += ws_len;
				i += ws_len;
			}

			if (ws_len < 0) {
				int w, c = -ws_len;
				uchar u;

				while (c > 0) {
					u = u_get_char(str, &s);
					w = u_char_width(u);
					c -= w;
				}
				if (c < 0) {
					/* gaah, skipped too much */
					if (u_char_width(u) == 2) {
						/* double-byte */
						buf[d++] = ' ';
					} else {
						/* <xx> */
						if (c == -3)
							buf[d++] = hex_tab[(u >> 4) & 0xf];
						if (c <= -2)
							buf[d++] = hex_tab[u & 0xf];
						buf[d++] = '>';
					}
				}
			}

			if (width - i > 0) {
				int w = width - i;

				d += u_copy_chars(buf + d, str + s, &w);
			}
		}
	} else {
		int s = 0;
		uchar u;

		while (1) {
			u = u_get_char(str, &s);
			if (u == 0)
				break;
			u_set_char(buf, &d, u);
		}
	}
	*idx = d;
}

static inline int strnequal(const char *a, const char *b, size_t b_len)
{
	return a && (strlen(a) == b_len) && (memcmp(a, b, b_len) == 0);
}

static void print(char *str, int str_width, const char *format, const struct format_option *fopts)
{
	/* format and str indices */
	int s = 0, d = 0;

	while (format[s]) {
		const struct format_option *fo;
		int long_len = 0;
		const char *long_begin = NULL;
		uchar u;

		u = u_get_char(format, &s);
		if (u != '%') {
			u_set_char(str, &d, u);
			continue;
		}
		u = u_get_char(format, &s);
		if (u == '%') {
			u_set_char(str, &d, u);
			continue;
		}

		if (u == '=') {
			break;
		}

		align_left = 0;
		if (u == '-') {
			align_left = 1;
			u = u_get_char(format, &s);
		}
		pad = ' ';
		if (u == '0') {
			pad = '0';
			u = u_get_char(format, &s);
		}
		width = 0;
		while (isdigit(u)) {
			width *= 10;
			width += u - '0';
			u = u_get_char(format, &s);
		}
		if (u == '%') {
			width = (width * str_width) / 100.0 + 0.5;
			u = u_get_char(format, &s);
		}
		if (u == '{') {
			long_begin = format + s;
			while (1) {
				u = u_get_char(format, &s);
				BUG_ON(u == 0);
				if (u == '}')
					break;
				long_len++;
			}
		}
		for (fo = fopts; fo->type; fo++) {
			if (long_len ? strnequal(fo->str, long_begin, long_len)
				     : (fo->ch == u)) {
				int type = fo->type;

				if (fo->empty) {
					memset(str + d, ' ', width);
					d += width;
				} else if (type == FO_STR) {
					print_str(str, &d, fo->fo_str);
				} else if (type == FO_INT) {
					d += print_num(str + d, fo->fo_int);
				} else if (type == FO_TIME) {
					d += print_time(str + d, fo->fo_time);
				} else if (type == FO_DOUBLE) {
					d += print_double(str + d, fo->fo_double);
				}
				break;
			}
		}
	}
	str[d] = 0;
}

static char *l_str = NULL;
static char *r_str = NULL;
/* sizes in bytes. not counting the terminating 0! */
static int l_str_size = -1;
static int r_str_size = -1;

int format_print(char *str, int str_width, const char *format, const struct format_option *fopts)
{
	/* lengths of left and right aligned texts */
	int llen = 0;
	int rlen = 0;
	int *len = &llen;
	int lsize, rsize;
	int eq_pos = -1;
	int s = 0;

	while (format[s]) {
		const struct format_option *fo;
		int nlen, long_len = 0;
		const char *long_begin = NULL;
		uchar u;

		u = u_get_char(format, &s);
		if (u != '%') {
			(*len) += u_char_width(u);
			continue;
		}
		u = u_get_char(format, &s);
		if (u == '%') {
			(*len)++;
			continue;
		}
		if (u == '=') {
			/* right aligned text starts */
			len = &rlen;
			eq_pos = s - 1;
			continue;
		}
		if (u == '-')
			u = u_get_char(format, &s);
		nlen = 0;
		while (isdigit(u)) {
			/* minimum length of this field */
			nlen *= 10;
			nlen += u - '0';
			u = u_get_char(format, &s);
		}
		if (u == '%') {
			nlen = (nlen * str_width) / 100.0 + 0.5;
			u = u_get_char(format, &s);
		}
		if (u == '{') {
			long_begin = format + s;
			while (1) {
				u = u_get_char(format, &s);
				BUG_ON(u == 0);
				if (u == '}')
					break;
				long_len++;
			}
		}
		for (fo = fopts; ; fo++) {
			BUG_ON(fo->type == 0);
			if (long_len ? strnequal(fo->str, long_begin, long_len)
				     : (fo->ch == u)) {
				int type = fo->type;
				int l = 0;

				if (fo->empty) {
					/* nothing */
				} else if (type == FO_STR) {
					l = u_str_width(fo->fo_str);
				} else if (type == FO_INT) {
					l = numlen(fo->fo_int);
				} else if (type == FO_TIME) {
					int t = fo->fo_time;

					if (t < 0) {
						t *= -1;
						l++;
					}
					if (t >= 3600) {
						l += numlen(t / 3600) + 6;
					} else {
						l += 5;
					}
				} else if (type == FO_DOUBLE) {
					l = double_len(fo->fo_double);
				}
				if (nlen) {
					*len += nlen;
				} else {
					*len += l;
				}
				break;
			}
		}
	}

	/* max utf-8 char len is 4 */
	lsize = llen * 4;
	rsize = rlen * 4;

	if (l_str_size < lsize) {
		free(l_str);
		l_str_size = lsize;
		l_str = xnew(char, l_str_size + 1);
		l_str[l_str_size] = 0;
	}
	if (r_str_size < rsize) {
		free(r_str);
		r_str_size = rsize;
		r_str = xnew(char, r_str_size + 1);
		r_str[r_str_size] = 0;
	}
	l_str[0] = 0;
	r_str[0] = 0;

	if (lsize > 0) {
		print(l_str, str_width, format, fopts);
#if DEBUG > 1
		{
			int ul = u_str_width(l_str);
			if (ul != llen)
				d_print("L %d != %d: size=%d '%s'\n", ul, llen, lsize, l_str);
		}
#endif
	}
	if (rsize > 0) {
		print(r_str, str_width, format + eq_pos + 1, fopts);
#if DEBUG > 1
		{
			int ul = u_str_width(r_str);
			if (ul != rlen)
				d_print("R %d != %d: size=%d '%s'\n", ul, rlen, rsize, r_str);
		}
#endif
	}

	/* NOTE: any invalid UTF-8 bytes have already been converted to <xx>
	 *       (ASCII) where x is hex digit
	 */

	if (llen + rlen <= str_width) {
		/* both fit */
		int ws_len = str_width - llen - rlen;
		int pos = 0;

		/* I would use strcpy if it returned anything useful */
		while (l_str[pos]) {
			str[pos] = l_str[pos];
			pos++;
		}
		memset(str + pos, ' ', ws_len);
		strcpy(str + pos + ws_len, r_str);
	} else {
		int l_space = str_width - rlen;
		int pos = 0;
		int idx = 0;

		if (l_space > 0)
			pos = u_copy_chars(str, l_str, &l_space);
		if (l_space < 0) {
			int w = -l_space;

			idx = u_skip_chars(r_str, &w);
			if (w != -l_space)
				str[pos++] = ' ';
		}
		strcpy(str + pos, r_str + idx);
	}
	return 0;
}

int format_valid(const char *format, const struct format_option *fopts)
{
	int s = 0;

	while (format[s]) {
		uchar u;

		u = u_get_char(format, &s);
		if (u == '%') {
			int pad_zero = 0, long_len = 0;
			const struct format_option *fo;
			const char *long_begin = NULL;

			u = u_get_char(format, &s);
			if (u == '%' || u == '=')
				continue;
			if (u == '-')
				u = u_get_char(format, &s);
			if (u == '0') {
				pad_zero = 1;
				u = u_get_char(format, &s);
			}
			while (isdigit(u))
				u = u_get_char(format, &s);
			if (u == '%')
				u = u_get_char(format, &s);
			if (u == '{') {
				long_begin = format + s;
				while (1) {
					u = u_get_char(format, &s);
					if (!u)
						return 0;
					if (u == '}')
						break;
					long_len++;
				}
			}
			for (fo = fopts; fo->type; fo++) {
				if (long_len ? strnequal(fo->str, long_begin, long_len)
					     : (fo->ch == u)) {
					if (pad_zero && !fo->pad_zero)
						return 0;
					break;
				}
			}
			if (! fo->type)
				return 0;
		}
	}
	return 1;
}
