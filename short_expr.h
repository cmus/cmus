/*
 * Copyright 2016 Various Authors
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

#ifndef CMUS_SHORT_EXPR_H
#define CMUS_SHORT_EXPR_H

#include <stdbool.h>

char *short_expr_expand(const char *input, void (*err)(const char *, void *),
		void *opaque);
bool short_expr_is_short(const char *s);

#endif
