/* 
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _COMMENT_H
#define _COMMENT_H

struct keyval {
	char *key;
	char *val;
};

extern struct keyval *comments_dup(const struct keyval *comments);
extern void comments_free(struct keyval *comments);

/* case insensitive key */
extern const char *comments_get_val(const struct keyval *comments, const char *key);
extern int comments_get_int(const struct keyval *comments, const char *key);
extern int comments_get_date(const struct keyval *comments, const char *key);

int is_interesting_key(const char *key);
void fix_track_or_disc(char *str);

#endif
