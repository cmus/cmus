/* 
 * Copyright 2004-2005 Timo Hirvonen
 */

#ifndef DEBUG_H
#define DEBUG_H

#include "compiler.h"
#include "config/debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/time.h>
#include <inttypes.h>

void debug_init(void);
void __debug_bug(const char *function, const char *fmt, ...) __FORMAT(2, 3) __NORETURN;
void __debug_print(const char *function, const char *fmt, ...) __FORMAT(2, 3);

/* ------------------------------------------------------------------------ */

#if DEBUG <= 0

#define BUG(...) do { } while (0)

#else /* >0 */

#define BUG(...) __debug_bug(__FUNCTION__, __VA_ARGS__)

#endif

/* ------------------------------------------------------------------------ */

#if DEBUG <= 1

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

	__debug_print("TIMER", "%s: %11u.%06u\n", what, (unsigned int)a, (unsigned int)b);
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
