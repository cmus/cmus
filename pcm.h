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

#ifndef _PCM_H
#define _PCM_H

typedef void (*pcm_conv_func)(void *dst, const void *src, int count);
typedef void (*pcm_conv_in_place_func)(void *buf, int count);

extern pcm_conv_func pcm_conv[8];
extern pcm_conv_in_place_func pcm_conv_in_place[8];

#endif
