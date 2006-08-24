/*
 * Copyright 2005 Timo Hirvonen
 */

#ifndef PROG_H
#define PROG_H

#include "compiler.h"

/* set in beginning of main */
extern char *program_name;

struct option {
	/* short option or 0 */
	int short_opt;

	/* long option or NULL */
	const char *long_opt;

	/* does option have an argument */
	int has_arg;
};

/*
 * arg: returned argument if .has_arg is 1
 *
 * returns: index to options array or -1 of no more options
 */
int get_option(char **argv[], const struct option *options, char **arg);

void warn(const char *format, ...) __FORMAT(1, 2);
void warn_errno(const char *format, ...) __FORMAT(1, 2);
void die(const char *format, ...) __FORMAT(1, 2) __NORETURN;
void die_errno(const char *format, ...) __FORMAT(1, 2) __NORETURN;

#endif
