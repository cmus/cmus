/*
 * Copyright 2005 Timo Hirvonen
 */

#ifndef _INPUT_H
#define _INPUT_H

#include <comment.h>
#include <sf.h>

struct input_plugin;

extern void ip_init_plugins(void);

/*
 * errors: IP_ERROR_{UNRECOGNIZED_FILE_TYPE, INVALID_URI}
 */
extern int ip_create(struct input_plugin **ip, const char *filename);

extern void ip_delete(struct input_plugin *ip);

/*
 * errors: IP_ERROR_{ERRNO, FILE_FORMAT, SAMPLE_FORMAT}
 */
extern int ip_open(struct input_plugin *ip);

/*
 * errors: none?
 */
extern int ip_close(struct input_plugin *ip);

/*
 * errors: IP_ERROR_{ERRNO, FILE_FORMAT}
 */
extern int ip_read(struct input_plugin *ip, char *buffer, int count);

/*
 * errors: IP_ERROR_{FUNCTION_NOT_SUPPORTED}
 */
extern int ip_seek(struct input_plugin *ip, double offset);

/*
 * errors: IP_ERROR_{ERRNO}
 */
extern int ip_read_comments(struct input_plugin *ip, struct comment **comments);

/*
 * errors: IP_ERROR_{ERRNO, FUNCTION_NOT_SUPPORTED}
 */
extern int ip_duration(struct input_plugin *ip);

extern sample_format_t ip_get_sf(struct input_plugin *ip);
extern const char *ip_get_filename(struct input_plugin *ip);
extern const char *ip_get_metadata(struct input_plugin *ip);
extern int ip_is_remote(struct input_plugin *ip);
extern int ip_metadata_changed(struct input_plugin *ip);
extern int ip_eof(struct input_plugin *ip);
extern char *ip_get_error_msg(struct input_plugin *ip, int rc, const char *arg);
extern char **ip_get_supported_extensions(void);

#endif
