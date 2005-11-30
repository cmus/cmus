/*
 * Copyright 2005 Timo Hirvonen
 */

#include <prog.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

char *program_name = NULL;

void warn(const char *format, ...)
{
	va_list ap;

	fprintf(stderr, "%s: ", program_name);
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
}

void warn_errno(const char *format, ...)
{
	int e = errno;
	va_list ap;

	fprintf(stderr, "%s: ", program_name);
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	fprintf(stderr, ": %s\n", strerror(e));
}

void __NORETURN die(const char *format, ...)
{
	va_list ap;

	fprintf(stderr, "%s: ", program_name);
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	exit(1);
}

void __NORETURN die_errno(const char *format, ...)
{
	int e = errno;
	va_list ap;

	fprintf(stderr, "%s: ", program_name);
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	fprintf(stderr, ": %s\n", strerror(e));
	exit(1);
}

static int short_option(int ch, const struct option *options)
{
	int i;

	for (i = 0; ; i++) {
		if (!options[i].short_opt) {
			if (!options[i].long_opt)
				die("unrecognized option `-%c'\n", ch);
			continue;
		}
		if (options[i].short_opt != ch)
			continue;
		return i;
	}
}

static int long_option(const char *opt, const struct option *options)
{
	int len, i, idx, num;

	len = strlen(opt);
	idx = -1;
	num = 0;
	for (i = 0; options[i].short_opt || options[i].long_opt; i++) {
		if (options[i].long_opt && strncmp(opt, options[i].long_opt, len) == 0) {
			idx = i;
			num++;
			if (options[i].long_opt[len] == 0) {
				/* exact */
				num = 1;
				break;
			}
		}
	}
	if (num > 1)
		die("option `--%s' is ambiguous\n", opt);
	if (num == 0)
		die("unrecognized option `--%s'\n", opt);
	return idx;
}

int get_option(char **argvp[], const struct option *options, char **arg)
{
	char **argv = *argvp;
	const char *opt = *argv;
	int i;

	*arg = NULL;
	if (opt == NULL || opt[0] != '-' || opt[1] == 0)
		return -1;

	if (opt[1] == '-') {
		if (opt[2] == 0) {
			/* '--' => no more options */
			*argvp = argv + 1;
			return -1;
		}
		i = long_option(opt + 2, options);
	} else if (opt[2]) {
		return -1;
	} else {
		i = short_option(opt[1], options);
	}
	argv++;
	if (options[i].has_arg) {
		if (*argv == NULL)
			die("option `%s' requires an argument\n", opt);
		*arg = *argv++;
	}
	*argvp = argv;
	return i;
}
