/*
 * Copyright 2016 Various Authors
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

#include "gbuf.h"
#include "utils.h"
#include "debug.h"
#include "short_expr.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <ctype.h>

struct short_expr {
	const char *input;
	struct gbuf *output;
	const char *key;
	void (*err)(const char *, void *);
	void *opaque;
};

enum {
	SHORT_EXPR_STR,
	SHORT_EXPR_INT,
	SHORT_EXPR_BOOL,
};

static const char *short_expr_lookup(char c, int *type)
{
	static const struct {
		char short_key;
		const char *long_key;
		int type;
	} map[] = {
		{ 'A', "albumartist" , SHORT_EXPR_STR  },
		{ 'a', "artist"      , SHORT_EXPR_STR  },
		{ 'c', "comment"     , SHORT_EXPR_STR  },
		{ 'D', "discnumber"  , SHORT_EXPR_INT  },
		{ 'd', "duration"    , SHORT_EXPR_INT  },
		{ 'f', "filename"    , SHORT_EXPR_STR  },
		{ 'g', "genre"       , SHORT_EXPR_STR  },
		{ 'l', "album"       , SHORT_EXPR_STR  },
		{ 'n', "tracknumber" , SHORT_EXPR_INT  },
		{ 's', "stream"      , SHORT_EXPR_BOOL },
		{ 'T', "tag"         , SHORT_EXPR_BOOL },
		{ 't', "title"       , SHORT_EXPR_STR  },
		{ 'X', "play_count"  , SHORT_EXPR_INT  },
		{ 'y', "date"        , SHORT_EXPR_INT  },
	};

	for (size_t i = 0; i < N_ELEMENTS(map); i++) {
		if (map[i].short_key == c) {
			*type = map[i].type;
			return map[i].long_key;
		}
	}
	return NULL;
}

static int short_expr_err(struct short_expr *e, const char *fmt, ...)
{
	char buf[128];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	e->err(buf, e->opaque);
	return -1;
}

static void short_expr_push_char(struct short_expr *e, char c)
{
	gbuf_add_ch(e->output, c);
}

static void short_expr_push_str(struct short_expr *e, const char *s)
{
	gbuf_add_str(e->output, s);
}

static void short_expr_push_strn(struct short_expr *e, const char *s, size_t n)
{
	gbuf_add_bytes(e->output, s, n);
}

static void short_expr_skip_spaces(struct short_expr *e)
{
	while (*e->input == ' ')
		e->input++;
}

static int short_expr_int(struct short_expr *e)
{
	short_expr_skip_spaces(e);
	size_t len = 0;
	while (isdigit(e->input[len]))
		len++;
	if (len == 0)
		return short_expr_err(e, "integer expected");
	short_expr_push_strn(e, e->input, len);
	e->input += len;
	return 0;
}

static int short_expr_le_ge_int_arg(struct short_expr *e)
{
	short_expr_push_str(e, e->key);
	short_expr_push_char(e, *e->input++);
	return short_expr_int(e);
}

static int short_expr_upto_int_arg(struct short_expr *e)
{
	short_expr_push_str(e, e->key);
	short_expr_push_str(e, "<=");
	e->input++; /* skip - */
	return short_expr_int(e);
}

static int short_expr_range_int_arg(struct short_expr *e, const char *first, size_t len)
{
	short_expr_push_str(e, e->key);
	short_expr_push_str(e, ">=");
	short_expr_push_strn(e, first, len);
	e->input++; /* skip - */
	short_expr_skip_spaces(e);
	if (isdigit(*e->input)) {
		short_expr_push_char(e, '&');
		short_expr_push_str(e, e->key);
		short_expr_push_str(e, "<=");
		return short_expr_int(e);
	}
	return 0;
}

static int short_expr_plain_int_arg(struct short_expr *e)
{
	size_t len = 0;
	const char *start = e->input;
	while (isdigit(e->input[len]))
		len++;
	e->input += len;
	short_expr_skip_spaces(e);
	if (*e->input == '-')
		return short_expr_range_int_arg(e, start, len);

	short_expr_push_str(e, e->key);
	short_expr_push_char(e, '=');
	short_expr_push_strn(e, start, len);
	return 0;
}

static int short_expr_int_arg(struct short_expr *e)
{
	short_expr_push_char(e, '(');
	short_expr_skip_spaces(e);
	int res;
	char c = *e->input;

	if (c == '<' || c == '>')
		res = short_expr_le_ge_int_arg(e);
	else if (c == '-')
		res = short_expr_upto_int_arg(e);
	else if (isdigit(c))
		res = short_expr_plain_int_arg(e);
	else
		return short_expr_err(e, "integer expected");

	short_expr_push_char(e, ')');
	return res;
}

static int short_expr_quoted_str_arg(struct short_expr *e)
{
	const char *start = e->input;
	e->input++; /* skip " */
	bool esc = false;
	while (1) {
		char c = *e->input;
		if (c == '"' && !esc)
			break;
		else if (esc)
			esc = false;
		else if (c == '\\')
			esc = true;
		if (c == 0)
			break;
		else
			e->input++;
	}
	size_t len = e->input - start;
	if (*e->input == '"')
		e->input++;
	short_expr_push_strn(e, start, len);
	short_expr_push_char(e, '"');
	return 0;
}

static int short_expr_plain_str_arg(struct short_expr *e)
{
	size_t len = 0;
	while (1) {
		char c = e->input[len];
		if (c == '~' || c == '!' || c == '|' || c == '(' || c == ')' || c == 0)
			break;
		len++;
	}
	while (len > 0 && e->input[len - 1] == ' ')
		len--;
	short_expr_push_str(e, "\"*");
	short_expr_push_strn(e, e->input, len);
	short_expr_push_str(e, "*\"");
	e->input += len;
	return 0;
}

static int short_expr_str_arg(struct short_expr *e)
{
	short_expr_push_str(e, e->key);
	short_expr_push_char(e, '=');
	short_expr_skip_spaces(e);
	if (*e->input == '"')
		return short_expr_quoted_str_arg(e);
	return short_expr_plain_str_arg(e);
}

static int short_expr_bool_arg(struct short_expr *e)
{
	short_expr_push_str(e, e->key);
	return 0;
}

static int short_expr_key(struct short_expr *e)
{
	e->input++; /* skip ~ */
	char short_key = *e->input;
	int type;
	e->key = short_expr_lookup(short_key, &type);
	if (!e->key)
		return short_expr_err(e, "unknown short key %c", short_key);
	e->input++; /* skip short_key */

	if (type == SHORT_EXPR_INT)
		return short_expr_int_arg(e);
	if (type == SHORT_EXPR_STR)
		return short_expr_str_arg(e);
	if (type == SHORT_EXPR_BOOL)
		return short_expr_bool_arg(e);
	BUG_ON(1);
}

static int short_expr_parse_longest(struct short_expr *e);

static int short_expr_paren(struct short_expr *e)
{
	short_expr_push_char(e, '(');
	e->input++; /* skip ( */
	if (short_expr_parse_longest(e))
		return -1;
	if (*e->input == ')')
		e->input++;
	else
		return short_expr_err(e, "')' expected");
	short_expr_push_char(e, ')');
	return 0;
}

static int short_expr_parse_one(struct short_expr *e);

static int short_expr_not(struct short_expr *e)
{
	short_expr_push_char(e, '!');
	short_expr_push_char(e, '(');
	e->input++; /* skip ! */
	if (short_expr_parse_one(e))
		return -1;
	short_expr_push_char(e, ')');
	return 0;
}

static bool short_expr_starts_expr(char c)
{
	return c == '~' || c == '(' || c == '!';
}

static int short_expr_parse_one(struct short_expr *e)
{
	short_expr_skip_spaces(e);
	char c = *e->input;
	if (c == '~')
		return short_expr_key(e);
	if (c == '(')
		return short_expr_paren(e);
	if (c == '!')
		return short_expr_not(e);
	if (c == 0)
		return 0;
	return short_expr_err(e, "unexpected '%c'", c);
}

static int short_expr_parse_longest(struct short_expr *e)
{
	char connector = 0;
	while (1) {
		short_expr_skip_spaces(e);
		if (!short_expr_starts_expr(*e->input))
			break;
		if (connector != 0)
			short_expr_push_char(e, connector);
		if (short_expr_parse_one(e))
			return -1;
		short_expr_skip_spaces(e);
		connector = *e->input;
		if (connector == '|')
			e->input++;
		else
			connector = '&';
	}
	if (connector == '|')
		return short_expr_err(e, "expected '~'");
	return 0;
}

static int short_expr_parse_(struct short_expr *e)
{
	if (short_expr_parse_longest(e))
		return -1;
	if (*e->input != 0)
		return short_expr_err(e, "unexpected '%c'", *e->input);
	return 0;
}

char *short_expr_expand(const char *input, void (*err)(const char *, void *),
		void *opaque)
{
	GBUF(output);
	char *ret = NULL;

	struct short_expr e = {
		.input = input,
		.output = &output,
		.err = err,
		.opaque = opaque,
	};

	gbuf_grow(&output, strlen(input) * 7);

	if (!short_expr_parse_(&e)) {
		gbuf_add_ch(&output, 0);
		ret = gbuf_steal(&output);
		d_print("expanded \"%s\" to \"%s\"\n", input, ret);
	}

	gbuf_free(&output);
	return ret;
}

bool short_expr_is_short(const char *s)
{
	for (; *s; s++) {
		if (*s == '~')
			return true;
		if (*s != '!' && *s != '(' && *s != ' ' && *s != ')')
			return false;
	}
	return false;
}
