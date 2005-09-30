/*
 * Copyright 2005 Timo Hirvonen
 */

#ifndef _COMPILER_H
#define _COMPILER_H

#if defined(__GNUC__) && ((__GNUC__ == 3 && __GNUC_MINOR__ >= 4) || (__GNUC__ >= 4))
/* gcc >= 3.4 */

/* Optimization: Condition @x is likely */
#define likely(x)	__builtin_expect(!!(x), 1)

/* Optimization: Condition @x is unlikely */
#define unlikely(x)	__builtin_expect(!!(x), 0)

/* Always inline */
#define inline		inline	__attribute__((always_inline))

/* Never inline */
#define noinline		__attribute__((__noinline__))

/* Optimization: Function depends on arguments and/or global data
 * Only side effect is return value
 */
#define __PURE			__attribute__((__pure__))

/* Optimization: Function MUST NOT reference pointer args or read global data
 * Only side effect is return value
 */
#define __CONST			__attribute__((__const__))

/* Optimization: Pointer returned can't alias other pointers */
#define __MALLOC		__attribute__((__malloc__))

/* Optimization: Function never returns */
#define __NORETURN		__attribute__((__noreturn__))

/* Must check return value */
#define __CHECK_RESULT		__attribute__((__warn_unused_result__))

/* Mark symbol deprecated */
#define __DEPRECATED		__attribute__((__deprecated__))

/* Mark symbol used */
#define __USED			__attribute__((__used__))

/* Mark symbol possibly unused */
#define __UNUSED		__attribute__((__unused__))

/* Argument at index @idx must not be NULL */
#define __NONNULL(idx)		__attribute__((__nonnull__(idx)))

/* All arguments must be non-NULL */
#define __ALL_NONNULL		__attribute__((__nonnull__))

/* Argument at index @fmt_idx is printf compatible format string and
 * argument at index @first_idx is the first format argument */
#define __FORMAT(fmt_idx, first_idx) __attribute__((format(printf, (fmt_idx), (first_idx))))

#else
/* gcc < 3.4 or not gcc */

#define likely(x)	(x)
#define unlikely(x)	(x)

/* #define inline */
#define noinline

#define __PURE
#define __CONST
#define __MALLOC
#define __NORETURN
#define __CHECK_RESULT

#define __DEPRECATED
#define __USED
#define __UNUSED

#define __NONNULL(a)
#define __ALL_NONNULL
#define __FORMAT(fmt_idx, first_idx)

#endif

#endif
