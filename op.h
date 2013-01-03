/*
 * Copyright 2008-2013 Various Authors
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _OP_H
#define _OP_H

#include "sf.h"
#include "channelmap.h"

#ifndef __GNUC__
#include <fcntl.h>
#endif

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
	/* mixer not open */
	OP_ERROR_NOT_OPEN,
	/* plugin does not support the sample format */
	OP_ERROR_SAMPLE_FORMAT,
	/* plugin does not have this option */
	OP_ERROR_NOT_OPTION,
	/*  */
	OP_ERROR_INTERNAL
};

struct output_plugin_ops {
	int (*init)(void);
	int (*exit)(void);
	int (*open)(sample_format_t sf, const channel_position_t *channel_map);
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

/* symbols exported by plugin */
extern const struct output_plugin_ops op_pcm_ops;
extern const char * const op_pcm_options[];
extern const int op_priority;

#endif
