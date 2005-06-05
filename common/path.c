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

#include <path.h>
#include <xmalloc.h>

#include <stdlib.h>
#include <unistd.h>

void path_strip(char *str)
{
	int i, s, d;

	i = 0;
	if (str[0] == '/')
		i = 1;
	while (str[i]) {
		if (str[i] == '/') {
			d = i;
			s = i + 1;
			while (str[s] && str[s] == '/')
				s++;
			s--;
			do {
				str[d++] = str[++s];
			} while (str[s]);
		} else if (i && str[i] == '.') {
			if (str[i + 1] == '/') {
				d = i;
				s = i + 1;
				do {
					str[d++] = str[++s];
				} while (str[s]);
			} else if (str[i + 1] == 0) {
				str[i] = 0;
				break;
			} else if (str[i + 1] == '.' &&
				  (str[i + 2] == '/' || str[i + 2] == 0)) {
				/* aaa/bbb/../ccc */
				/* aaa/ccc */
				if (str[i + 2]) {
					s = i + 3; /* ccc */
				} else {
					s = i + 2;
				}
				d = i - 1; /* /../ccc */
				do {
					if (d == 0)
						break;
					d--;
				} while (str[d] != '/');
				d++;
				/* std[d] is bbb/../ccc */
				i = d;
				s--;
				do {
					str[d++] = str[++s];
				} while (str[s]);
			} else {
				while (str[i] && str[i] != '/')
					i++;
				if (str[i])
					i++;
			}
		} else {
			while (str[i] && str[i] != '/')
				i++;
			if (str[i])
				i++;
		}
	}
	if (i > 1 && str[i - 1] == '/')
		str[i - 1] = 0;
}

char *path_absolute(const char *src)
{
	char *str;

	if (src[0] == '/') {
		/* already absolute */
		str = xstrdup(src);
	} else {
		char cwd[1024];
		int src_len;
		int cwd_len;

		if (getcwd(cwd, sizeof(cwd)) == NULL)
			return NULL;
		src_len = strlen(src);
		cwd_len = strlen(cwd);
		str = xnew(char, cwd_len + 1 + src_len + 1);
		memcpy(str, cwd, cwd_len);
		str[cwd_len] = '/';
		memcpy(str + cwd_len + 1, src, src_len + 1);
	}
	path_strip(str);
	return str;
}
