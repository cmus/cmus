#ifndef _COMMENT_H
#define _COMMENT_H

#include "keyval.h"

const char *comments_get_albumartist(const struct keyval *comments);
int comments_get_int(const struct keyval *comments, const char *key);
int comments_get_date(const struct keyval *comments, const char *key);

int comments_add(struct growing_keyvals *c, const char *key, char *val);
int comments_add_const(struct growing_keyvals *c, const char *key, const char *val);

#endif
