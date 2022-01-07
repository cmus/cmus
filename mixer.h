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

#ifndef CMUS_MIXER_H
#define CMUS_MIXER_H

#ifndef __GNUC__
#include <fcntl.h>
#endif

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
	union {
	    int (*abi_1)(int *fds); // MIXER_FDS_VOLUME
	    int (*abi_2)(int what, int *fds);
	} get_fds;
	int (*set_volume)(int l, int r);
	int (*get_volume)(int *l, int *r);
};

struct mixer_plugin_opt {
	const char *name;
	int (*set)(const char *val);
	int (*get)(char **val);
};

/* symbols exported by plugin */
extern const struct mixer_plugin_ops op_mixer_ops;
extern const struct mixer_plugin_opt op_mixer_options[];

#endif
