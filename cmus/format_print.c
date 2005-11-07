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

#include <format_print.h>
#include <uchar.h>
#include <utils.h>
#include <xmalloc.h>
#include <debug.h>

#include <string.h>
#include <ctype.h>

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

static int stack_print(char *buf, char *stack, int stack_len, int width, int align_left, char pad)
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

static int print_num(char *buf, int num, int width, int align_left, char pad)
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

	return stack_print(buf, stack, p, width, align_left, pad);
}

/* print '{,-}{h:,}mm:ss' */
static int print_time(char *buf, int t, int width, int align_left, char pad)
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

	return stack_print(buf, stack, p, width, align_left, pad);
}

/*
 * buf:
 *     where to print
 * idx:
 *     index to @buf
 * str:
 *     what to print
 * width:
 *     field width. 0 if not set
 * aling_left:
 *     1 => aling left. 0 => align right
 */
static void print_str(char *buf, int *idx, const char *str, int width, int align_left)
{
	int s, d;

	s = 0;
	d = *idx;
	if (width) {
		int ws_len;
		int i = 0;

		if (align_left) {
			int c;

			i = width;
			c = u_copy_chars(buf + d, str + s, &i);
			s += c;
			d += c;

			ws_len = width - i;
			memset(buf + d, ' ', ws_len);
			d += ws_len;
			i += ws_len;
		} else {
			ws_len = width - u_str_width(str);

			if (ws_len > 0) {
				memset(buf + d, ' ', ws_len);
				d += ws_len;
				i += ws_len;
			}

			if (ws_len < 0) {
				int w = -ws_len;

				s += u_skip_chars(str, &w);
				if (w != -ws_len)
					buf[d++] = ' ';
			}

			if (width - i > 0) {
				int c, w = width - i;

				c = u_copy_chars(buf + d, str + s, &w);
				s += c;
				d += c;
			}
		}
	} else {
		while (str[s])
			buf[d++] = str[s++];
	}
	*idx = d;
}

static void print(char *str, const char *format, const struct format_option *fopts)
{
	/* format and str indices */
	int s = 0, d = 0;

	while (format[s]) {
		uchar u;
		int nlen, align_left, pad, j;

		u_get_char(format, &s, &u);
		if (u != '%') {
			u_set_char(str, &d, u);
			continue;
		}
		u_get_char(format, &s, &u);
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
			u_get_char(format, &s, &u);
		}
		pad = ' ';
		if (u == '0') {
			pad = '0';
			u_get_char(format, &s, &u);
		}
		nlen = 0;
		while (isdigit(u)) {
			nlen *= 10;
			nlen += u - '0';
			u_get_char(format, &s, &u);
		}
		j = 0;
		while (1) {
			if (fopts[j].ch == 0) {
				/* invalid user */
				break;
			}
			if (fopts[j].ch == u) {
				if (fopts[j].empty) {
					memset(str + d, ' ', nlen);
					d += nlen;
				} else if (fopts[j].type == FO_STR) {
					print_str(str, &d, fopts[j].fo_str, nlen, align_left);
				} else if (fopts[j].type == FO_INT) {
					d += print_num(str + d, fopts[j].fo_int, nlen, align_left, pad);
				} else if (fopts[j].type == FO_TIME) {
					d += print_time(str + d, fopts[j].fo_time, nlen, align_left, pad);
				}
				break;
			}
			j++;
		}
	}
	str[d] = 0;
}

static char *l_str = NULL;
static char *r_str = NULL;
/* sizes in bytes. not counting the terminating 0! */
static int l_str_size = -1;
static int r_str_size = -1;

int format_print(char *str, int width, const char *format, const struct format_option *fopts)
{
	/* lengths of left and right aligned texts */
	int llen = 0;
	int rlen = 0;
	int *len = &llen;
	int lsize, rsize;
	int eq_pos = -1;
	int s;

	s = 0;
	while (format[s]) {
		uchar u;
		int nlen, j;

		u_get_char(format, &s, &u);
		if (u != '%') {
			(*len) += u_char_width(u);
			continue;
		}
		u_get_char(format, &s, &u);
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
			u_get_char(format, &s, &u);
		nlen = 0;
		while (isdigit(u)) {
			/* minimum length of this field */
			nlen *= 10;
			nlen += u - '0';
			u_get_char(format, &s, &u);
		}
		j = 0;
		while (1) {
			BUG_ON(fopts[j].ch == 0);
			if (fopts[j].ch == u) {
				int l = 0;

				if (fopts[j].empty) {
					/* nothing */
				} else if (fopts[j].type == FO_STR) {
					l = u_str_width(fopts[j].fo_str);
				} else if (fopts[j].type == FO_INT) {
					l = numlen(fopts[j].fo_int);
				} else if (fopts[j].type == FO_TIME) {
					int t = fopts[j].fo_time;

					if (t < 0) {
						t *= -1;
						l++;
					}
					if (t >= 3600) {
						l += numlen(t / 3600) + 6;
					} else {
						l += 5;
					}
				} else {
					BUG("invalid format option\n");
				}
				if (nlen) {
					*len += nlen;
				} else {
					*len += l;
				}
				break;
			}
			j++;
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
		print(l_str, format, fopts);
#if DEBUG > 1
		{
			int ul = u_str_width(l_str);
			if (ul != llen)
				d_print("L %d != %d: size=%d '%s'\n", ul, llen, lsize, l_str);
		}
#endif
	}
	if (rsize > 0) {
		print(r_str, format + eq_pos + 1, fopts);
#if DEBUG > 1
		{
			int ul = u_str_width(r_str);
			if (ul != rlen)
				d_print("R %d != %d: size=%d '%s'\n", ul, rlen, rsize, r_str);
		}
#endif
	}

	if (llen + rlen <= width) {
		/* both fit */
		int ws_len = width - llen - rlen;
		int pos = 0;

		/* I would use strcpy if it returned anything useful */
		while (l_str[pos]) {
			str[pos] = l_str[pos];
			pos++;
		}
		memset(str + pos, ' ', ws_len);
		strcpy(str + pos + ws_len, r_str);
	} else {
		int l_space = width - rlen;
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

/* FIXME: compare with struct format_option[] */
int format_valid(const char *format)
{
	int s;

	s = 0;
	while (format[s]) {
		uchar u;

		u_get_char(format, &s, &u);
		if (u == '%') {
			int pad_zero = 0;

			u_get_char(format, &s, &u);
			if (u == '%' || u == '=')
				continue;
			if (u == '-')
				u_get_char(format, &s, &u);
			if (u == '0') {
				pad_zero = 1;
				u_get_char(format, &s, &u);
			}
			while (isdigit(u))
				u_get_char(format, &s, &u);
			switch (u) {
			case 'a':
			case 'l':
			case 't':
			case 'd':
			case 'f':
			case 'F':
				if (pad_zero)
					return 0;
				break;
			case 'D':
			case 'n':
			case 'y':
			case 'g':
				break;
			default:
				return 0;
			}
		}
	}
	return 1;
}
