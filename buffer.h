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

#ifndef CMUS_BUFFER_H
#define CMUS_BUFFER_H

/*
 * must be a multiple of any supported frame size
 *
 * 12 is the LCM of 1, 2, 3 and 4, which corresponds to
 * 8, 16, 24 and 32 bits respectively
 *
 * 840 is the LCM of 1, 2, 3, 4, 5, 6, 7 and 8, which
 * are the numbers of supported channels
 *
 * we used to define the value as 60 * 1024 = 61440
 * hence the extra 6, which makes the new value 60480
 */
#define CHUNK_SIZE (12 * 840 * 6)

extern unsigned int buffer_nr_chunks;

void buffer_init(void);
void buffer_free(void);
int buffer_get_rpos(char **pos);
int buffer_get_wpos(char **pos);
void buffer_consume(int count);
int buffer_fill(int count);
void buffer_reset(void);
int buffer_get_filled_chunks(void);

#endif
