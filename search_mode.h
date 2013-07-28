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

#ifndef _SEARCH_MODE_H
#define _SEARCH_MODE_H

#include "search.h"
#include "uchar.h"

extern char *search_str;
extern enum search_direction search_direction;

/* //WORDS or ??WORDS search mode */
extern int search_restricted;

void search_mode_ch(uchar ch);
void search_mode_escape(int c);
void search_mode_key(int key);
void search_mode_init(void);
void search_mode_exit(void);

void search_text(const char *text, int restricted, int beginning);

#endif
