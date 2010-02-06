#ifndef KEYVAL_H
#define KEYVAL_H

struct keyval {
	char *key;
	char *val;
};

struct growing_keyvals {
	struct keyval *keyvals;
	int alloc;
	int count;
};

#define GROWING_KEYVALS(name) struct growing_keyvals name = { NULL, 0, 0 }

void keyvals_init(struct growing_keyvals *c, const struct keyval *keyvals);
void keyvals_add(struct growing_keyvals *c, const char *key, char *val);
const char *keyvals_get_val_growing(const struct growing_keyvals *c, const char *key);
void keyvals_terminate(struct growing_keyvals *c);
void keyvals_free(struct keyval *keyvals);
struct keyval *keyvals_dup(const struct keyval *keyvals);
const char *keyvals_get_val(const struct keyval *keyvals, const char *key);

#endif
