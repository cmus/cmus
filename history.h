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

#ifndef _HISTORY_H
#define _HISTORY_H

#include "list.h"

struct history {
	struct list_head head;
	struct list_head *search_pos;
	char *filename;
	int max_lines;
	int lines;
};

void history_load(struct history *history, char *filename, int max_lines);
void history_save(struct history *history);
void history_free(struct history *history);
void history_add_line(struct history *history, const char *line);
void history_reset_search(struct history *history);
const char *history_search_forward(struct history *history, const char *text);
const char *history_search_backward(struct history *history, const char *text);

#endif
