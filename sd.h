/*
 * Copyright 2013-2014 Various Authors
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

#ifndef _SD_H_
#define _SD_H_

#include "config/dbus.h"

#ifdef HAVE_DBUS
 #define SD_VAR extern
 #define SD_FUNC(f) f;
#else
 #define SD_VAR static __attribute__((unused))
 #define SD_FUNC(f) static inline f {}
#endif

enum sd_signal {
	SD_EXIT,
	SD_STATUS_CHANGE,
	SD_TRACK_CHANGE,
	SD_VOL_CHANGE,
};


SD_VAR int sd_socket;

SD_FUNC(void sd_init(void))
SD_FUNC(void sd_handle(void))
SD_FUNC(void sd_notify(enum sd_signal signal))

#endif
