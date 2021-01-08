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

#ifndef CMUS_UTILS_H
#define CMUS_UTILS_H

#ifdef HAVE_CONFIG
#include "config/utils.h"
#endif

#include "compiler.h"
#include "debug.h"

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>
#include <stdint.h>
#ifdef HAVE_BYTESWAP_H
#include <byteswap.h>
#endif


#define N_ELEMENTS(array) (sizeof(array) / sizeof((array)[0]))

struct slice {
	void *ptr;
	size_t len;
};

#define TO_SLICE(type, ...) ((struct slice) { \
	.ptr = ((type[]){__VA_ARGS__}), \
	.len = N_ELEMENTS(((type[]){__VA_ARGS__})), \
})

#define STRINGIZE_HELPER(x) #x
#define STRINGIZE(x) STRINGIZE_HELPER(x)

#define CONCATENATE_HELPER(x,y) x##y
#define CONCATENATE(x,y) CONCATENATE_HELPER(x,y)

#define getentry(ptr, offset, type) (*((type *) ((void *) ((char *) (ptr) + (offset)))))

#define STATIC_ASSERT(cond) \
	static uint8_t CONCATENATE(_cmus_unused_, __LINE__)[2*(cond) - 1] UNUSED

static inline long min_i(long a, long b)
{
	return a < b ? a : b;
}

static inline unsigned long min_u(unsigned long a, unsigned long b)
{
	return a < b ? a : b;
}

static inline long max_i(long a, long b)
{
	return a > b ? a : b;
}

static inline int clamp(int val, int minval, int maxval)
{
	if (val < minval)
		return minval;
	if (val > maxval)
		return maxval;
	return val;
}

static inline int scale_from_percentage(int val, int max_val)
{
	if (val < 0)
		return (val * max_val - 50) / 100;
	return (val * max_val + 50) / 100;
}

static inline int scale_to_percentage(int val, int max_val)
{
	int half = max_val / 2;

	if (max_val <= 0)
		return 100;

	if (val < 0)
		return (val * 100 - half) / max_val;
	return (val * 100 + half) / max_val;
}

static inline int str_to_int(const char *str, long int *val)
{
	char *end;

	*val = strtol(str, &end, 10);
	if (*str == 0 || *end != 0)
		return -1;
	return 0;
}

static inline int strcmp0(const char *str1, const char *str2)
{
	if (!str1)
		return str2 ? -1 : 0;
	if (!str2)
		return 1;

	return strcmp(str1, str2);
}

static inline int is_space(const char ch)
{
	return (ch == ' ' || ch == '\t');
}

static inline int ends_with(const char *str, const char *suffix)
{
	return strstr(str, suffix) + strlen(suffix) == str + strlen(str);
}

static inline void strip_trailing_spaces(char *str)
{
	char *end = str + strlen(str);
	while (end > str && is_space(*(end-1))) {
		end--;
	}
	*end = 0;
}

static inline uint32_t hash_str(const char *s)
{
	const unsigned char *p = (const unsigned char *)s;
	uint32_t h = 5381;

	while (*p) {
		h *= 33;
		h ^= *p++;
	}

	return h ^ (h >> 16);
}

static inline time_t file_get_mtime(const char *filename)
{
	struct stat s;

	/* stat follows symlinks, lstat does not */
	if (stat(filename, &s) == -1)
		return -1;
	return s.st_mtime;
}

static inline void ns_sleep(int ns)
{
	struct timespec req;

	req.tv_sec = 0;
	req.tv_nsec = ns;
	nanosleep(&req, NULL);
}

static inline void us_sleep(int us)
{
	ns_sleep(us * 1e3);
}

static inline void ms_sleep(int ms)
{
	ns_sleep(ms * 1e6);
}

static inline int is_http_url(const char *name)
{
	return strncmp(name, "http://", 7) == 0;
}

static inline int is_cdda_url(const char *name)
{
	return strncmp(name, "cdda://", 7) == 0;
}

static inline int is_cue_url(const char *name)
{
	return strncmp(name, "cue://", 6) == 0;
}

static inline int is_url(const char *name)
{
	return is_http_url(name) || is_cdda_url(name) || is_cue_url(name);
}

static inline int is_freeform_true(const char *c)
{
	return	c[0] == '1' ||
		c[0] == 'y' || c[0] == 'Y' ||
		c[0] == 't' || c[0] == 'T';
}

/* e.g. NetBSD */
#if defined(bswap16)
/* GNU libc */
#elif defined(bswap_16)
# define bswap16 bswap_16
/* e.g. OpenBSD */
#elif defined(swap16)
# define bswap16 swap16
#else
# define bswap16(x) \
	((((x) >> 8) & 0xff) | (((x) & 0xff) << 8))
#endif

static inline uint16_t swap_uint16(uint16_t x)
{
	return bswap16(x);
}

/* e.g. NetBSD */
#if defined(bswap32)
/* GNU libc */
#elif defined(bswap_32)
# define bswap32 bswap_32
/* e.g. OpenBSD */
#elif defined(swap32)
# define bswap32 swap32
#else
# define bswap32(x) \
	((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) |	\
	 (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))
#endif

static inline uint32_t swap_uint32(uint32_t x)
{
	return bswap32(x);
}

static inline uint32_t read_le32(const char *buf)
{
	const unsigned char *b = (const unsigned char *)buf;

	return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
}

static inline uint32_t read_le24(const char *buf)
{
	const unsigned char *b = (const unsigned char *)buf;

	return b[0] | (b[1] << 8) | (b[2] << 16);
}

static inline int32_t read_le24i(const char *buf)
{
	uint32_t a = read_le24(buf);
	return (a & 0x800000) ? 0xFF000000 | a : a;
}

static inline uint16_t read_le16(const char *buf)
{
	const unsigned char *b = (const unsigned char *)buf;

	return b[0] | (b[1] << 8);
}

static int _saved_stdout;
static int _saved_stderr;

static inline void disable_stdio(void)
{
	_saved_stdout = dup(1);
	_saved_stderr = dup(2);
	if (_saved_stdout == -1 || _saved_stderr == -1) {
		return;
	}

	int devnull = open("/dev/null", O_WRONLY);
	dup2(devnull, 1);
	dup2(devnull, 2);
	close(devnull);
}

static inline void enable_stdio(void)
{
	fflush(stdout);
	fflush(stderr);
	while (dup2(_saved_stdout, 1) == -1 && errno == EINTR) { }
	while (dup2(_saved_stderr, 2) == -1 && errno == EINTR) { }
	close(_saved_stdout);
	close(_saved_stderr);
}

static inline void init_pipes(int *out, int *in)
{
	int fds[2];
	int rc = pipe(fds);
	BUG_ON(rc);
	*out = fds[0];
	*in = fds[1];
	int flags = fcntl(*out, F_GETFL);
	rc = fcntl(*out, F_SETFL, flags | O_NONBLOCK);
	BUG_ON(rc);
}

static inline void notify_via_pipe(int pipe)
{
	char buf = 0;
	if (write(pipe, &buf, 1) != 1) {
		d_print("write to pipe failed. errno = %d\n", errno);
	}
}

static inline void clear_pipe(int pipe, size_t bytes)
{
	char buf[128];
	size_t bytes_to_read = min_u(sizeof(buf), bytes);
	if (read(pipe, buf, bytes_to_read) < 0) {
		d_print("read from pipe failed. errno = %d\n", errno);
	}
}

static inline ssize_t strscpy(char *dst, const char *src, size_t size)
{
	for (size_t i = 0; i < size; i++) {
		dst[i] = src[i];
		if (src[i] == 0)
			return i;
	}
	if (size > 0)
		dst[size - 1] = 0;
	return -1;
}

#endif
