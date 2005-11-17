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

#ifndef _MIXER_H
#define _MIXER_H

struct mixer_plugin_ops {
	int (*init)(void);
	int (*exit)(void);
	int (*open)(int *volume_max);
	int (*close)(void);
	int (*set_volume)(int l, int r);
	int (*get_volume)(int *l, int *r);
	int (*set_option)(int key, const char *val);
	int (*get_option)(int key, char **val);
};

#endif
