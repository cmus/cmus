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

#include <file.h>
#include <xmalloc.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

ssize_t read_all(int fd, void *buf, size_t count)
{
	char *buffer = buf;
	int pos = 0;

	do {
		int rc;

		rc = read(fd, buffer + pos, count - pos);
		if (rc == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return -1;
		}
		if (rc == 0) {
			/* eof */
			break;
		}
		pos += rc;
	} while (count - pos > 0);
	return pos;
}

ssize_t write_all(int fd, const void *buf, size_t count)
{
	const char *buffer = buf;
	int count_save = count;

	do {
		int rc;

		rc = write(fd, buffer, count);
		if (rc == -1) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return -1;
		}
		buffer += rc;
		count -= rc;
	} while (count > 0);
	return count_save;
}

char **bsplit(const char *buffer, unsigned int size, char ch, unsigned int limit)
{
	char **array;
	unsigned int i, p, n = 1;

	for (i = 0; i < size && buffer[i]; i++) {
		if (buffer[i] == ch)
			n++;
	}
	if (limit && n > limit)
		n = limit;
	array = xnew(char *, n + 1);
	i = 0;
	for (p = 0; p < n - 1; p++) {
		int start = i;

		while (buffer[i] != ch)
			i++;
		array[p] = xnew(char, i - start + 1);
		memcpy(array[p], &buffer[start], i - start);
		array[p][i - start] = 0;
		i++;
	}
	array[p] = xnew(char, size - i + 1);
	memcpy(array[p], buffer + i, size - i);
	array[p++][size - i] = 0;
	array[p] = NULL;
	return array;
}

char *file_get_contents(const char *filename, int *len)
{
	char *contents;
	int fd, size, save, rc;

	*len = 0;
	fd = open(filename, O_RDONLY);
	if (fd == -1)
		return NULL;
	size = lseek(fd, 0, SEEK_END);
	if (size == -1 || lseek(fd, 0, SEEK_SET) == -1) {
		save = errno;
		close(fd);
		errno = save;
		return NULL;
	}
	contents = xnew(char, size + 1);
	rc = read_all(fd, contents, size);
	if (rc == -1) {
		save = errno;
		free(contents);
		close(fd);
		errno = save;
		return NULL;
	}
	*len = rc;
	contents[rc] = 0;
	close(fd);
	return contents;
}

char **file_get_lines(const char *filename)
{
	char **lines;
	char *contents;
	int len, i;

	contents = file_get_contents(filename, &len);
	if (contents == NULL)
		return NULL;
	if (contents[0] == 0) {
		/* empty file */
		free(contents);
		lines = xnew0(char *, 1);
		return lines;
	}
	if (len > 0 && contents[len - 1] == '\n')
		contents[--len] = 0;
	lines = bsplit(contents, len, '\n', 0);
	free(contents);

	for (i = 0; lines[i]; i++) {
		char *ptr = lines[i];

		len = strlen(ptr);
		if (len && ptr[len - 1] == '\r')
			ptr[len - 1] = 0;
	}

	return lines;
}
