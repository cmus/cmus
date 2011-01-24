/*
 * Copyright 2005 Timo Hirvonen
 */

#ifndef _INPUT_H
#define _INPUT_H

#include "comment.h"
#include "sf.h"

struct input_plugin;

void ip_load_plugins(void);

/*
 * allocates new struct input_plugin.
 * never fails. does not check if the file is really playable
 */
struct input_plugin *ip_new(const char *filename);

/*
 * frees struct input_plugin closing it first if necessary
 */
void ip_delete(struct input_plugin *ip);

/*
 * errors: IP_ERROR_{ERRNO, FILE_FORMAT, SAMPLE_FORMAT}
 */
int ip_open(struct input_plugin *ip);

void ip_setup(struct input_plugin *ip);

/*
 * errors: none?
 */
int ip_close(struct input_plugin *ip);

/*
 * errors: IP_ERROR_{ERRNO, FILE_FORMAT}
 */
int ip_read(struct input_plugin *ip, char *buffer, int count);

/*
 * errors: IP_ERROR_{FUNCTION_NOT_SUPPORTED}
 */
int ip_seek(struct input_plugin *ip, double offset);

/*
 * errors: IP_ERROR_{ERRNO}
 */
int ip_read_comments(struct input_plugin *ip, struct keyval **comments);

int ip_duration(struct input_plugin *ip);
int ip_bitrate(struct input_plugin *ip);

sample_format_t ip_get_sf(struct input_plugin *ip);
const char *ip_get_filename(struct input_plugin *ip);
const char *ip_get_metadata(struct input_plugin *ip);
int ip_is_remote(struct input_plugin *ip);
int ip_metadata_changed(struct input_plugin *ip);
int ip_eof(struct input_plugin *ip);
char *ip_get_error_msg(struct input_plugin *ip, int rc, const char *arg);
char **ip_get_supported_extensions(void);
void ip_dump_plugins(void);

#endif
