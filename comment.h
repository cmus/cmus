#ifndef _COMMENT_H
#define _COMMENT_H

struct keyval {
	char *key;
	char *val;
};

struct growing_keyvals {
	struct keyval *comments;
	int alloc;
	int count;
};

#define GROWING_KEYVALS(name) struct growing_keyvals name = { NULL, 0, 0 }

extern struct keyval *comments_dup(const struct keyval *comments);
extern void comments_free(struct keyval *comments);

/* case insensitive key */
extern const char *comments_get_val(const struct keyval *comments, const char *key);
extern int comments_get_int(const struct keyval *comments, const char *key);
extern int comments_get_date(const struct keyval *comments, const char *key);

int comments_add(struct growing_keyvals *c, const char *key, char *val);
int comments_add_const(struct growing_keyvals *c, const char *key, const char *val);
void comments_terminate(struct growing_keyvals *c);

#endif
