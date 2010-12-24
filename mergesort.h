#ifndef _MERGESORT_H
#define _MERGESORT_H

#include "list.h"

void list_mergesort(struct list_head *head,
	int (*compare)(const struct list_head *, const struct list_head *));

#endif
