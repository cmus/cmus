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

#ifndef _REMOTE_H
#define _REMOTE_H

enum remote_command {
	CMD_PLAY, CMD_PAUSE, CMD_STOP,
	CMD_NEXT, CMD_PREV, CMD_SEEK,
	CMD_TCONT, CMD_TREPEAT, CMD_TPLAYMODE,
	CMD_PLRESHUFFLE, CMD_PLADD, CMD_PLCLEAR,
	CMD_ENQUEUE, CMD_MIX_VOL,
	CMD_MAX
};

struct remote_command_header {
	enum remote_command cmd;
	int data_size;
};

#endif
