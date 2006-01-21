/*
 * Copyright 2005 Timo Hirvonen
 */

#ifndef OPTIONS_H
#define OPTIONS_H

extern int default_view;
extern const char *valid_sort_keys[];

void options_init(void);
void options_exit(void);

char **parse_sort_keys(const char *value);
char *keys_to_str(char **keys);

#endif
