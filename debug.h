/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2004-2005 Timo Hirvonen
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

#ifndef CMUS_DEBUG_H_8871604286E243AF95574E613D7945F5
#define CMUS_DEBUG_H_8871604286E243AF95574E613D7945F5

#include "compiler.h"
#ifdef HAVE_CONFIG
#include "config/debug.h"
#endif

#include <errno.h>
#include <stdint.h>

void debug_init(void);
void __debug_bug(const char *function, const char *fmt, ...) __FORMAT(2, 3) __NORETURN;
void __debug_print(const char *function, const char *fmt, ...) __FORMAT(2, 3);

uint64_t timer_get(void);
void timer_print(const char *what, uint64_t usec);

#define BUG(...) __debug_bug(__FUNCTION__, __VA_ARGS__)

#define __STR(a) #a

#define BUG_ON(a)			\
do {					\
	if (unlikely(a))		\
		BUG("%s\n", __STR(a));	\
} while (0)

#define d_print(...) __debug_print(__FUNCTION__, __VA_ARGS__)

#endif
