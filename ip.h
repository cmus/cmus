/*
 * Copyright 2008-2013 Various Authors
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef CMUS_IP_H
#define CMUS_IP_H

#include "keyval.h"
#include "sf.h"
#include "channelmap.h"

#ifndef __GNUC__
#include <fcntl.h>
#include <unistd.h>
#endif

#define IP_ABI_VERSION 1

enum {
	/* no error */
	IP_ERROR_SUCCESS,
	/* system error (error code in errno) */
	IP_ERROR_ERRNO,
	/* file type not recognized */
	IP_ERROR_UNRECOGNIZED_FILE_TYPE,
	/* file type recognized, but not supported */
	IP_ERROR_UNSUPPORTED_FILE_TYPE,
	/* function not supported (usually seek) */
	IP_ERROR_FUNCTION_NOT_SUPPORTED,
	/* input plugin detected corrupted file */
	IP_ERROR_FILE_FORMAT,
	/* malformed uri */
	IP_ERROR_INVALID_URI,
	/* sample format not supported */
	IP_ERROR_SAMPLE_FORMAT,
	/* wrong disc inserted */
	IP_ERROR_WRONG_DISC,
	/* could not read disc */
	IP_ERROR_NO_DISC,
	/* error parsing response line / headers */
	IP_ERROR_HTTP_RESPONSE,
	/* usually 404 */
	IP_ERROR_HTTP_STATUS,
	/* too many redirections */
	IP_ERROR_HTTP_REDIRECT_LIMIT,
	/* plugin does not have this option */
	IP_ERROR_NOT_OPTION,
	/*  */
	IP_ERROR_INTERNAL
};

struct input_plugin_data {
	/* filled by ip-layer */
	char *filename;
	int fd;

	unsigned int remote : 1;
	unsigned int metadata_changed : 1;

	/* shoutcast */
	int counter;
	int metaint;
	char *metadata;
	char *icy_name;
	char *icy_genre;
	char *icy_url;

	/* filled by plugin */
	sample_format_t sf;
	channel_position_t channel_map[CHANNELS_MAX];
	void *private;
};

struct input_plugin_ops {
	int (*open)(struct input_plugin_data *ip_data);
	int (*close)(struct input_plugin_data *ip_data);
	int (*read)(struct input_plugin_data *ip_data, char *buffer, int count);
	int (*seek)(struct input_plugin_data *ip_data, double offset);
	int (*read_comments)(struct input_plugin_data *ip_data,
			struct keyval **comments);
	int (*duration)(struct input_plugin_data *ip_data);
	long (*bitrate)(struct input_plugin_data *ip_data);
	long (*bitrate_current)(struct input_plugin_data *ip_data);
	char *(*codec)(struct input_plugin_data *ip_data);
	char *(*codec_profile)(struct input_plugin_data *ip_data);
};

struct input_plugin_opt {
	const char *name;
	int (*set)(const char *val);
	int (*get)(char **val);
};

/* symbols exported by plugin */
extern const struct input_plugin_ops ip_ops;
extern const int ip_priority;
extern const char * const ip_extensions[];
extern const char * const ip_mime_types[];
extern const struct input_plugin_opt ip_options[];
extern const unsigned ip_abi_version;

#endif
