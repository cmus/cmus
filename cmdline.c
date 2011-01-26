/*
 * Copyright 2005 Timo Hirvonen
 */

#include "cmdline.h"
#include "uchar.h"
#include "xmalloc.h"

struct cmdline cmdline;

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
	u_get_char(cmdline.line, &bpos, &ch);
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
		uchar ch;

		u_get_char(cmdline.line, &cmdline.bpos, &ch);
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
