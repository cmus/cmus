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

#ifndef _COMMENT_H
#define _COMMENT_H

#include "keyval.h"

int track_is_compilation(const struct keyval *comments);
int track_is_va_compilation(const struct keyval *comments);

const char *comments_get_albumartist(const struct keyval *comments);
const char *comments_get_artistsort(const struct keyval *comments); /* can return NULL */

int comments_get_int(const struct keyval *comments, const char *key);
double comments_get_double(const struct keyval *comments, const char *key);
int comments_get_date(const struct keyval *comments, const char *key);

int comments_add(struct growing_keyvals *c, const char *key, char *val);
int comments_add_const(struct growing_keyvals *c, const char *key, const char *val);

#endif
