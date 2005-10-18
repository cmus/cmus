/*
 * Copyright 2005 Timo Hirvonen
 */

#ifndef FILTERS_H
#define FILTERS_H

#include <list.h>
#include <window.h>
#include <search.h>
#include <uchar.h>

struct filter_entry {
	struct list_head node;

	char *name;
	char *filter;
	unsigned selected : 1;
	unsigned active : 1;
	unsigned visited : 1;
};

static inline struct filter_entry *iter_to_filter_entry(struct iter *iter)
{
	return iter->data1;
}

extern struct window *filters_win;
extern struct searchable *filters_searchable;
extern int filters_changed;

void filters_init(void);
void filters_exit(void);

/* add filter to filter list (replaces old filter with same name)
 *
 * @keyval  "name=value" where value is filter
 */
void filters_set_filter(const char *keyval);

/* set throwaway filter (not saved to the filter list)
 *
 * @val   filter or NULL to disable filtering
 */
void filters_set_anonymous(const char *val);

int filters_ch(uchar ch);
int filters_key(int key);

#endif
