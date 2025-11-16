#include "alias.h"
#include "uchar.h"
#include "xmalloc.h"
#include <stdlib.h>

struct alias_node *alias_list = NULL;

struct alias *get_alias(char *name)
{
	int name_len = strlen(name);

	for (struct alias_node *i = alias_list; i != NULL; i = i->next) {
		if (strncmp(i->alias.name, name, name_len) == 0) {
			return &i->alias;
		}
	}

	return NULL;
}

void add_alias(char *name, char *command)
{
	struct alias *duplicate = get_alias(name);
	if (duplicate == NULL) {
		struct alias_node *new = xnew(struct alias_node, 1);
		new->alias.name = xstrdup(name);
		new->alias.command = xstrdup(command);
		new->next = alias_list;
		alias_list = new;
	} else {
		free(duplicate->command);

		duplicate->command = xstrdup(command);
	}
}
int delete_alias(char *name)
{
	if (alias_list == NULL) {
		return 0;
	}
	int name_len = strlen(name);

	if (strncmp(alias_list->alias.name, name, name_len) == 0) {
		free(alias_list->alias.name);
		free(alias_list->alias.command);
		free(alias_list);

		alias_list = NULL;
		return 1;
	}

	for (struct alias_node *i = alias_list; i->next != NULL; i = i->next) {
		if (strncmp(i->next->alias.name, name, name_len) == 0) {
			struct alias_node *node_to_be_free = i->next;

			i->next = node_to_be_free->next;

			free(node_to_be_free->alias.name);
			free(node_to_be_free->alias.command);
			free(node_to_be_free);

			return 1;
		}
	}
	return 0;
}
