/*
 * Copyright 2010-2013 Various Authors
 * Copyright 2010 Johannes Weißl
 *
 * based on gunicollate.c from glib
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

#include "u_collate.h"
#include "uchar.h"
#include "xmalloc.h"
#include "ui_curses.h" /* using_utf8, charset */
#include "convert.h"
#ifdef HAVE_CONFIG
#include "config/corefoundation.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifdef HAVE_COREFOUNDATION
#include <CoreFoundation/CoreFoundation.h>

/*
 * strxfrm() on macOS asserts inside libc for some non-ASCII strings
 * (https://github.com/cmus/cmus/issues/1421), so build the key with
 * CoreFoundation instead. Canonical decomposition (NFD) makes precomposed
 * and decomposed forms compare equal and keeps characters with diacritics
 * next to their base character when the key is compared bytewise.
 *
 * Returns NULL if @str could not be converted.
 */
static char *cf_strcoll_key(const char *str)
{
	CFStringRef cfstr;
	CFMutableStringRef normalized;
	CFIndex max_size;
	char *result = NULL;

	cfstr = CFStringCreateWithCString(kCFAllocatorDefault, str, kCFStringEncodingUTF8);
	if (!cfstr)
		return NULL;

	normalized = CFStringCreateMutableCopy(kCFAllocatorDefault, 0, cfstr);
	CFRelease(cfstr);
	if (!normalized)
		return NULL;

	CFStringNormalize(normalized, kCFStringNormalizationFormD);

	max_size = CFStringGetMaximumSizeForEncoding(CFStringGetLength(normalized), kCFStringEncodingUTF8);
	if (max_size != kCFNotFound && max_size < INT_MAX - 2) {
		result = xnew(char, max_size + 1);
		if (!CFStringGetCString(normalized, result, max_size + 1, kCFStringEncodingUTF8)) {
			free(result);
			result = NULL;
		}
	}

	CFRelease(normalized);
	return result;
}
#endif

char *u_strcoll_key(const char *str)
{
	char *result = NULL;

#ifdef HAVE_COREFOUNDATION
	result = cf_strcoll_key(str);
#else
	if (using_utf8) {
		size_t xfrm_len = strxfrm(NULL, str, 0);
		if ((ssize_t) xfrm_len >= 0 && xfrm_len < INT_MAX - 2) {
			result = xnew(char, xfrm_len + 1);
			strxfrm(result, str, xfrm_len + 1);
		}
	}

	if (!result) {
		char *str_locale = NULL;

		convert(str, -1, &str_locale, -1, charset, "UTF-8");

		if (str_locale) {
			size_t xfrm_len = strxfrm(NULL, str_locale, 0);
			if ((ssize_t) xfrm_len >= 0 && xfrm_len < INT_MAX - 2) {
				result = xnew(char, xfrm_len + 2);
				result[0] = 'A';
				strxfrm(result + 1, str_locale, xfrm_len + 1);
			}
			free(str_locale);
		}
	}
#endif

	if (!result) {
		size_t xfrm_len = strlen(str);
		result = xmalloc(xfrm_len + 2);
		result[0] = 'B';
		memcpy(result + 1, str, xfrm_len);
		result[xfrm_len+1] = '\0';
	}

	return result;
}

char *u_strcasecoll_key(const char *str)
{
	char *key, *cf_str;

	cf_str = u_casefold(str);

	key = u_strcoll_key(cf_str);

	free(cf_str);

	return key;
}

char *u_strcasecoll_key0(const char *str)
{
	return str ? u_strcasecoll_key(str) : NULL;
}
