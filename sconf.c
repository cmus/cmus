/* 
 * Copyright 2004-2005 Timo Hirvonen
 */

#include <sconf.h>
#include <list.h>
#include <xmalloc.h>
#include <file.h>
#include <prog.h>
#include <misc.h>

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
	};
};

static LIST_HEAD(head);
static char filename[256];
static int line_nr = 0;

static void option_free(struct sconf_option *opt)
{
	free(opt->name);
	if (opt->type == CO_STR)
		free(opt->str_val);
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

static void add_option(struct sconf_option *new)
{
	struct list_head *item;

	list_for_each(item, &head) {
		struct sconf_option *opt;

		opt = list_entry(item, struct sconf_option, node);
		if (strcmp(opt->name, new->name) == 0) {
			list_del(&opt->node);
			option_free(opt);
			break;
		}
	}
	list_add_tail(&new->node, &head);
}

static int handle_line(void *data, const char *line)
{
	struct sconf_option *opt;
	const char *ns;
	char *end;
	double d;

	line_nr++;
	while (isspace(*line))
		line++;

	if (*line == 0 || *line == '#')
		return 0;

	ns = line;
	while (1) {
		int ch = *line;
		if (!isalnum(ch) && ch != '_' && ch != '.')
			break;
		line++;
	}

	if (ns == line)
		goto syntax;

	opt = xnew(struct sconf_option, 1);
	opt->name = xstrndup(ns, line - ns);

	while (isspace(*line))
		line++;
	if (*line != '=')
		goto syntax;
	line++;

	while (isspace(*line))
		line++;

	d = strtod(line, &end);
	if (*line != 0 && end != line) {
		int i = (int)d;

		if (d != (double)i) {
			opt->type = CO_FLT;
			opt->flt_val = d;
		} else {
			opt->type = CO_INT;
			opt->int_val = i;
		}
		line = end;
	} else if (*line == '"') {
		const char *pos;
		char *str;
		int i = 0;

		line++;
		pos = line;

		str = xnew(char, strlen(line));
		while (1) {
			int ch = *pos++;

			if (ch == '"')
				break;
			if (ch == '\\')
				ch = *pos++;
			if (ch == 0) {
				free(str);
				goto syntax;
			}
			str[i++] = ch;
		}
		str[i] = 0;
		line = pos;

		opt->type = CO_STR;
		opt->str_val = str;
	} else {
		int val;

		if (strncmp(line, "true", 4) == 0) {
			val = 1;
			line += 4;
		} else if (strncmp(line, "false", 5) == 0) {
			val = 0;
			line += 5;
		} else {
			goto syntax;
		}

		opt->type = CO_BOOL;
		opt->bool_val = val;
	}

	while (isspace(*line))
		line++;

	if (*line != 0) {
		option_free(opt);
		goto syntax;
	}
	add_option(opt);
	return 0;
syntax:
	die("syntax error in file `%s` on line %d\n", filename, line_nr);
}

void sconf_load(void)
{
	snprintf(filename, sizeof(filename), "%s/config", cmus_config_dir);
	if (file_for_each_line(filename, handle_line, NULL))
		die_errno("error loading config file `%s'", filename);
}

void sconf_save(void)
{
	const char *header = "# Do not edit while the program is running or your changes will be lost.\n\n";
	struct list_head *item;
	int fd, rc;

	fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0666);
	if (fd == -1)
		goto error;
	rc = write_all(fd, header, strlen(header));
	if (rc == -1)
		goto error;
	list_for_each(item, &head) {
		struct sconf_option *opt;
		char str[1024];
		char *s;
		int pos, count;

		opt = list_entry(item, struct sconf_option, node);
		switch (opt->type) {
		case CO_STR:
			s = co_str_to_str(opt->str_val);
			snprintf(str, sizeof(str), "%s = \"%s\"\n",
					opt->name, s);
			free(s);
			break;
		case CO_INT:
			snprintf(str, sizeof(str), "%s = %d\n",
					opt->name, opt->int_val);
			break;
		case CO_FLT:
			snprintf(str, sizeof(str), "%s = %g\n",
					opt->name, opt->flt_val);
			break;
		case CO_BOOL:
			snprintf(str, sizeof(str), "%s = %s\n",
					opt->name,
					opt->bool_val ? "true" : "false");
			break;
		}
		count = strlen(str);
		pos = 0;
		while (count) {
			rc = write_all(fd, str + pos, count);
			if (rc == -1)
				goto error;
			pos += rc;
			count -= rc;
		}
	}
	close(fd);
	return;
error:
	warn("error saving `%s': %s\n", filename, strerror(errno));
	if (fd != -1)
		close(fd);
}

static struct sconf_option *find_opt(const char *name, int type)
{
	struct sconf_option *opt;

	list_for_each_entry(opt, &head, node) {
		if (strcmp(opt->name, name))
			continue;

		if (type == CO_FLT && opt->type == CO_INT) {
			int val = opt->int_val;

			opt->type = CO_FLT;
			opt->flt_val = (double)val;
		}
		if (opt->type != type)
			break;
		return opt;
	}
	return NULL;
}

static struct sconf_option *find_and_init_opt(const char *name, int type)
{
	struct sconf_option *opt;

	list_for_each_entry(opt, &head, node) {
		if (strcmp(opt->name, name))
			continue;

		if (opt->type == CO_STR)
			free(opt->str_val);
		goto init;
	}
	opt = xnew(struct sconf_option, 1);
	opt->name = xstrdup(name);
	list_add_tail(&opt->node, &head);
init:
	opt->type = type;
	return opt;
}

int sconf_get_str_option(const char *name, char **value)
{
	struct sconf_option *opt = find_opt(name, CO_STR);
	if (opt == NULL)
		return 0;
	*value = xstrdup(opt->str_val);
	return 1;
}

int sconf_get_int_option(const char *name, int *value)
{
	struct sconf_option *opt = find_opt(name, CO_INT);
	if (opt == NULL)
		return 0;
	*value = opt->int_val;
	return 1;
}

int sconf_get_flt_option(const char *name, double *value)
{
	struct sconf_option *opt = find_opt(name, CO_FLT);
	if (opt == NULL)
		return 0;
	*value = opt->flt_val;
	return 1;
}

int sconf_get_bool_option(const char *name, int *value)
{
	struct sconf_option *opt = find_opt(name, CO_BOOL);
	if (opt == NULL)
		return 0;
	*value = opt->bool_val;
	return 1;
}

void sconf_set_str_option(const char *name, const char *value)
{
	struct sconf_option *opt = find_and_init_opt(name, CO_STR);
	opt->str_val = xstrdup(value);
}

void sconf_set_int_option(const char *name, int value)
{
	struct sconf_option *opt = find_and_init_opt(name, CO_INT);
	opt->int_val = value;
}

void sconf_set_flt_option(const char *name, double value)
{
	struct sconf_option *opt = find_and_init_opt(name, CO_FLT);
	opt->flt_val = value;
}

void sconf_set_bool_option(const char *name, int value)
{
	struct sconf_option *opt = find_and_init_opt(name, CO_BOOL);
	opt->bool_val = value;
}
