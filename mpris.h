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

#ifndef CMUS_MPRIS_H
#define CMUS_MPRIS_H

#include "config/mpris.h"

#ifdef CONFIG_MPRIS

extern int mpris_fd;
void mpris_init(void);
void mpris_process(void);
void mpris_free(void);
void mpris_playback_status_changed(void);
void mpris_loop_status_changed(void);
void mpris_shuffle_changed(void);
void mpris_volume_changed(void);
void mpris_metadata_changed(void);
void mpris_seeked(void);

#else

#define mpris_fd -1
#define mpris_init() { }
#define mpris_process() { }
#define mpris_free() { }
#define mpris_playback_status_changed() { }
#define mpris_loop_status_changed() { }
#define mpris_shuffle_changed() { }
#define mpris_volume_changed() { }
#define mpris_metadata_changed() { }
#define mpris_seeked() { }

#endif

#endif
