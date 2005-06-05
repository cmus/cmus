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

#ifndef _HISTORY_H
#define _HISTORY_H

#include <list.h>
#include <uchar.h>

struct history_entry {
	struct list_head node;
	char *text;
};

struct history {
	struct list_head head;
	struct list_head *search_pos;

	/* allocated size */
	int current_size;

	/* length in bytes */
	int current_blen;

	/* length in characters */
	int current_clen;

	/* pos in bytes */
	int current_bpos;

	/* pos in characters */
	int current_cpos;

	int max_lines;
	int lines;
	char *current;
	char *search;
	char *save;
};

extern void history_init(struct history *history, int max_lines);
extern void history_insert_ch(struct history *history, uchar ch);
extern int history_backspace(struct history *history);
extern int history_delete_ch(struct history *history);
extern void history_current_save(struct history *history);
extern void history_move_left(struct history *history);
extern void history_move_right(struct history *history);
extern void history_move_home(struct history *history);
extern void history_move_end(struct history *history);
extern void history_free(struct history *history);
extern int history_search_forward(struct history *history);
extern int history_search_backward(struct history *history);
extern int history_load(struct history *history, const char *filename);
extern int history_save(struct history *history, const char *filename);

#endif
