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

#ifndef CMUS_MISC_H
#define CMUS_MISC_H

#include <stddef.h>

extern const char *cmus_config_dir;
extern const char *cmus_playlist_dir;
extern const char *cmus_socket_path;
extern const char *cmus_data_dir;
extern const char *cmus_lib_dir;
extern const char *home_dir;
extern const char *cmus_albumart_dir;

char **get_words(const char *text);
int strptrcmp(const void *a, const void *b);
int strptrcoll(const void *a, const void *b);
int misc_init(void);
const char *escape(const char *str);
const char *unescape(const char *str);
const char *get_filename(const char *path);

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
void shuffle_array(void *array, size_t n, size_t size);

// taken from https://github.com/dnmfarrell/URI-Encode-C
#define _______ "\0\0\0\0"
size_t uri_encode(const char *src, const size_t len, char *dst);

// taken from https://nachtimwald.com/2017/11/18/base64-encode-and-decode-in-c/
int b64_decode(const char *in, char **out, int *out_len);

#define swap_endianness(num) ((num >> 24) & 0xff) | ((num << 8) & 0xff0000) | ((num >> 8) & 0xff00) | ((num << 24) & 0xff000000)

#endif
