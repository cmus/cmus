#ifndef JOB_H
#define JOB_H

#include "cmus.h"

struct add_data {
	enum file_type type;
	char *name;
	add_ti_cb add;
};

struct update_data {
	size_t size;
	size_t used;
	struct track_info **ti;
};

void do_add_job(void *data);
void free_add_job(void *data);
void do_update_job(void *data);
void free_update_job(void *data);

#endif
