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

#include <sconf.h>
#include <list.h>
#include <xmalloc.h>
#include <file.h>

#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>

struct sconf_option {
	struct list_head node;
	char *name;
	enum { CO_STR, CO_INT, CO_FLT, CO_BOOL } type;
	union {
		char *str_val;
		int int_val;
		double flt_val;
		int bool_val;
	} u;
};

/* skip white space and comment */
static inline void skip_wsc(const char *str, int *posp)
{
	int pos = *posp;

	while (str[pos] == ' ' || str[pos] == '\t')
		pos++;
	if (str[pos] == '#') {
		while (str[pos])
			pos++;
	}
	*posp = pos;
}

/* gets name '[a-zA-Z_][a-zA-Z_0-9.]*' */
static int get_option_name(struct sconf_option *opt, const char *str, int *posp)
{
	int pos = *posp;

	if (!isalpha(str[pos]) && str[pos] != '_') {
		/* syntax error */
		return -1;
	}
	pos++;
	while (isalnum(str[pos]) || str[pos] == '_' || str[pos] == '.')
		pos++;
	opt->name = (char *)xstrndup(str, pos - *posp);
	*posp = pos;
	return 0;
}

static int get_option_value(struct sconf_option *opt, const char *str, int *posp)
{
	int pos = *posp;

	if (str[pos] == '"') {
		/* str */
		int start, len;

		pos++;
		start = pos;
		len = 0;
		while (str[pos]) {
			if (str[pos] == '"')
				break;
			if (str[pos] == '\\') {
				pos++;
				if (str[pos] == 0) {
					/* syntax error */
					return -1;
				}
			}
			pos++;
			len++;
		}
		opt->type = CO_STR;
		opt->u.str_val = xnew(char, len + 1);
		pos = start;
		len = 0;
		while (str[pos]) {
			if (str[pos] == '"')
				break;
			if (str[pos] == '\\') {
				pos++;
			}
			opt->u.str_val[len] = str[pos];
			pos++;
			len++;
		}
		opt->u.str_val[len] = 0;
		pos++;
	} else if (isdigit(str[pos]) || str[pos] == '.' ||
			str[pos] == '-' || str[pos] == '+') {
		/* int or flt */
		int sign = 1;
		int lval = 0;
		int llen = 0;
		int esign = 1;
		int eval = 0;
		int elen = 0;
		double rval = 0.0;
		double rdiv = 1.0;

		opt->type = CO_INT;
		/* get sign */
		if (str[pos] == '+') {
			pos++;
		} else if (str[pos] == '-') {
			sign = -1;
			pos++;
		}
		/* N part */
		while (isdigit(str[pos])) {
			lval *= 10;
			lval += str[pos] - '0';
			llen++;
			pos++;
		}
		if (str[pos] == '.') {
			pos++;
			opt->type = CO_FLT;
			while (isdigit(str[pos])) {
				rval *= 10.0;
				rval += str[pos] - '0';
				rdiv *= 10.0;
				pos++;
			}
		}
		if (llen == 0 && rdiv == 1.0) {
			/* syntax error */
			return -1;
		}
		if (str[pos] == 'e') {
			pos++;
			if (str[pos] == '+') {
				pos++;
			} else if (str[pos] == '-') {
				opt->type = CO_FLT;
				esign = -1;
				pos++;
			}
			while (isdigit(str[pos])) {
				eval *= 10;
				eval += str[pos] - '0';
				elen++;
				pos++;
			}
			if (elen == 0) {
				/* syntax error */
				return -1;
			}
		}
		if (opt->type == CO_FLT) {
			opt->u.flt_val = (double)lval + rval / rdiv;
			opt->u.flt_val *= (double)sign;
			opt->u.flt_val *= pow(10.0, (double)(esign * eval));
		} else {
			opt->u.int_val = sign * lval * (int)pow(10.0, eval);
		}
	} else {
		/* boolean */
		if (strncmp(str + pos, "true", 4) == 0) {
			opt->u.bool_val = 1;
			pos += 4;
		} else if (strncmp(str + pos, "false", 5) == 0) {
			opt->u.bool_val = 0;
			pos += 5;
		} else {
			/* syntax error */
			return -1;
		}
		opt->type = CO_BOOL;
	}
	*posp = pos;
	return 0;
}

static void option_free(struct sconf_option *opt)
{
	free(opt->name);
	if (opt->type == CO_STR)
		free(opt->u.str_val);
	free(opt);
}

static char *co_str_to_str(const char *src)
{
	char *dst;
	int len, i, j;

	len = 0;
	i = 0;
	while (src[i]) {
		if (src[i] == '"')
			len++;
		len++;
		i++;
	}
	dst = xnew(char, len + 1);
	i = 0;
	j = 0;
	while (src[i]) {
		if (src[i] == '"') {
			dst[j++] = '\\';
		}
		dst[j++] = src[i++];
	}
	dst[j] = 0;
	return dst;
}

static void add_option(struct list_head *head, struct sconf_option *new)
{
	struct list_head *item;

	list_for_each(item, head) {
		struct sconf_option *opt;

		opt = list_entry(item, struct sconf_option, node);
		if (strcmp(opt->name, new->name) == 0) {
			list_del(&opt->node);
			option_free(opt);
			break;
		}
	}
	list_add_tail(&new->node, head);
}

int sconf_load(struct list_head *head, const char *filename, int *ln)
{
	int i, rc;
	char **lines;
	struct sconf_option *opt;

	lines = file_get_lines(filename);
	if (lines == NULL)
		return -SCONF_ERROR_ERRNO;

	rc = 0;
	for (i = 0; ; i++) {
		const char *line = lines[i];
		int j = 0;

		if (line == NULL)
			goto exit;
		skip_wsc(line, &j);
		if (line[j] == 0)
			continue;

		opt = xnew(struct sconf_option, 1);
		opt->name = NULL;
		if (get_option_name(opt, line, &j))
			break;
		skip_wsc(line, &j);
		if (line[j] != '=')
			break;
		j++;
		skip_wsc(line, &j);
		if (get_option_value(opt, line, &j))
			break;
		skip_wsc(line, &j);
		if (line[j] != 0) {
			option_free(opt);
			goto syntax_error;
		}
		add_option(head, opt);
	}
	free(opt->name);
	free(opt);
syntax_error:
	rc = -SCONF_ERROR_SYNTAX;
	sconf_free(head);
exit:
	*ln = i + 1;
	free_str_array(lines);
	return rc;
}

int sconf_save(struct list_head *head, const char *filename)
{
	const char *header = "# Do not edit while the program is running or your changes will be lost.\n\n";
	struct list_head *item;
	int fd, rc;

	fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0666);
	if (fd == -1)
		return -SCONF_ERROR_ERRNO;
	rc = write_all(fd, header, strlen(header));
	if (rc == -1) {
		int save = errno;
		close(fd);
		errno = save;
		return -SCONF_ERROR_ERRNO;
	}
	list_for_each(item, head) {
		struct sconf_option *opt;
		char str[1024];
		char *s;
		int pos, count;

		opt = list_entry(item, struct sconf_option, node);
		switch (opt->type) {
		case CO_STR:
			s = co_str_to_str(opt->u.str_val);
			snprintf(str, sizeof(str), "%s = \"%s\"\n",
					opt->name, s);
			free(s);
			break;
		case CO_INT:
			snprintf(str, sizeof(str), "%s = %d\n",
					opt->name, opt->u.int_val);
			break;
		case CO_FLT:
			snprintf(str, sizeof(str), "%s = %g\n",
					opt->name, opt->u.flt_val);
			break;
		case CO_BOOL:
			snprintf(str, sizeof(str), "%s = %s\n",
					opt->name,
					opt->u.bool_val ? "true" : "false");
			break;
		}
		count = strlen(str);
		pos = 0;
		while (count) {
			rc = write_all(fd, str + pos, count);
			if (rc == -1) {
				int save = errno;
				close(fd);
				errno = save;
				return -SCONF_ERROR_ERRNO;
			}
			pos += rc;
			count -= rc;
		}
	}
	close(fd);
	return 0;
}

void sconf_free(struct list_head *head)
{
	struct list_head *item;

	item = head->next;
	while (item != head) {
		struct list_head *next;

		next = item->next;
		option_free(list_entry(item, struct sconf_option, node));
		item = next;
	}
	list_init(head);
}

int sconf_get_str_option(struct list_head *head, const char *name, char **value)
{
	struct list_head *item;

	list_for_each(item, head) {
		struct sconf_option *opt;

		opt = list_entry(item, struct sconf_option, node);
		if (strcmp(opt->name, name) == 0) {
			if (opt->type != CO_STR)
				return -SCONF_ERROR_TYPE;
			*value = xstrdup(opt->u.str_val);
			return 0;
		}
	}
	return -SCONF_ERROR_NOTFOUND;
}

int sconf_get_int_option(struct list_head *head, const char *name, int *value)
{
	struct list_head *item;

	list_for_each(item, head) {
		struct sconf_option *opt;

		opt = list_entry(item, struct sconf_option, node);
		if (strcmp(opt->name, name) == 0) {
			if (opt->type != CO_INT)
				return -SCONF_ERROR_TYPE;
			*value = opt->u.int_val;
			return 0;
		}
	}
	return -SCONF_ERROR_NOTFOUND;
}

int sconf_get_flt_option(struct list_head *head, const char *name, double *value)
{
	struct list_head *item;

	list_for_each(item, head) {
		struct sconf_option *opt;

		opt = list_entry(item, struct sconf_option, node);
		if (strcmp(opt->name, name) == 0) {
			if (opt->type == CO_FLT) {
				*value = opt->u.flt_val;
				return 0;
			} else if (opt->type == CO_INT) {
				*value = opt->u.int_val;
				return 0;
			}
			return -SCONF_ERROR_TYPE;
		}
	}
	return -SCONF_ERROR_NOTFOUND;
}

int sconf_get_bool_option(struct list_head *head, const char *name, int *value)
{
	struct list_head *item;

	list_for_each(item, head) {
		struct sconf_option *opt;

		opt = list_entry(item, struct sconf_option, node);
		if (strcmp(opt->name, name) == 0) {
			if (opt->type != CO_BOOL)
				return -SCONF_ERROR_TYPE;
			*value = opt->u.bool_val;
			return 0;
		}
	}
	return -SCONF_ERROR_NOTFOUND;
}

int sconf_set_str_option(struct list_head *head, const char *name,
		const char *value)
{
	struct list_head *item;
	struct sconf_option *opt;

	list_for_each(item, head) {
		opt = list_entry(item, struct sconf_option, node);
		if (strcmp(opt->name, name) == 0) {
			if (opt->type == CO_STR)
				free(opt->u.str_val);
			opt->type = CO_STR;
			opt->u.str_val = xstrdup(value);
			return 0;
		}
	}
	opt = xnew(struct sconf_option, 1);
	opt->name = xstrdup(name);
	opt->type = CO_STR;
	opt->u.str_val = xstrdup(value);
	list_add_tail(&opt->node, head);
	return 0;
}

int sconf_set_int_option(struct list_head *head, const char *name, int value)
{
	struct list_head *item;
	struct sconf_option *opt;

	list_for_each(item, head) {
		opt = list_entry(item, struct sconf_option, node);
		if (strcmp(opt->name, name) == 0) {
			if (opt->type == CO_STR)
				free(opt->u.str_val);
			opt->type = CO_INT;
			opt->u.int_val = value;
			return 0;
		}
	}
	opt = xnew(struct sconf_option, 1);
	opt->name = xstrdup(name);
	opt->type = CO_INT;
	opt->u.int_val = value;
	list_add_tail(&opt->node, head);
	return 0;
}

int sconf_set_flt_option(struct list_head *head, const char *name, double value)
{
	struct list_head *item;
	struct sconf_option *opt;

	list_for_each(item, head) {
		opt = list_entry(item, struct sconf_option, node);
		if (strcmp(opt->name, name) == 0) {
			if (opt->type == CO_STR)
				free(opt->u.str_val);
			opt->type = CO_FLT;
			opt->u.flt_val = value;
			return 0;
		}
	}
	opt = xnew(struct sconf_option, 1);
	opt->name = xstrdup(name);
	opt->type = CO_FLT;
	opt->u.flt_val = value;
	list_add_tail(&opt->node, head);
	return 0;
}

int sconf_set_bool_option(struct list_head *head, const char *name, int value)
{
	struct list_head *item;
	struct sconf_option *opt;

	list_for_each(item, head) {
		opt = list_entry(item, struct sconf_option, node);
		if (strcmp(opt->name, name) == 0) {
			if (opt->type == CO_STR)
				free(opt->u.str_val);
			opt->type = CO_BOOL;
			opt->u.bool_val = value;
			return 0;
		}
	}
	opt = xnew(struct sconf_option, 1);
	opt->name = xstrdup(name);
	opt->type = CO_BOOL;
	opt->u.bool_val = value;
	list_add_tail(&opt->node, head);
	return 0;
}
