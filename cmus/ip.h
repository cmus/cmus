/* 
 * Copyright 2004-2005 Timo Hirvonen
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _IP_H
#define _IP_H

#include <comment.h>
#include <sf.h>

#include <stdio.h>
#include <stdio.h>

enum {
	/* no error */
	IP_ERROR_SUCCESS,
	/* system error (error code in errno) */
	IP_ERROR_ERRNO,
	/* file type not supported */
	IP_ERROR_UNRECOGNIZED_FILE_TYPE,
	/* function not supported (usually seek) */
	IP_ERROR_FUNCTION_NOT_SUPPORTED,
	/* input plugin detected corrupted file */
	IP_ERROR_FILE_FORMAT,
	/* malformed uri */
	IP_ERROR_INVALID_URI,
	/* sample format not supported */
	IP_ERROR_SAMPLE_FORMAT,
	/* error parsing response line / headers */
	IP_ERROR_HTTP_RESPONSE,
	/* usually 404 */
	IP_ERROR_HTTP_STATUS,
	/*  */
	IP_ERROR_INTERNAL
};

struct input_plugin_data {
	/* filled by ip-layer */
	char *filename;
	int fd;

	unsigned int remote : 1;
	unsigned int metadata_changed : 1;
	int counter;
	int metaint;
	char *metadata;
	int http_code;
	char *http_reason;

	/* filled by plugin */
	struct sample_format sf;
	void *private;
};

struct input_plugin_ops {
	int (*open)(struct input_plugin_data *ip_data);
	int (*close)(struct input_plugin_data *ip_data);
	int (*read)(struct input_plugin_data *ip_data, char *buffer, int count);
	int (*seek)(struct input_plugin_data *ip_data, double offset);
	int (*read_comments)(struct input_plugin_data *ip_data,
			struct comment **comments);
	int (*duration)(struct input_plugin_data *ip_data);
};

struct input_plugin {
	const struct input_plugin_ops *ops;
	struct input_plugin_data data;
	unsigned int open : 1;
	unsigned int eof : 1;
};

/*
 * errors: IP_ERROR_{UNRECOGNIZED_FILE_TYPE, INVALID_URI}
 */
extern int ip_create(struct input_plugin *ip, const char *filename);

extern void ip_delete(struct input_plugin *ip);

/*
 * errors: IP_ERROR_{ERRNO, FILE_FORMAT, SAMPLE_FORMAT}
 */
extern int ip_open(struct input_plugin *ip, struct sample_format *sf);

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

extern int ip_eof(struct input_plugin *ip);
extern char *ip_get_error_msg(struct input_plugin *ip, int rc, const char *arg);
extern char **ip_get_supported_extensions(void);

#endif
