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
#include "expr.h"
#include "glob.h"
#include "utils.h"
#include "options.h"
#include "uchar.h"
#include "xmalloc.h"
#include "debug.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

static int width;
static int align_left;
static int pad;
static bool width_is_exact;

static GBUF(cond_buffer);
static GBUF(l_str);
static GBUF(m_str);
static GBUF(r_str);
static struct fp_len str_len = {0, 0, 0};
static int *len = &str_len.llen;
static struct gbuf* str = &l_str;

static void stack_print(char *stack, int stack_len)
{
	int i = 0;

	gbuf_grow(str, width ? width : stack_len);
	char* buf = str->buffer + str->len;

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
	gbuf_used(str, i);
	*len += i;
}

static void print_num(int num)
{
	char stack[20];
	int i, p;

	if (num < 0) {
		if (width == 0)
			width = 1;
		for (i = 0; i < width; i++)
			gbuf_add_ch(str, '?');
		*len += width;
		return;
	}
	p = 0;
	do {
		stack[p++] = num % 10 + '0';
		num /= 10;
	} while (num);

	stack_print(stack, p);
}

#define DBL_MAX_LEN (20)

static int format_double(char *buf, int buflen, double num)
{
	int l = snprintf(buf, buflen, "%f", num);
	/* skip trailing zeros */
	while (l > 0 && buf[l-1] == '0')
		l--;
	return l;
}

static void print_double(double num)
{
	char stack[DBL_MAX_LEN], b[DBL_MAX_LEN];
	int i, p = 0;

	i = format_double(b, DBL_MAX_LEN, num) - 1;
	while (i >= 0) {
		stack[p++] = b[i];
		i--;
	}
	stack_print(stack, p);
}

/* print '{,-}{h:,}mm:ss' */
static void print_time(int t)
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
	if (m / 10 || h || time_show_leading_zero)
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

	stack_print(stack, p);
}

static void print_str(const char *src)
{
	int str_width = u_str_width(src);

	if (width && width_is_exact) {
		*len += width;
		if (align_left) {
			gbuf_add_ustr(str, src, &width);
			gbuf_set(str, ' ', width);
		} else {
			int s = 0;
			int ws_len = width - str_width;
			if (ws_len < 0) {
				int skip = -ws_len;
				int clipped_mark_len = min_u(u_str_width(clipped_text_internal), width);
				skip += clipped_mark_len;
				gbuf_add_ustr(str, clipped_text_internal, &clipped_mark_len);
				s = u_skip_chars(src, &skip, true);
				/* pad if a wide character caused us to skip too much */
				ws_len = -skip;

			}
			gbuf_set(str, ' ', ws_len);
			gbuf_add_ustr(str, src + s, &width);
		}
	} else {
		if (!width)
			width = str_width;
		*len += width;
		gbuf_add_ustr(str, src, &width);
		*len -= width;
	}
}

static inline int strnequal(const char *a, const char *b, size_t b_len)
{
	return a && (strlen(a) == b_len) && (memcmp(a, b, b_len) == 0);
}

static const struct format_option *find_fopt(const struct format_option *fopts, const char *key)
{
	const struct format_option *fo;
	char ch = strlen(key) == 1 ? *key : 0;
	for (fo = fopts; fo->type != 0; fo++) {
		if ((ch != 0 && fo->ch == ch) || strnequal(fo->str, key, strlen(key))) {
			return fo;
		}
	}
	return NULL;
}

static const char *str_val(const char *key, const struct format_option *fopts, char *buf)
{
	const struct format_option *fo;
	const struct cmus_opt *opt;
	const char *val = NULL;

	fo = find_fopt(fopts, key);
	if (fo && !fo->empty) {
		if (fo->type == FO_STR)
			val = fo->fo_str;
	} else {
		opt = option_find_silent(key);
		if (opt) {
			opt->get(opt->data, buf, OPTION_MAX_SIZE);
			val = buf;
		}
	}
	return val;
}

static int int_val(const char *key, const struct format_option *fopts, char *buf)
{
	const struct format_option *fo;
	int val = -1;

	fo = find_fopt(fopts, key);
	if (fo && !fo->empty) {
		if (fo->type == FO_INT)
			val = fo->fo_int;
	}
	return val;
}

static int format_eval_cond(struct expr* expr, const struct format_option *fopts)
{
	if (!expr)
		return -1;
	enum expr_type type = expr->type;
	const char *key;
	const struct format_option *fo;
	const struct cmus_opt *opt;
	char buf[OPTION_MAX_SIZE];

	if (expr->left) {
		int left = format_eval_cond(expr->left, fopts);

		if (type == EXPR_AND)
			return left && format_eval_cond(expr->right, fopts);
		if (type == EXPR_OR)
			return left || format_eval_cond(expr->right, fopts);
		/* EXPR_NOT */
		return !left;
	}

	key = expr->key;
	if (type == EXPR_STR) {
		const char *val = str_val(key, fopts, buf);
		int res;

		if (!val)
			val = "";
		res = glob_match(&expr->estr.glob_head, val);
		if (expr->estr.op == SOP_EQ)
			return res;
		return !res;
	} else if (type == EXPR_INT) {
		int val = int_val(key, fopts, buf);
		int res = val - expr->eint.val;
		if (val == -1 || expr->eint.val == -1) {
			switch (expr->eid.op) {
			case KOP_EQ:
				return res == 0;
			case KOP_NE:
				return res != 0;
			default:
				return 0;
			}
		}
		return expr_op_to_bool(res, expr->eint.op);
	} else if (type == EXPR_ID) {
		int a = 0, b = 0;
		const char *sa, *sb;
		int res = 0;
		if ((sa = str_val(key, fopts, buf)) && (sb = str_val(expr->eid.key, fopts, buf))) {
			res = strcmp(sa, sb);
			return expr_op_to_bool(res, expr->eid.op);
		} else {
			a = int_val(key, fopts, buf);
			b = int_val(expr->eid.key, fopts, buf);
			res = a - b;
			if (a == -1 || b == -1) {
				switch (expr->eid.op) {
				case KOP_EQ:
					return res == 0;
				case KOP_NE:
					return res != 0;
				default:
					return 0;
				}
			}
			return expr_op_to_bool(res, expr->eid.op);
		}
		return res;
	}
	if (strcmp(key, "stream") == 0) {
		fo = find_fopt(fopts, "filename");
		return fo && is_http_url(fo->fo_str);
	}
	fo = find_fopt(fopts, key);
	if (fo)
		return !fo->empty;
	opt = option_find_silent(key);
	if (opt) {
		opt->get(opt->data, buf, OPTION_MAX_SIZE);
		if (strcmp(buf, "false") != 0 && strlen(buf) != 0)
			return 1;
	}
	return 0;
}

static struct expr *format_parse_cond(const char* format, int size)
{
	gbuf_clear(&cond_buffer);
	gbuf_add_bytes(&cond_buffer, format, size);
	return expr_parse_i(cond_buffer.buffer, "condition contains control characters", 0);
}

static uchar format_skip_cond_expr(const char *format, int *s)
{
	uchar r = 0;
	while (format[*s]) {
		uchar u = u_get_char(format, s);
		if (u == '}' || u == '?') {
			return u;
		}
		if (u != '%') {
			continue;
		}
		u = u_get_char(format, s);
		if (u == '%' || u == '?' || u == '!' || u == '=') {
			continue;
		}
		if (u == '-') {
			u = u_get_char(format, s);
		}
		if (u == '.')
			u = u_get_char(format, s);
		while (isdigit(u)) {
			u = u_get_char(format, s);
		}
		if (u == '{') {
			unsigned level = 1;
			while (level) {
				u = u_get_char(format, s);
				if (u == 0)
					return 0;
				if (u == '}')
					--level;
				if (u != '%')
					continue;
				u = u_get_char(format, s);
				if (u == '%' || u == '?' || u == '!' || u == '=')
					continue;
				if (u == '-')
					u = u_get_char(format, s);
				if (u == '.')
					u = u_get_char(format, s);
				while (isdigit(u))
					u = u_get_char(format, s);
				if (u == 0)
					return 0;
				if (u == '{')
					++level;
			}
		}
	}
	return r;
}

static int format_read_cond(const char *format, int *s, int *a, int *b, int *end)
{
	uchar t = format_skip_cond_expr(format, s);
	if (t != '?')
		return 1;
	*a = *s - 1;
	t = format_skip_cond_expr(format, s);
	if (t == 0)
		return 1;
	if (t == '?') {
		*b = *s - 1;
		t = format_skip_cond_expr(format, s);
		if (t != '}')
			return 1;
	}
	*end = *s - 1;
	return 0;
}

static void format_parse(int str_width, const char *format, const struct format_option *fopts, int f_size);

static void format_parse_if(int str_width, const char *format, const struct format_option *fopts, int *s)
{
	int cond_pos = *s, then_pos = -1, else_pos = -1, end_pos = -1, cond_res = -1;
	BUG_ON(format_read_cond(format, s, &then_pos, &else_pos, &end_pos) != 0);

	struct expr *cond = format_parse_cond(format + cond_pos, then_pos - cond_pos);
	cond_res = format_eval_cond(cond, fopts);
	if (cond)
		expr_free(cond);

	BUG_ON(cond_res < 0);
	if (cond_res) {
		format_parse(str_width, format + then_pos + 1, fopts,
				(else_pos > 0 ? else_pos : end_pos) - then_pos - 1);
	} else if (else_pos > 0) {
		format_parse(str_width, format + else_pos + 1, fopts, end_pos - else_pos - 1);
	}

	*s = end_pos + 1;
}

static void format_parse(int str_width, const char *format, const struct format_option *fopts, int f_size)
{
	int s = 0;

	while (s < f_size) {
		const struct format_option *fo;
		int long_len = 0;
		const char *long_begin = NULL;
		uchar u;

		u = u_get_char(format, &s);
		if (u != '%') {
			gbuf_add_uchar(str, u);
			(*len) += u_char_width(u);
			continue;
		}
		u = u_get_char(format, &s);
		if (u == '%' || u == '?') {
			gbuf_add_ch(str, u);
			++(*len);
			continue;
		}
		if (u == '!') {
			/* middle (priority) text starts */
			str = &m_str;
			len = &str_len.mlen;
			continue;
		}
		if (u == '=') {
			/* right aligned text starts */
			str = &r_str;
			len = &str_len.rlen;
			continue;
		}
		align_left = 0;
		if (u == '-') {
			align_left = 1;
			u = u_get_char(format, &s);
		}
		width_is_exact = true;
		if (u == '.') {
			width_is_exact = false;
			u = u_get_char(format, &s);
		}
		pad = ' ';
		if (u == '0') {
			pad = '0';
			u = u_get_char(format, &s);
		}
		width = 0;
		while (isdigit(u)) {
			/* minimum length of this field */
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
			if (*long_begin == '?') {
				++s;
				format_parse_if(str_width, format, fopts, &s);
				BUG_ON(s > f_size);
				continue;
			}
			while (1) {
				BUG_ON(s >= f_size);
				u = u_get_char(format, &s);
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

				if (fo->empty) {
					gbuf_set(str, ' ', width);
					*len += width;
				} else if (type == FO_STR) {
					print_str(fo->fo_str);
				} else if (type == FO_INT) {
					print_num(fo->fo_int);
				} else if (type == FO_TIME) {
					print_time(fo->fo_time);
				} else if (type == FO_DOUBLE) {
					print_double(fo->fo_double);
				}
				break;
			}
		}
	}
}

static void format_read(int str_width, const char *format, const struct format_option *fopts)
{
	gbuf_clear(&l_str);
	gbuf_clear(&m_str);
	gbuf_clear(&r_str);
	str_len.llen = 0;
	str_len.mlen = 0;
	str_len.rlen = 0;
	str = &l_str;
	len = &str_len.llen;
	format_parse(str_width, format, fopts, strlen(format));
}

static void format_write(struct gbuf *buf, int str_width)
{
	if (str_width == 0)
		str_width = str_len.llen + str_len.mlen + str_len.rlen + (str_len.rlen > 0);

	/* NOTE: any invalid UTF-8 bytes have already been converted to <xx>
	 *       (ASCII) where x is hex digit
	 */

	if (str_len.llen + str_len.mlen + str_len.rlen <= str_width) {
		/* all fit */
		int ws_len = str_width - (str_len.llen + str_len.mlen + str_len.rlen);

		gbuf_add_bytes(buf, l_str.buffer, l_str.len);
		gbuf_add_bytes(buf, m_str.buffer, m_str.len);
		gbuf_set(buf, ' ', ws_len);
		gbuf_add_bytes(buf, r_str.buffer, r_str.len);
	} else {
		/* keep first character since it's almost always padding */
		int clipped_mark_len = min_u(u_str_width(clipped_text_internal) + 1, str_width);
		int r_space = str_width - clipped_mark_len;
		int r_width = min_i(r_space, str_len.rlen);
		int m_space = r_space - r_width;
		int m_width = min_i(m_space, str_len.mlen);
		int l_space = m_space - m_width;
		int l_width = l_space + clipped_mark_len;
		int r_idx = 0, ws_pad = 0;

		gbuf_add_ustr(buf, l_str.buffer, &l_width);
		ws_pad += l_width;
		gbuf_add_ustr(buf, m_str.buffer, &m_width);
		ws_pad += m_width;

		int r_skip = str_len.rlen - r_width;
		r_idx = u_skip_chars(r_str.buffer, &r_skip, true);
		ws_pad += -r_skip;
		gbuf_set(buf, ' ', ws_pad);
		gbuf_add_bytes(buf, r_str.buffer + r_idx, r_str.len - r_idx);
	}
}

struct fp_len format_print(struct gbuf *buf, int str_width, const char *format, const struct format_option *fopts)
{
	format_read(str_width, format, fopts);

#if DEBUG > 1
	if (str_len.llen > 0) {
		int ul = u_str_width(l_str.buffer);
		if (ul != str_len.llen)
			d_print("L %d != %d: size=%zu '%s'\n", ul, str_len.llen, l_str.len, l_str.buffer);
	}

	if (str_len.rlen > 0) {
		int ul = u_str_width(r_str.buffer);
		if (ul != str_len.rlen)
			d_print("R %d != %d: size=%zu '%s'\n", ul, str_len.rlen, r_str.len, r_str.buffer);
	}
#endif

	format_write(buf, str_width);
	return str_len;
}

static int format_valid_sub(const char *format, const struct format_option *fopts, int f_size);

static int format_valid_if(const char *format, const struct format_option *fopts, int *s)
{
	int cond_pos = *s, then_pos = -1, else_pos = -1, end_pos = -1;
	if (format_read_cond(format, s, &then_pos, &else_pos, &end_pos) != 0)
		return 0;

	struct expr *cond = format_parse_cond(format + cond_pos, then_pos - cond_pos);
	if (cond == NULL)
		return 0;
	expr_free(cond);

	if (!format_valid_sub(format + then_pos + 1, fopts,
				(else_pos > 0 ? else_pos : end_pos) - then_pos - 1))
		return 0;
	if (else_pos > 0)
		if (!format_valid_sub(format + else_pos + 1, fopts, end_pos - else_pos - 1))
			return 0;

	*s = end_pos + 1;
	return 1;
}

static int format_valid_sub(const char *format, const struct format_option *fopts, int f_size)
{
	int s = 0;

	while (s < f_size) {
		uchar u;

		u = u_get_char(format, &s);
		if (u == '%') {
			int pad_zero = 0, long_len = 0;
			const struct format_option *fo;
			const char *long_begin = NULL;

			u = u_get_char(format, &s);
			if (u == '%' || u == '?' || u == '!' || u == '=')
				continue;
			if (u == '-')
				u = u_get_char(format, &s);
			if (u == '.')
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
				if (*long_begin == '?') {
					++s;
					if (!format_valid_if(format, fopts, &s))
						return 0;
					if (s > f_size)
						return 0;
					continue;
				}

				while (1) {
					if (s >= f_size)
						return 0;
					u = u_get_char(format, &s);
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

int format_valid(const char *format, const struct format_option *fopts)
{
	return format_valid_sub(format, fopts, strlen(format));
}
