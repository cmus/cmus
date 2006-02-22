/* 
 * Copyright Timo Hirvonen
 */

#ifndef _LOAD_DIR_H
#define _LOAD_DIR_H

#include <sys/stat.h>

int load_dir(const char *dirname, char ***names,
		int (*filter)(const char *name, const struct stat *s),
		int (*compare)(const void *a, const void *b));

#endif
