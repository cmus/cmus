#include "tabexp.h"
#include "xmalloc.h"
#include "xstrjoin.h"
#include "debug.h"

#include <stdlib.h>

struct tabexp tabexp = {
	.head = NULL,
	.tails = NULL
};

char *tabexp_expand(const char *src, void (*load_matches)(const char *src))
{
	static int idx = 0;
	char *expanded;

	if (tabexp.tails == NULL) {
		load_matches(src);
		if (tabexp.tails == NULL) {
			BUG_ON(tabexp.head != NULL);
			return NULL;
		}
		BUG_ON(tabexp.head == NULL);
		idx = 0;
	}

	expanded = xstrjoin(tabexp.head, tabexp.tails[idx++]);
	if (!tabexp.tails[idx])
		idx = 0;
	if (!tabexp.tails[1])
		tabexp_reset();
	return expanded;
}

void tabexp_reset(void)
{
	free_str_array(tabexp.tails);
	free(tabexp.head);
	tabexp.tails = NULL;
	tabexp.head = NULL;
}
