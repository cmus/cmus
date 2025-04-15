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

#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>

/* Helper function for CoreFoundation string comparison */
static int cf_strcoll(const char *str1, const char *str2, CFStringCompareFlags flags)
{
    int result;
    CFStringRef cfStr1 = CFStringCreateWithCString(NULL, str1, kCFStringEncodingUTF8);
    CFStringRef cfStr2 = CFStringCreateWithCString(NULL, str2, kCFStringEncodingUTF8);
    
    if (cfStr1 && cfStr2) {
        result = CFStringCompare(cfStr1, cfStr2, flags) - kCFCompareLessThan;
    } else {
        /* Fallback to byte comparison if CF conversion fails */
        result = strcmp(str1, str2);
    }
    
    if (cfStr1) CFRelease(cfStr1);
    if (cfStr2) CFRelease(cfStr2);
    
    return result;
}
#endif

int u_strcoll(const char *str1, const char *str2)
{
	int result;

#ifdef __APPLE__
	/* Use CoreFoundation for proper string comparison on macOS */
	result = cf_strcoll(str1, str2, kCFCompareNonliteral);
#else
	if (using_utf8) {
		result = strcoll(str1, str2);
	} else {
		char *str1_locale = NULL, *str2_locale = NULL;

		convert(str1, -1, &str1_locale, -1, charset, "UTF-8");
		convert(str2, -1, &str2_locale, -1, charset, "UTF-8");

		if (str1_locale && str2_locale)
			result = strcoll(str1_locale, str2_locale);
		else
			result = strcmp(str1, str2);

		if (str2_locale)
			free(str2_locale);
		if (str1_locale)
			free(str1_locale);
	}
#endif

	return result;
}

int u_strcasecoll(const char *str1, const char *str2)
{
	char *cf_a, *cf_b;
	int res;

	cf_a = u_casefold(str1);
	cf_b = u_casefold(str2);

#ifdef __APPLE__
	/* Use CoreFoundation for proper case-insensitive comparison on macOS */
	res = cf_strcoll(cf_a, cf_b, kCFCompareCaseInsensitive | kCFCompareNonliteral);
#else
	res = u_strcoll(cf_a, cf_b);
#endif

	free(cf_b);
	free(cf_a);

	return res;
}

int u_strcasecoll0(const char *str1, const char *str2)
{
	if (!str1)
		return str2 ? -1 : 0;
	if (!str2)
		return 1;

	return u_strcasecoll(str1, str2);
}

/* Helper function to create collation key using CoreFoundation on macOS */
#ifdef __APPLE__
static char *cf_create_collation_key(const char *str) 
{
	char *result = NULL;
	CFStringRef cfStr = CFStringCreateWithCString(NULL, str, kCFStringEncodingUTF8);
	
	if (cfStr) {
		/* Get a representation that can be used for sorting */
		CFMutableStringRef mStr = CFStringCreateMutableCopy(NULL, 0, cfStr);
		if (mStr) {
			/* Perform canonical decomposition and strip combining marks for comparison */
			CFStringNormalize(mStr, kCFStringNormalizationFormD);
			
			/* Convert back to C string */
			size_t max_size = CFStringGetMaximumSizeForEncoding(CFStringGetLength(mStr), kCFStringEncodingUTF8) + 2;
			result = xmalloc(max_size);
			result[0] = 'A'; /* Mark as preprocessed */
			
			if (CFStringGetCString(mStr, result + 1, max_size - 1, kCFStringEncodingUTF8)) {
				/* Success */
			} else {
				/* Fallback in case of conversion failure */
				free(result);
				result = NULL;
			}
			
			CFRelease(mStr);
		}
		CFRelease(cfStr);
	}
	
	return result;
}
#endif

char *u_strcoll_key(const char *str)
{
#ifdef __APPLE__
	/* On macOS, create a collation key using CoreFoundation */
	char *result = cf_create_collation_key(str);
	return result;
#else
	/* For other platforms, use the original implementation */
	char *result = NULL;

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

	if (!result) {
		size_t xfrm_len = strlen(str);
		result = xmalloc(xfrm_len + 2);
		result[0] = 'B';
		memcpy(result + 1, str, xfrm_len);
		result[xfrm_len+1] = '\0';
	}

	return result;
#endif
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
