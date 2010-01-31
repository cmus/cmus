/* 
 * Copyright 2004-2005 Timo Hirvonen
 */

#ifndef DEBUG_H
#define DEBUG_H

#include "compiler.h"
#include "config/debug.h"

#include <errno.h>
#include <inttypes.h>

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
