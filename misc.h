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

#ifndef _MISC_H
#define _MISC_H

extern const char *cmus_config_dir;
extern const char *cmus_socket_path;
extern const char *home_dir;
extern const char *user_name;

char **get_words(const char *text);
int strptrcmp(const void *a, const void *b);
int strptrcoll(const void *a, const void *b);
int misc_init(void);
const char *escape(const char *str);
const char *unescape(const char *str);

/*
 * @field   contains Replay Gain data format in bit representation
 * @gain    pointer where to store gain value * 10
 *
 * Returns 0 if @field doesn't contain a valid gain value,
 *         1 for track (= radio) adjustment
 *         2 for album (= audiophile) adjustment
 *
 * http://replaygain.hydrogenaudio.org/rg_data_format.html
 */
int replaygain_decode(unsigned int field, int *gain);

char *expand_filename(const char *name);

#endif
