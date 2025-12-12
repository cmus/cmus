#ifndef ALIAS_H
#define ALIAS_H

struct alias {
	char *name;
	char *command;
};

struct alias_node {
	struct alias alias;
	struct alias_node *next;
};

extern struct alias_node *alias_list;

struct alias *get_alias(char *name);

void add_alias(char *name, char *command);
/**
* returns 1 on successful deletion
* returns 0 on failure to delete 
*/
int delete_alias(char *name);

#endif
