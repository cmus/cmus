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
	int (*open)(sample_format_t sf);
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

#endif
