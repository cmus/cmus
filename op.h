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

#ifndef CMUS_OP_H
#define CMUS_OP_H

#include "sf.h"
#include "channelmap.h"
#ifdef HAVE_CONFIG
#include "config/plugin.h"
#endif

#ifndef __GNUC__
#include <fcntl.h>
#endif

#define OP_ABI_VERSION 4

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

};

#define OPT(prefix, name) { #name, prefix ## _set_ ## name, \
	prefix ## _get_ ## name }

struct output_plugin_opt {
	const char *name;
	int (*set)(const char *val);
	int (*get)(char **val);
};

#define NR_MIXER_FDS 4

enum {
    /* volume changes */
    MIXER_FDS_VOLUME,
    /* output changes */
    MIXER_FDS_OUTPUT
};

struct mixer_plugin_ops {
	int (*init)(void);
	int (*exit)(void);
	int (*open)(int *volume_max);
	int (*close)(void);
	int (*get_fds)(int what, int *fds);
	int (*set_volume)(int l, int r);
	int (*get_volume)(int *l, int *r);
};

struct mixer_plugin_opt {
	const char *name;
	int (*set)(const char *val);
	int (*get)(char **val);
};

struct output_plugin_api {
	const int priority;
	const struct output_plugin_ops *pcm_ops;
	const struct output_plugin_opt *pcm_options; /* null-terminated array */
	const struct mixer_plugin_ops *mixer_ops; /* optional */
	const struct mixer_plugin_opt *mixer_options; /* null-terminated array, required if has mixer_ops */
};

#ifndef STATICPLUGIN
#define CMUS_OP_DEFINE(...) \
	const unsigned op_abi_version = OP_ABI_VERSION; \
	const struct output_plugin_api op_api = (struct output_plugin_api){__VA_ARGS__};
#else
#define CMUS_OP_DEFINE(...) \
	static const unsigned op_abi_version = OP_ABI_VERSION; \
	static const struct output_plugin_api op_api = (struct output_plugin_api){__VA_ARGS__}; \
	__attribute__((constructor)) static void op_register(void) { cmus_op_register(__FILE__, op_abi_version, &op_api); }
#endif

extern int cmus_op_register(const char *filename, unsigned abi_version, const struct output_plugin_api *api);

#endif
