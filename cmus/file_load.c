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

#include <file_load.h>
#include <xmalloc.h>

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

int file_load(const char *filename,
		void (*handle_line)(void *data, const char *line), void *data)
{
	char *buffer;
	size_t buffer_size = 4096;
	int pos = 0;
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd == -1)
		return -1;
	buffer = xnew(char, buffer_size);
	while (1) {
		int i, start, len, rc;
		
		rc = read(fd, buffer + pos, buffer_size - pos);
		if (rc == -1) {
			int save = errno;

			free(buffer);
			close(fd);
			errno = save;
			return -1;
		}
		if (rc == 0) {
			if (pos > 0) {
				/* handle line without '\n' */
				buffer[pos] = 0;
				handle_line(data, buffer);
			}
			free(buffer);
			close(fd);
			return 0;
		}
		start = 0;
		len = pos + rc;
		i = 0;
		while (i < len) {
			if (buffer[start + i] == '\n') {
				buffer[start + i] = 0;
				handle_line(data, buffer + start);
				start += i + 1;
				len -= i + 1;
				i = 0;
			} else {
				i++;
			}
		}
		if (start == 0) {
			/* line doesn't fit into the buffer */
			buffer_size *= 2;
			buffer = xrenew(char, buffer, buffer_size);
		} else if (len) {
			/* len - start bytes left */
			memmove(buffer, buffer + start, len);
		}
		pos = len;
	}
}
