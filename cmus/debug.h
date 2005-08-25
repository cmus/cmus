/* 
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

/*
 * DEBUG must be defined before including this file
 *
 * DEBUG levels:
 *     0:
 *     1: BUG, BUG_ON
 *     2: BUG, BUG_ON, d_print
 * 
 * void debug_init(FILE *stream)
 *     Call this before any other functions in this file.
 *     Sets the debug stream.
 *
 * void d_print(const char *format, ...)
 *     Print debugging information to the debug stream.
 *
 * void BUG(const char *format, ...)
 *     Print debugging information to the debug stream and to
 *     stderr if debug stream is NOT stdout or stderr.
 *     Exits with return value 127.
 *
 * void BUG_ON(int condition)
 *     Calls BUG if condition is true.
 */

#ifndef _DEBUG_H
#define _DEBUG_H

/* unlikely() */
#include <xmalloc.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/time.h>
#include <inttypes.h>

#if !defined(DEBUG)
#error DEBUG not defined
#endif

#if DEBUG < 0
#error DEBUG must be >= 0
#endif

#if DEBUG > 1
#define __format_check(fmt, first) __attribute__((format(printf, (fmt), (first))))
#else
#define __format_check(fmt, first)
#endif

extern void debug_init(FILE *stream);
extern void __debug_bug(const char *function, const char *fmt, ...) __format_check(2, 3) __noreturn;
extern void __debug_print(const char *function, const char *fmt, ...) __format_check(2, 3);

/* ------------------------------------------------------------------------ */

#if DEBUG == 0

#define BUG(...) do { } while (0)

#else /* >0 */

#define BUG(...) __debug_bug(__FUNCTION__, __VA_ARGS__)

#endif

/* ------------------------------------------------------------------------ */

#if DEBUG == 0 || DEBUG == 1

#define d_print(...) do { } while (0)

static inline void timer_get(uint64_t *usec)
{
	*usec = 0;
}

static inline void timer_print(const char *what, uint64_t usec)
{
}

#else /* >1 */

#define d_print(...) __debug_print(__FUNCTION__, __VA_ARGS__)

static inline void timer_get(uint64_t *usec)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	*usec = tv.tv_sec * 1e6L + tv.tv_usec;
}

static inline void timer_print(const char *what, uint64_t usec)
{
	uint64_t a = usec / 1e6;
	uint64_t b = usec - a * 1e6;

	__debug_print("TIMER", "%s: %11Lu.%06Lu\n", what, a, b);
}

#endif

/* ------------------------------------------------------------------------ */

#define __STR(a) #a

#define BUG_ON(a) \
do { \
	if (unlikely(a)) \
		BUG("%s\n", __STR(a)); \
} while (0)

#endif
