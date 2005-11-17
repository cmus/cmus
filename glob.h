/* 
 * Copyright 2005 Timo Hirvonen
 */

#ifndef GLOB_H
#define GLOB_H

#include <list.h>

void glob_compile(struct list_head *head, const char *pattern);
void glob_free(struct list_head *head);
int glob_match(struct list_head *head, const char *text);

#endif
