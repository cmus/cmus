/*
 * Copyright 2008-2015 Various Authors
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

#include "strnatcmp.h"

#include <string.h>
#include <ctype.h>
#include <inttypes.h>

static const char *skip_ws(const char *str)
{
        while (*str != '\0' && (isspace(*str) || iscntrl(*str) || !isprint(*str))) 
		str++;

	return str;
}

int strnatcmp(const char *str1, const char *str2)
{
	if (str1 == NULL)
		return str2 != NULL ? -1 : 0;

	if (str2 == NULL)
		return 1;

	if (*str1 == '\0')
		return *str2 != '\0' ? -1 : 0;

	if (*str2 == '\0')
		return 1;

	str1 = skip_ws(str1);
	str2 = skip_ws(str2);

	/* first digit or \0 in either of strings */
	int pos = 0;

	while (*(str1 + pos) != '\0'   &&
	       *(str2 + pos) != '\0'   &&
	       !isdigit(*(str1 + pos)) &&
	       !isdigit(*(str2 + pos))) {

		pos++;
	}

	/* better to use u_strncasecoll(), but it does not exist yet */
	const int cmp = strncasecmp(str1, str2, pos);
	if (cmp != 0) {
		return cmp;
	}

	char *str1_after_digits = NULL;
	char *str2_after_digits = NULL;
	const int num1 = strtoimax(str1 + pos, &str1_after_digits, 10);
	const int num2 = strtoimax(str2 + pos, &str2_after_digits, 10);

	return num1 != num2 ? num1 - num2 : strnatcmp(str1_after_digits, str2_after_digits);
}

