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

#include "search_mode.h"
#include "cmdline.h"
#include "history.h"
#include "ui_curses.h"
#include "search.h"
#include "xmalloc.h"
#include "xstrjoin.h"
#include "misc.h"
#include "lib.h"

#include <ctype.h>

#if defined(__sun__)
#include <ncurses.h>
#else
#include <curses.h>
#endif

/* this is set in ui_curses.c */
enum search_direction search_direction = SEARCH_FORWARD;

/* current search string, this is set _only_ when user presses enter
 * this string is used when 'n' or 'N' is pressed
 * incremental search does not use this, it uses cmdline.line directly
 */
char *search_str = NULL;
int search_restricted = 0;

static struct history search_history;
static char *search_history_filename;
static char *history_search_text = NULL;

static void update_search_line(const char *text, int restricted)
{
	int len = strlen(text);
	char ch = search_direction == SEARCH_FORWARD ? '/' : '?';
	char *buf, *ptr;

	buf = xnew(char, len + 2);
	ptr = buf;
	if (restricted)
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

static void parse_line(const char **text, int *restricted)
{
	char ch = search_direction == SEARCH_FORWARD ? '/' : '?';
	int r = 0;

	if (cmdline.line[0] == ch) {
		/* //WORDS or ??WORDS */
		r = 1;
	}
	*text = cmdline.line + r;
	*restricted = r;
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
	} else {
		input_mode = NORMAL_MODE;
	}
}

static void delete(void)
{
	/* save old value */
	int restricted = search_restricted;
	const char *text;

	cmdline_delete_ch();
	parse_line(&text, &search_restricted);
	if (text[0])
		search(searchable, text, search_direction, 0);

	/* restore old value */
	search_restricted = restricted;
}

void search_text(const char *text, int restricted, int beginning)
{
	if (text[0] == 0) {
		/* cmdline is "/", "?", "//" or "??" */
		if (search_str) {
			/* use old search string */
			search_restricted = restricted;
			if (!search_next(searchable, search_str, search_direction))
				search_not_found();
		}
	} else {
		/* set new search string and add it to the history */
		free(search_str);
		search_str = xstrdup(text);
		history_add_line(&search_history, text);

		/* search not yet done if up or down arrow was pressed */
		search_restricted = restricted;
		if (!search(searchable, search_str, search_direction, beginning))
			search_not_found();
	}
}

void search_mode_ch(uchar ch)
{
	const char *text;
	int restricted;

	switch (ch) {
	case 0x01: // ^A
		cmdline_move_home();
		break;
	case 0x02: // ^B
		cmdline_move_left();
		break;
	case 0x04: // ^D
		delete();
		break;
	case 0x05: // ^E
		cmdline_move_end();
		break;
	case 0x06: // ^F
		cmdline_move_right();
		break;
	case 0x03: // ^C
	case 0x07: // ^G
	case 0x1B: // ESC
		parse_line(&text, &restricted);
		if (text[0]) {
			history_add_line(&search_history, text);
			cmdline_clear();
		}
		input_mode = NORMAL_MODE;
		break;
	case 0x0A:
		parse_line(&text, &restricted);
		search_text(text, restricted, 0);
		cmdline_clear();
		input_mode = NORMAL_MODE;
		break;
	case 0x0B:
		cmdline_clear_end();
		break;
	case 0x15:
		cmdline_backspace_to_bol();
		break;
	case 0x17: // ^W
		cmdline_backward_delete_word(cmdline_word_delimiters);
		break;
	case 0x08: // ^H
	case 127:
		backspace();
		break;
	default:
		if (ch < 0x20) {
			return;
		} else {
			/* start from beginning if this is first char */
			int beginning = search_line_empty();

			/* save old value
			 *
			 * don't set search_{str,restricted} here because
			 * search can be cancelled by pressing ESC
			 */
			restricted = search_restricted;

			cmdline_insert_ch(ch);
			parse_line(&text, &search_restricted);
			search(searchable, text, search_direction, beginning);

			/* restore old value */
			search_restricted = restricted;
		}
		break;
	}
	reset_history_search();
}

void search_mode_escape(int c)
{
	switch (c) {
	case 98:
		cmdline_backward_word(cmdline_filename_delimiters);
		break;
	case 100:
		cmdline_delete_word(cmdline_filename_delimiters);
		break;
	case 102:
		cmdline_forward_word(cmdline_filename_delimiters);
		break;
	case 127:
	case KEY_BACKSPACE:
		cmdline_backward_delete_word(cmdline_filename_delimiters);
		break;
	}
	reset_history_search();
}

void search_mode_key(int key)
{
	const char *text;
	int restricted;

	switch (key) {
	case KEY_DC:
		delete();
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
		parse_line(&text, &restricted);
		if (history_search_text == NULL)
			history_search_text = xstrdup(text);
		text = history_search_forward(&search_history, history_search_text);
		if (text)
			update_search_line(text, restricted);
		return;
	case KEY_DOWN:
		if (history_search_text) {
			parse_line(&text, &restricted);
			text = history_search_backward(&search_history, history_search_text);
			if (text) {
				update_search_line(text, restricted);
			} else {
				update_search_line(history_search_text, restricted);
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
	search_history_filename = xstrjoin(cmus_config_dir, "/search-history");
	history_load(&search_history, search_history_filename, 100);
}

void search_mode_exit(void)
{
	history_save(&search_history);
	history_free(&search_history);
	free(search_history_filename);
}
