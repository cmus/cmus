/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2005 Timo Hirvonen
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

#ifndef CMDLINE_H
#define CMDLINE_H

#include "uchar.h"

struct cmdline {
	/* length in bytes */
	int blen;

	/* length in characters */
	int clen;

	/* pos in bytes */
	int bpos;

	/* pos in characters */
	int cpos;

	/* allocated size */
	int size;

	char *line;
};

extern struct cmdline cmdline;

extern const char cmdline_word_delimiters[];
extern const char cmdline_filename_delimiters[];

void cmdline_init(void);
void cmdline_insert_ch(uchar ch);
void cmdline_backspace(void);
void cmdline_backspace_to_bol(void);
void cmdline_delete_ch(void);
void cmdline_set_text(const char *text);
void cmdline_clear(void);
void cmdline_clear_end(void);
void cmdline_move_left(void);
void cmdline_move_right(void);
void cmdline_move_home(void);
void cmdline_move_end(void);

void cmdline_forward_word(const char *delim);
void cmdline_backward_word(const char *delim);
void cmdline_delete_word(const char *delim);
void cmdline_backward_delete_word(const char *delim);

#endif
