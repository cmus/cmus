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

#ifndef CMUS_OUTPUT_H
#define CMUS_OUTPUT_H

#include "sf.h"
#include "channelmap.h"

extern int volume_max;
extern int volume_l;
extern int volume_r;

void op_load_plugins(void);
void op_exit_plugins(void);

/*
 * select output plugin and open its mixer
 *
 * errors: OP_ERROR_{ERRNO, NO_PLUGIN}
 */
int op_select(const char *name);
int op_select_any(void);

/*
 * open selected plugin
 *
 * errors: OP_ERROR_{}
 */
int op_open(sample_format_t sf, const channel_position_t *channel_map);

/*
 * drop pcm data
 *
 * errors: OP_ERROR_{ERRNO}
 */
int op_drop(void);

/*
 * close plugin
 *
 * errors: OP_ERROR_{}
 */
int op_close(void);

/*
 * returns bytes written or error
 *
 * errors: OP_ERROR_{ERRNO}
 */
int op_write(const char *buffer, int count);

/*
 * errors: OP_ERROR_{}
 */
int op_pause(void);
int op_unpause(void);

/*
 * returns space in output buffer in bytes or -1 if busy
 */
int op_buffer_space(void);

/*
 * errors: OP_ERROR_{}
 */
int op_reset(void);

void mixer_open(void);
void mixer_close(void);
int mixer_set_volume(int left, int right);
int mixer_read_volume(void);
int mixer_get_fds(int *fds);

void op_add_options(void);
char *op_get_error_msg(int rc, const char *arg);
void op_dump_plugins(void);
const char *op_get_current(void);

#endif
