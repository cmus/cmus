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
#include <cmdline.h>
#include <history.h>
#include <ui_curses.h>
#include <search.h>
#include <xmalloc.h>
#include <xstrjoin.h>
#include <misc.h>

#include <curses.h>
#include <ctype.h>

char *search_str = NULL;
enum search_direction search_direction = SEARCH_FORWARD;

static int search_found = 0;
static struct history search_history;
static char *search_history_filename;
static char *history_search_text = NULL;

/* //WORDS or ??WORDS search mode */
static int search_restricted = 0;

static void update_search_line(const char *text)
{
	int len = strlen(text);
	char ch = search_direction == SEARCH_FORWARD ? '/' : '?';
	char *buf, *ptr;

	buf = xnew(char, len + 2);
	ptr = buf;
	if (search_restricted)
		*ptr++ = ch;
	memcpy(ptr, text, len + 1);
	cmdline_set_text(buf);
	free(buf);
}

static int search_line_empty(void)
{
	char ch;

	if (cmdline.clen == 0)
		return 1;
	if (cmdline.clen > 1)
		return 0;
	ch = search_direction == SEARCH_FORWARD ? '/' : '?';
	return cmdline.line[0] == ch;
}

static void do_search(void)
{
	char ch = search_direction == SEARCH_FORWARD ? '/' : '?';
	int restricted = 0;

	if (cmdline.line[0] == ch) {
		/* //WORDS or ??WORDS */
		restricted = 1;
	}
	search_found = search(searchable, cmdline.line + restricted, search_direction, 0);
}

static void reset_history_search(void)
{
	history_reset_search(&search_history);
	free(history_search_text);
	history_search_text = NULL;
}

static void backspace(void)
{
	if (cmdline.clen > 0) {
		cmdline_backspace();
		if (cmdline.clen > 0)
			do_search();
	} else {
		ui_curses_input_mode = NORMAL_MODE;
	}
}

void search_mode_ch(uchar ch)
{
	switch (ch) {
	case 0x1B:
		if (cmdline.blen) {
			history_add_line(&search_history, cmdline.line);
			cmdline_clear();
		}
		ui_curses_input_mode = NORMAL_MODE;
		break;
	case 0x0A:
		if (search_line_empty()) {
			/* use old search string */
			if (search_str) {
				search_found = search_next(searchable, search_str, search_direction);
			}
		} else {
			/* set new search string and add it to the history */
			free(search_str);
			search_str = xstrdup(cmdline.line);
			history_add_line(&search_history, cmdline.line);

			/* search not yet done if up or down arrow was pressed */
			do_search();
			cmdline_clear();
		}
		if (!search_found)
			ui_curses_display_info_msg("Pattern not found: %s", search_str ? : "");
		ui_curses_input_mode = NORMAL_MODE;
		break;
	case 127:
		backspace();
		break;
	default:
		if (ch < 0x20)
			return;
		cmdline_insert_ch(ch);
		// FIXME FALKDFpsakdfsadfasf
		search_found = search(searchable, cmdline.line, search_direction, cmdline.clen == 1);
		do_search(1);
		break;
	}
	reset_history_search();
}

void search_mode_key(int key)
{
	switch (key) {
	case KEY_DC:
		cmdline_delete_ch();
		do_search();
		break;
	case KEY_BACKSPACE:
		backspace();
		break;
	case KEY_LEFT:
		cmdline_move_left();
		return;
	case KEY_RIGHT:
		cmdline_move_right();
		return;
	case KEY_HOME:
		cmdline_move_home();
		return;
	case KEY_END:
		cmdline_move_end();
		return;
	case KEY_UP:
		{
			const char *s;

			if (history_search_text == NULL) {
				char ch = search_direction == SEARCH_FORWARD ? '/' : '?';

				if (cmdline.line[0] == ch) {
					/* //WORDS or ??WORDS */
					history_search_text = xstrdup(cmdline.line + 1);
					search_restricted = 1;
				} else {
					/* /WORDS or ?WORDS */
					history_search_text = xstrdup(cmdline.line);
					search_restricted = 0;
				}
			}
			s = history_search_forward(&search_history, history_search_text);
			if (s)
				update_search_line(s);
		}
		return;
	case KEY_DOWN:
		if (history_search_text) {
			const char *s;
			
			s = history_search_backward(&search_history, history_search_text);
			if (s) {
				update_search_line(s);
			} else {
				update_search_line(history_search_text);
			}
		}
		return;
	default:
		return;
	}
	reset_history_search();
}

void search_mode_init(void)
{
	search_history_filename = xstrjoin(cmus_cache_dir, "/ui_curses_search_history");
	history_load(&search_history, search_history_filename, 100);
}

void search_mode_exit(void)
{
	history_save(&search_history);
	free(search_history_filename);
}
