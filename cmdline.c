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

#include "cmdline.h"
#include "uchar.h"
#include "xmalloc.h"

struct cmdline cmdline;

const char cmdline_word_delimiters[]     = " ";
const char cmdline_filename_delimiters[] = "/";

void cmdline_init(void)
{
	cmdline.blen = 0;
	cmdline.clen = 0;
	cmdline.bpos = 0;
	cmdline.cpos = 0;
	cmdline.size = 128;
	cmdline.line = xnew(char, cmdline.size);
	cmdline.line[0] = 0;
}

void cmdline_insert_ch(uchar ch)
{
	int size;

	size = u_char_size(ch);
	if (cmdline.blen + size > cmdline.size) {
		cmdline.size *= 2;
		cmdline.line = xrenew(char, cmdline.line, cmdline.size);
	}
	memmove(cmdline.line + cmdline.bpos + size,
		cmdline.line + cmdline.bpos,
		cmdline.blen - cmdline.bpos + 1);
	u_set_char_raw(cmdline.line, &cmdline.bpos, ch);
	cmdline.cpos++;
	cmdline.blen += size;
	cmdline.clen++;
}

void cmdline_backspace(void)
{
	int bpos, size;

	if (cmdline.bpos == 0)
		return;

	bpos = cmdline.bpos;
	u_prev_char_pos(cmdline.line, &bpos);
	size = cmdline.bpos - bpos;

	memmove(cmdline.line + bpos,
		cmdline.line + cmdline.bpos,
		cmdline.blen - cmdline.bpos + 1);
	cmdline.bpos -= size;
	cmdline.cpos--;
	cmdline.blen -= size;
	cmdline.clen--;
}

void cmdline_backspace_to_bol(void)
{
	while (cmdline.bpos)
		cmdline_backspace();
}

void cmdline_delete_ch(void)
{
	uchar ch;
	int size, bpos;

	if (cmdline.bpos == cmdline.blen)
		return;
	bpos = cmdline.bpos;
	ch = u_get_char(cmdline.line, &bpos);
	size = u_char_size(ch);
	cmdline.blen -= size;
	cmdline.clen--;
	memmove(cmdline.line + cmdline.bpos,
		cmdline.line + cmdline.bpos + size,
		cmdline.blen - cmdline.bpos + 1);
}

void cmdline_set_text(const char *text)
{
	int len = strlen(text);

	if (len >= cmdline.size) {
		while (len >= cmdline.size)
			cmdline.size *= 2;
		cmdline.line = xrenew(char, cmdline.line, cmdline.size);
	}
	memcpy(cmdline.line, text, len + 1);
	cmdline.cpos = u_strlen_safe(cmdline.line);
	cmdline.bpos = len;
	cmdline.clen = cmdline.cpos;
	cmdline.blen = len;
}

void cmdline_clear(void)
{
	cmdline.blen = 0;
	cmdline.clen = 0;
	cmdline.bpos = 0;
	cmdline.cpos = 0;
	cmdline.line[0] = 0;
}

void cmdline_clear_end(void)
{
	cmdline.line[cmdline.bpos] = 0;

	cmdline.clen = u_strlen_safe(cmdline.line);
	cmdline.blen = strlen(cmdline.line);
}

void cmdline_move_left(void)
{
	if (cmdline.bpos > 0) {
		cmdline.cpos--;
		u_prev_char_pos(cmdline.line, &cmdline.bpos);
	}
}

void cmdline_move_right(void)
{
	if (cmdline.bpos < cmdline.blen) {
		u_get_char(cmdline.line, &cmdline.bpos);
		cmdline.cpos++;
	}
}

void cmdline_move_home(void)
{
	cmdline.cpos = 0;
	cmdline.bpos = 0;
}

void cmdline_move_end(void)
{
	cmdline.cpos = cmdline.clen;
	cmdline.bpos = cmdline.blen;
}

static int next_word(const char *str, int bpos, int *cdiff, const char *delim, int direction)
{
	int skip_delim = 1;
	while ((direction > 0) ? str[bpos] : (bpos > 0)) {
		uchar ch;
		int oldp = bpos;

		if (direction > 0) {
			ch = u_get_char(str, &bpos);
		} else {
			u_prev_char_pos(str, &bpos);
			oldp = bpos;
			ch = u_get_char(str, &oldp);
		}

		if (u_strchr(delim, ch)) {
			if (!skip_delim) {
				bpos -= bpos - oldp;
				break;
			}
		} else
			skip_delim = 0;

		*cdiff += direction;
	}
	return bpos;
}

void cmdline_forward_word(const char *delim)
{
	cmdline.bpos = next_word(cmdline.line, cmdline.bpos, &cmdline.cpos, delim, +1);
}

void cmdline_backward_word(const char *delim)
{
	cmdline.bpos = next_word(cmdline.line, cmdline.bpos, &cmdline.cpos, delim, -1);
}

void cmdline_delete_word(const char *delim)
{
	int bpos, cdiff = 0;

	bpos = next_word(cmdline.line, cmdline.bpos, &cdiff, delim, +1);

	memmove(cmdline.line + cmdline.bpos,
		cmdline.line + bpos,
		cmdline.blen - bpos + 1);
	cmdline.blen -= bpos - cmdline.bpos;
	cmdline.clen -= cdiff;
}

void cmdline_backward_delete_word(const char *delim)
{
	int bpos, cdiff = 0;

	bpos = next_word(cmdline.line, cmdline.bpos, &cdiff, delim, -1);

	cmdline.blen += bpos - cmdline.bpos;
	memmove(cmdline.line + bpos,
		cmdline.line + cmdline.bpos,
		cmdline.blen - bpos + 1);
	cmdline.bpos = bpos;
	cmdline.clen += cdiff;
	cmdline.cpos += cdiff;
}
