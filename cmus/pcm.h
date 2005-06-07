/* 
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _PCM_H
#define _PCM_H

extern void convert_u8_1ch_to_s16_2ch(char *dst, const char *src, int count);
extern void convert_s8_1ch_to_s16_2ch(char *dst, const char *src, int count);
extern void convert_u8_2ch_to_s16_2ch(char *dst, const char *src, int count);
extern void convert_s8_2ch_to_s16_2ch(char *dst, const char *src, int count);

extern void convert_u16_le_to_s16_le(char *buf, int count);
extern void convert_u16_be_to_s16_le(char *buf, int count);
extern void convert_s16_be_to_s16_le(char *buf, int count);

extern void convert_16_1ch_to_16_2ch(char *dst, const char *src, int count);

#endif
