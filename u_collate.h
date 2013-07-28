/*
 * Copyright 2010-2013 Various Authors
 * Copyright 2010 Johannes Weißl
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

#ifndef U_COLLATE_H
#define U_COLLATE_H

/*
 * @str1  valid, normalized, null-terminated UTF-8 string
 * @str2  valid, normalized, null-terminated UTF-8 string
 *
 * Compares two strings for ordering using the linguistically
 * correct rules for the current locale.
 *
 * Returns -1 if @str1 compares before @str2, 0 if they compare equal,
 * +1 if @str1 compares after @str2.
 */
int u_strcoll(const char *str1, const char *str2);

/*
 * @str1  valid, normalized, null-terminated UTF-8 string
 * @str2  valid, normalized, null-terminated UTF-8 string
 *
 * Like u_strcoll(), but do casefolding before comparing.
 */
int u_strcasecoll(const char *str1, const char *str2);

/*
 * @str1  valid, normalized, null-terminated UTF-8 string or NULL
 * @str2  valid, normalized, null-terminated UTF-8 string or NULL
 *
 * Like u_strcasecoll(), but handle NULL pointers gracefully.
 */
int u_strcasecoll0(const char *str1, const char *str2);

/*
 * @str  valid, normalized, null-terminated UTF-8 string
 *
 * Converts a string into a collation key that can be compared
 * with other collation keys produced by the same function using
 * strcmp().
 *
 * Returns a newly allocated string.
 */
char *u_strcoll_key(const char *str);

/*
 * @str  valid, normalized, null-terminated UTF-8 string
 *
 * Like u_strcoll_key(), but do casefolding before generating key.
 *
 * Returns a newly allocated string.
 */
char *u_strcasecoll_key(const char *str);

/*
 * @str  valid, normalized, null-terminated UTF-8 string or NULL
 *
 * Like u_strcasecoll_key(), but handle NULL pointers gracefully.
 */
char *u_strcasecoll_key0(const char *str);

#endif
