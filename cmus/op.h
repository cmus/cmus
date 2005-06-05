/* 
 * Copyright 2004 Timo Hirvonen
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

#ifndef _OP_H
#define _OP_H

#include <sf.h>

enum {
	/* no error */
	OP_ERROR_SUCCESS,
	/* system error (error code in errno) */
	OP_ERROR_ERRNO,
	/* no such plugin */
	OP_ERROR_NO_PLUGIN,
	/* plugin not initialized */
	OP_ERROR_NOT_INITIALIZED,
	/* function not supported */
	OP_ERROR_NOT_SUPPORTED,
	/* plugin does not support the sample format */
	OP_ERROR_SAMPLE_FORMAT,
	/* plugin does not have this option */
	OP_ERROR_NOT_OPTION
};

struct output_plugin_ops {
	int (*init)(void);
	int (*exit)(void);
	int (*open)(const struct sample_format *sf);
	int (*close)(void);
	int (*drop)(void);
	int (*write)(const char *buffer, int count);
	int (*buffer_space)(void);

	/* these can be NULL */
	int (*pause)(void);
	int (*unpause)(void);

	int (*set_option)(int key, const char *val);
	int (*get_option)(int key, char **val);
};


/*
 * errors: OP_ERROR_{}
 */
extern int op_init(void);

/*
 * errors: OP_ERROR_{}
 */
extern int op_exit(void);

/*
 * select output plugin and open its mixer
 *
 * errors: OP_ERROR_{ERRNO, NO_PLUGIN}
 */
extern int op_select(const char *name);

/*
 * open selected plugin
 *
 * errors: OP_ERROR_{}
 */
extern int op_open(const struct sample_format *sf);

/*
 * returns:
 *     0 if sample format didn't change
 *     1 if sample format did change
 *     OP_ERROR_{} on error
 */
extern int op_set_sf(const struct sample_format *sf);

extern int op_second_size(void);

/*
 * drop pcm data
 *
 * errors: OP_ERROR_{ERRNO}
 */
extern int op_drop(void);

/*
 * close plugin
 *
 * errors: OP_ERROR_{}
 */
extern int op_close(void);

/*
 * returns bytes written or error
 *
 * errors: OP_ERROR_{ERRNO}
 */
extern int op_write(const char *buffer, int count);

/*
 * errors: OP_ERROR_{}
 */
extern int op_pause(void);
extern int op_unpause(void);

/*
 * returns space in output buffer in bytes or -1 if busy
 */
extern int op_buffer_space(void);

/*
 * errors: OP_ERROR_{}
 */
extern int op_reset(void);

/*
 * errors: OP_ERROR_{}
 */
extern int op_set_volume(int left, int right);
extern int op_get_volume(int *left, int *right);

/*
 * adds volume and returns new volume
 *
 *
 */
extern int op_add_volume(int *left, int *right);

/*
 * returns 1 if changed, 0 otherwise
 */
extern int op_volume_changed(int *left, int *right);

/*
 * errors: OP_ERROR_{NO_PLUGIN, NOT_INITIALIZED, NOT_OPTION}
 */
extern int op_set_option(const char *key, const char *val);

extern int op_for_each_option(void (*callback)(void *data, const char *key), void *data);
extern char *op_get_error_msg(int rc, const char *arg);

#endif
