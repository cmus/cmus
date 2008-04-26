#include "format_print.h"
#include "uchar.h"
#include "xmalloc.h"
#include "debug.h"

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
					u_get_char(str, &s, &u);
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
			u_get_char(str, &s, &u);
			if (u == 0)
				break;
			u_set_char(buf, &d, u);
		}
	}
	*idx = d;
}

static void print(char *str, const char *format, const struct format_option *fopts)
{
	/* format and str indices */
	int s = 0, d = 0;

	while (format[s]) {
		const struct format_option *fo;
		uchar u;

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
		width = 0;
		while (isdigit(u)) {
			width *= 10;
			width += u - '0';
			u_get_char(format, &s, &u);
		}
		for (fo = fopts; fo->ch; fo++) {
			if (fo->ch == u) {
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
		int nlen;
		uchar u;

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
		for (fo = fopts; ; fo++) {
			BUG_ON(fo->ch == 0);
			if (fo->ch == u) {
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

/* FIXME: compare with struct format_option[] */
int format_valid(const char *format)
{
	int s = 0;

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
			case 'g':
			case 'f':
			case 'F':
				if (pad_zero)
					return 0;
				break;
			case 'D':
			case 'n':
			case 'y':
				break;
			default:
				return 0;
			}
		}
	}
	return 1;
}
