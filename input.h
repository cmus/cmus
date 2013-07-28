/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2005 Timo Hirvonen
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

#ifndef _INPUT_H
#define _INPUT_H

#include "keyval.h"
#include "sf.h"
#include "channelmap.h"

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
int ip_current_bitrate(struct input_plugin *ip);
char *ip_codec(struct input_plugin *ip);
char *ip_codec_profile(struct input_plugin *ip);

sample_format_t ip_get_sf(struct input_plugin *ip);
void ip_get_channel_map(struct input_plugin *ip, channel_position_t *channel_map);
const char *ip_get_filename(struct input_plugin *ip);
const char *ip_get_metadata(struct input_plugin *ip);
int ip_is_remote(struct input_plugin *ip);
int ip_metadata_changed(struct input_plugin *ip);
int ip_eof(struct input_plugin *ip);
void ip_add_options(void);
char *ip_get_error_msg(struct input_plugin *ip, int rc, const char *arg);
char **ip_get_supported_extensions(void);
void ip_dump_plugins(void);

#endif
