#include "tabexp.h"
#include "xmalloc.h"
#include "xstrjoin.h"
#include "debug.h"

#include <stdlib.h>

struct tabexp tabexp = {
	.head = NULL,
	.tails = NULL,
	.count = 0
};

char *tabexp_expand(const char *src, void (*load_matches)(const char *src), int direction)
{
	static int idx = -1;
	char *expanded;

	if (tabexp.tails == NULL) {
		load_matches(src);
		if (tabexp.tails == NULL) {
			BUG_ON(tabexp.head != NULL);
			return NULL;
		}
		BUG_ON(tabexp.head == NULL);
		idx = -1;
	}
	idx += direction;

	if (idx >= tabexp.count)
		idx = 0;
	else if (idx < 0)
		idx = tabexp.count - 1;

	expanded = xstrjoin(tabexp.head, tabexp.tails[idx]);
	if (tabexp.count == 1)
		tabexp_reset();
	return expanded;
}

void tabexp_reset(void)
{
	int i;
	for (i = 0; i < tabexp.count; i++)
		free(tabexp.tails[i]);
	free(tabexp.tails);
	free(tabexp.head);
	tabexp.tails = NULL;
	tabexp.head = NULL;
	tabexp.count = 0;
}
