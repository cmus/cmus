/*
 * Copyright 2005 Timo Hirvonen
 */

#include <symbol.h>
#include <prog.h>

#include <dlfcn.h>
#include <stdio.h>

int get_symbol(void *handle, const char *name, const char *filename, void **symp, int null_is_valid)
{
	dlerror();
	*symp = dlsym(handle, name);
	if (*symp == NULL && !null_is_valid) {
		warn("%s: symbol %s not found\n", filename, name);
		return 0;
	}
	return 1;
}
