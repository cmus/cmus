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

#ifndef _MIXER_H
#define _MIXER_H

#ifndef __GNUC__
#include <fcntl.h>
#endif

#define NR_MIXER_FDS 4

struct mixer_plugin_ops {
	int (*init)(void);
	int (*exit)(void);
	int (*open)(int *volume_max);
	int (*close)(void);
	int (*get_fds)(int *fds);
	int (*set_volume)(int l, int r);
	int (*get_volume)(int *l, int *r);
	int (*set_option)(int key, const char *val);
	int (*get_option)(int key, char **val);
};

/* symbols exported by plugin */
extern const struct mixer_plugin_ops op_mixer_ops;
extern const char * const op_mixer_options[];

#endif
