/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2004-2005 Timo Hirvonen
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

#include "misc.h"
#include "prog.h"
#include "xmalloc.h"
#include "xstrjoin.h"
#include "ui_curses.h"
#include "config/libdir.h"
#include "config/datadir.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdarg.h>
#include <pwd.h>

const char *cmus_config_dir = NULL;
const char *cmus_playlist_dir = NULL;
const char *cmus_socket_path = NULL;
const char *cmus_data_dir = NULL;
const char *cmus_lib_dir = NULL;
const char *home_dir = NULL;
const char *cmus_albumart_dir = NULL;

char **get_words(const char *text)
{
	char **words;
	int i, j, count;

	while (*text == ' ')
		text++;

	count = 0;
	i = 0;
	while (text[i]) {
		count++;
		while (text[i] && text[i] != ' ')
			i++;
		while (text[i] == ' ')
			i++;
	}
	words = xnew(char *, count + 1);

	i = 0;
	j = 0;
	while (text[i]) {
		int start = i;

		while (text[i] && text[i] != ' ')
			i++;
		words[j++] = xstrndup(text + start, i - start);
		while (text[i] == ' ')
			i++;
	}
	words[j] = NULL;
	return words;
}

int strptrcmp(const void *a, const void *b)
{
	const char *as = *(char **)a;
	const char *bs = *(char **)b;

	return strcmp(as, bs);
}

int strptrcoll(const void *a, const void *b)
{
	const char *as = *(char **)a;
	const char *bs = *(char **)b;

	return strcoll(as, bs);
}

const char *escape(const char *str)
{
	static char *buf = NULL;
	static size_t alloc = 0;
	size_t len = strlen(str);
	size_t need = len * 2 + 1;
	int s, d;

	if (need > alloc) {
		alloc = (need + 16) & ~(16 - 1);
		buf = xrealloc(buf, alloc);
	}

	d = 0;
	for (s = 0; str[s]; s++) {
		if (str[s] == '\\') {
			buf[d++] = '\\';
			buf[d++] = '\\';
			continue;
		}
		if (str[s] == '\n') {
			buf[d++] = '\\';
			buf[d++] = 'n';
			continue;
		}
		buf[d++] = str[s];
	}
	buf[d] = 0;
	return buf;
}

const char *unescape(const char *str)
{
	static char *buf = NULL;
	static size_t alloc = 0;
	size_t need = strlen(str) + 1;
	int do_escape = 0;
	int s, d;

	if (need > alloc) {
		alloc = (need + 16) & ~(16 - 1);
		buf = xrealloc(buf, alloc);
	}

	d = 0;
	for (s = 0; str[s]; s++) {
		if (!do_escape && str[s] == '\\')
			do_escape = 1;
		else {
			buf[d++] = (do_escape && str[s] == 'n') ? '\n' : str[s];
			do_escape = 0;
		}
	}
	buf[d] = 0;
	return buf;
}

static int dir_exists(const char *dirname)
{
	DIR *dir;

	dir = opendir(dirname);
	if (dir == NULL) {
		if (errno == ENOENT)
			return 0;
		return -1;
	}
	closedir(dir);
	return 1;
}

static void make_dir(const char *dirname)
{
	int rc;

	rc = dir_exists(dirname);
	if (rc == 1)
		return;
	if (rc == -1)
		die_errno("error: opening `%s'", dirname);
	rc = mkdir(dirname, 0700);
	if (rc == -1)
		die_errno("error: creating directory `%s'", dirname);
}

static char *get_non_empty_env(const char *name)
{
	const char *val;

	val = getenv(name);
	if (val == NULL || val[0] == 0)
		return NULL;
	return xstrdup(val);
}

const char *get_filename(const char *path)
{
	const char *file = strrchr(path, '/');
	if (!file)
		file = path;
	else
		file++;
	if (!*file)
		return NULL;
	return file;
}

static void move_old_playlist(void)
{
	char *default_playlist = xstrjoin(cmus_playlist_dir, "/default");
	char *old_playlist = xstrjoin(cmus_config_dir, "/playlist.pl");
	int rc = rename(old_playlist, default_playlist);
	if (rc && errno != ENOENT)
		die_errno("error: unable to move %s to playlist directory",
				old_playlist);
	free(default_playlist);
	free(old_playlist);
}

int misc_init(void)
{
	char *xdg_runtime_dir = get_non_empty_env("XDG_RUNTIME_DIR");

	home_dir = get_non_empty_env("HOME");
	if (home_dir == NULL)
		die("error: environment variable HOME not set\n");

	cmus_config_dir = get_non_empty_env("CMUS_HOME");
	if (cmus_config_dir == NULL) {
		char *cmus_home = xstrjoin(home_dir, "/.cmus");
		int cmus_home_exists = dir_exists(cmus_home);

		if (cmus_home_exists == 1) {
			cmus_config_dir = xstrdup(cmus_home);
		} else if (cmus_home_exists == -1) {
			die_errno("error: opening `%s'", cmus_home);
		} else {
			char *xdg_config_home = get_non_empty_env("XDG_CONFIG_HOME");
			if (xdg_config_home == NULL) {
				xdg_config_home = xstrjoin(home_dir, "/.config");
			}

			make_dir(xdg_config_home);
			cmus_config_dir = xstrjoin(xdg_config_home, "/cmus");

			free(xdg_config_home);
		}

		free(cmus_home);
	}
	make_dir(cmus_config_dir);

	cmus_playlist_dir = get_non_empty_env("CMUS_PLAYLIST_DIR");
	if (!cmus_playlist_dir)
		cmus_playlist_dir = xstrjoin(cmus_config_dir, "/playlists");

	int playlist_dir_is_new = dir_exists(cmus_playlist_dir) == 0;
	make_dir(cmus_playlist_dir);
	if (playlist_dir_is_new)
		move_old_playlist();

	cmus_socket_path = get_non_empty_env("CMUS_SOCKET");
	if (cmus_socket_path == NULL) {
		if (xdg_runtime_dir == NULL) {
			cmus_socket_path = xstrjoin(cmus_config_dir, "/socket");
		} else {
			cmus_socket_path = xstrjoin(xdg_runtime_dir, "/cmus-socket");
		}
	}

	cmus_lib_dir = getenv("CMUS_LIB_DIR");
	if (!cmus_lib_dir)
		cmus_lib_dir = LIBDIR "/cmus";

	cmus_data_dir = getenv("CMUS_DATA_DIR");
	if (!cmus_data_dir)
		cmus_data_dir = DATADIR "/cmus";

	cmus_albumart_dir = get_non_empty_env("CMUS_ALBUMART_DIR");
	if (!cmus_albumart_dir)
		cmus_albumart_dir = xstrjoin(cmus_config_dir, "/albumart");

	int albumart_dir_is_new = dir_exists(cmus_albumart_dir) == 0;
	if (albumart_dir_is_new) {
		make_dir(cmus_albumart_dir);
	}

	free(xdg_runtime_dir);
	return 0;
}

int replaygain_decode(unsigned int field, int *gain)
{
	unsigned int name_code, originator_code, sign_bit, val;

	name_code = (field >> 13) & 0x7;
	if (!name_code || name_code > 2)
		return 0;
	originator_code = (field >> 10) & 0x7;
	if (!originator_code)
		return 0;
	sign_bit = (field >> 9) & 0x1;
	val = field & 0x1ff;
	if (sign_bit && !val)
		return 0;
	*gain = (sign_bit ? -1 : 1) * val;
	return name_code;
}

static char *get_home_dir(const char *username)
{
	struct passwd *passwd;

	if (username == NULL)
		return xstrdup(home_dir);
	passwd = getpwnam(username);
	if (passwd == NULL)
		return NULL;
	/* don't free passwd */
	return xstrdup(passwd->pw_dir);
}

char *expand_filename(const char *name)
{
	if (name[0] == '~') {
		char *slash;

		slash = strchr(name, '/');
		if (slash) {
			char *username, *home;

			if (slash - name - 1 > 0) {
				/* ~user/... */
				username = xstrndup(name + 1, slash - name - 1);
			} else {
				/* ~/... */
				username = NULL;
			}
			home = get_home_dir(username);
			free(username);
			if (home) {
				char *expanded;

				expanded = xstrjoin(home, slash);
				free(home);
				return expanded;
			} else {
				return xstrdup(name);
			}
		} else {
			if (name[1] == 0) {
				return xstrdup(home_dir);
			} else {
				char *home;

				home = get_home_dir(name + 1);
				if (home)
					return home;
				return xstrdup(name);
			}
		}
	} else {
		return xstrdup(name);
	}
}

void shuffle_array(void *array, size_t n, size_t size)
{
	char tmp[size];
	char *arr = array;
	for (ssize_t i = 0; i < (ssize_t)n - 1; ++i) {
		size_t rnd = (size_t) rand();
		size_t j = i + rnd / (RAND_MAX / (n - i) + 1);
		memcpy(tmp, arr + j * size, size);
		memcpy(arr + j * size, arr + i * size, size);
		memcpy(arr + i * size, tmp, size);
	}
}

// taken from https://github.com/dnmfarrell/URI-Encode-C
static const char uri_encode_tbl[sizeof(signed int) * 0x100] = {
/*  0       1       2       3       4       5       6       7       8       9       a       b       c       d       e       f                        */
    "%00\0" "%01\0" "%02\0" "%03\0" "%04\0" "%05\0" "%06\0" "%07\0" "%08\0" "%09\0" "%0A\0" "%0B\0" "%0C\0" "%0D\0" "%0E\0" "%0F\0"  /* 0:   0 ~  15 */
    "%10\0" "%11\0" "%12\0" "%13\0" "%14\0" "%15\0" "%16\0" "%17\0" "%18\0" "%19\0" "%1A\0" "%1B\0" "%1C\0" "%1D\0" "%1E\0" "%1F\0"  /* 1:  16 ~  31 */
    "%20\0" "%21\0" "%22\0" "%23\0" "%24\0" "%25\0" "%26\0" "%27\0" "%28\0" "%29\0" "%2A\0" "%2B\0" "%2C\0" _______ _______ _______  /* 2:  32 ~  47 */
    _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ "%3A\0" "%3B\0" "%3C\0" "%3D\0" "%3E\0" "%3F\0"  /* 3:  48 ~  63 */
    "%40\0" _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______  /* 4:  64 ~  79 */
    _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ "%5B\0" "%5C\0" "%5D\0" "%5E\0" _______  /* 5:  80 ~  95 */
    "%60\0" _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______  /* 6:  96 ~ 111 */
    _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ _______ "%7B\0" "%7C\0" "%7D\0" _______ "%7F\0"  /* 7: 112 ~ 127 */
    "%80\0" "%81\0" "%82\0" "%83\0" "%84\0" "%85\0" "%86\0" "%87\0" "%88\0" "%89\0" "%8A\0" "%8B\0" "%8C\0" "%8D\0" "%8E\0" "%8F\0"  /* 8: 128 ~ 143 */
    "%90\0" "%91\0" "%92\0" "%93\0" "%94\0" "%95\0" "%96\0" "%97\0" "%98\0" "%99\0" "%9A\0" "%9B\0" "%9C\0" "%9D\0" "%9E\0" "%9F\0"  /* 9: 144 ~ 159 */
    "%A0\0" "%A1\0" "%A2\0" "%A3\0" "%A4\0" "%A5\0" "%A6\0" "%A7\0" "%A8\0" "%A9\0" "%AA\0" "%AB\0" "%AC\0" "%AD\0" "%AE\0" "%AF\0"  /* A: 160 ~ 175 */
    "%B0\0" "%B1\0" "%B2\0" "%B3\0" "%B4\0" "%B5\0" "%B6\0" "%B7\0" "%B8\0" "%B9\0" "%BA\0" "%BB\0" "%BC\0" "%BD\0" "%BE\0" "%BF\0"  /* B: 176 ~ 191 */
    "%C0\0" "%C1\0" "%C2\0" "%C3\0" "%C4\0" "%C5\0" "%C6\0" "%C7\0" "%C8\0" "%C9\0" "%CA\0" "%CB\0" "%CC\0" "%CD\0" "%CE\0" "%CF\0"  /* C: 192 ~ 207 */
    "%D0\0" "%D1\0" "%D2\0" "%D3\0" "%D4\0" "%D5\0" "%D6\0" "%D7\0" "%D8\0" "%D9\0" "%DA\0" "%DB\0" "%DC\0" "%DD\0" "%DE\0" "%DF\0"  /* D: 208 ~ 223 */
    "%E0\0" "%E1\0" "%E2\0" "%E3\0" "%E4\0" "%E5\0" "%E6\0" "%E7\0" "%E8\0" "%E9\0" "%EA\0" "%EB\0" "%EC\0" "%ED\0" "%EE\0" "%EF\0"  /* E: 224 ~ 239 */
    "%F0\0" "%F1\0" "%F2\0" "%F3\0" "%F4\0" "%F5\0" "%F6\0" "%F7\0" "%F8\0" "%F9\0" "%FA\0" "%FB\0" "%FC\0" "%FD\0" "%FE\0" "%FF"    /* F: 240 ~ 255 */
};
#undef _______

size_t uri_encode(const char *src, const size_t len, char *dst)
{
	size_t i = 0, j = 0;
	while (i < len)
	{
		const char octet = src[i++];
		const int32_t code = ((int32_t *) uri_encode_tbl) [(unsigned char) octet];
		if (code) {
			*((int32_t *) &dst[j]) = code;
			j += 3;
		}
		else dst[j++] = octet;
	}
	dst[j] = '\0';
	return j;
}

// taken from https://nachtimwald.com/2017/11/18/base64-encode-and-decode-in-c/
static int b64invs[] = { 62, -1, -1, -1, 63, 52, 53, 54, 55, 56, 57, 58,
	59, 60, 61, -1, -1, -1, -1, -1, -1, -1, 0, 1, 2, 3, 4, 5,
	6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
	21, 22, 23, 24, 25, -1, -1, -1, -1, -1, -1, 26, 27, 28,
	29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42,
	43, 44, 45, 46, 47, 48, 49, 50, 51 };

static int b64_isvalidchar(char c)
{
	if (c >= '0' && c <= '9')
		return 1;
	if (c >= 'A' && c <= 'Z')
		return 1;
	if (c >= 'a' && c <= 'z')
		return 1;
	if (c == '+' || c == '/' || c == '=')
		return 1;
	return 0;
}

static size_t b64_decoded_size(const char *in)
{
	size_t len;
	size_t ret;
	size_t i;

	if (in == NULL)
		return 0;

	len = strlen(in);
	ret = len / 4 * 3;

	for (i = len; i-- > 0; ) {
		if (in[i] == '=')
			ret--;
		else
			break;
	}

	return ret;
}

int b64_decode(const char *in, char **out, int *out_len)
{
	size_t len, i, j;
	int v;

	if (in == NULL || out == NULL || out_len == NULL)
		return 0;

	len = strlen(in);
	if (len % 4 != 0)
		return 0;

	*out_len = b64_decoded_size(in);
	*out = xnew(char, *out_len);

	for (i = 0; i < len; i++) {
		if (!b64_isvalidchar(in[i])) {
			free(*out);
			return 0;
		}
	}

	for (i = 0, j = 0; i < len; i += 4, j += 3) {
		v = b64invs[in[i] - 43];
		v = (v << 6) | b64invs[in[i+1]-43];
		v = in[i + 2] == '=' ? v << 6 : (v << 6) | b64invs[in[i + 2] - 43];
		v = in[i + 3] == '=' ? v << 6 : (v << 6) | b64invs[in[i + 3] - 43];

		(*out)[j] = (v >> 16) & 0xFF;
		if (in[i + 2] != '=')
			(*out)[j + 1] = (v >> 8) & 0xFF;
		if (in[i + 3] != '=')
			(*out)[j + 2] = v & 0xFF;
	}

	return 1;
}
