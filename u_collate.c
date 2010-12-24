/*
 * Copyright 2010 Johannes Weißl (based on gunicollate.c from glib)
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

#include "u_collate.h"
#include "uchar.h"
#include "xmalloc.h"
#include "ui_curses.h" /* using_utf8, charset */
#include "convert.h"

#include <stdlib.h>
#include <string.h>

int u_strcoll(const char *str1, const char *str2)
{
	int result;

	if (using_utf8) {
		result = strcoll(str1, str2);
	} else {
		char *str1_locale, *str2_locale;

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

	return result;
}

int u_strcasecoll(const char *str1, const char *str2)
{
	char *cf_a, *cf_b;
	int res;

	cf_a = u_casefold(str1);
	cf_b = u_casefold(str2);

	res = u_strcoll(cf_a, cf_b);

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
