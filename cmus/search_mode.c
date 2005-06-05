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

#include <search_mode.h>
#include <ui_curses.h>
#include <search.h>
#include <xmalloc.h>
#include <xstrjoin.h>
#include <misc.h>

#include <curses.h>
#include <ctype.h>

struct history search_history;
char *search_str = NULL;
enum search_direction search_direction = SEARCH_FORWARD;

static int search_found = 0;
static char *search_history_filename;

void search_mode_ch(uchar ch)
{
	switch (ch) {
	case 0x1B:
		if (search_history.current_blen) {
			history_current_save(&search_history);
		}
		ui_curses_input_mode = NORMAL_MODE;
		break;
	case 0x0A:
		if (search_history.current_blen) {
			/* set new search string and save old one to the history */
			free(search_str);
			search_str = xstrdup(search_history.current);
			history_current_save(&search_history);
		} else {
			/* use old search string */
			if (search_str) {
				search_found = search_next(searchable, search_str, search_direction);
			}
		}
		if (!search_found)
			ui_curses_display_info_msg("Pattern not found: %s", search_str);
		ui_curses_input_mode = NORMAL_MODE;
		break;
	case 127:
		if (history_backspace(&search_history))
			ui_curses_input_mode = NORMAL_MODE;
		break;
	case 0x09:
		break;
	default:
		history_insert_ch(&search_history, ch);
		search_found = search(searchable, search_history.current, search_direction, search_history.current_clen == 1);
		break;
	}
}

void search_mode_key(int key)
{
	switch (key) {
	case KEY_DC:
		history_delete_ch(&search_history);
		break;
	case KEY_BACKSPACE:
		if (history_backspace(&search_history))
			ui_curses_input_mode = NORMAL_MODE;
		break;
	case KEY_LEFT:
		history_move_left(&search_history);
		break;
	case KEY_RIGHT:
		history_move_right(&search_history);
		break;
	case KEY_UP:
		if (history_search_forward(&search_history))
			search_found = search(searchable, search_history.current, search_direction, 0);
		break;
	case KEY_DOWN:
		if (history_search_backward(&search_history))
			search_found = search(searchable, search_history.current, search_direction, 0);
		break;
	case KEY_HOME:
		history_move_home(&search_history);
		break;
	case KEY_END:
		history_move_end(&search_history);
		break;
	default:
		break;
	}
}

void search_mode_init(void)
{
	search_history_filename = xstrjoin(cmus_cache_dir, "/ui_curses_search_history");
	history_init(&search_history, 100);
	history_load(&search_history, search_history_filename);
}

void search_mode_exit(void)
{
	history_save(&search_history, search_history_filename);
	history_free(&search_history);
	free(search_history_filename);
}
