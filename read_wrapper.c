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

#include "read_wrapper.h"
#include "ip.h"
#include "file.h"

#include <unistd.h>

ssize_t read_wrapper(struct input_plugin_data *ip_data, void *buffer, size_t count)
{
	int rc;

	if (ip_data->metaint == 0) {
		/* no metadata in the stream */
		return read(ip_data->fd, buffer, count);
	}

	if (ip_data->counter == ip_data->metaint) {
		/* read metadata */
		unsigned char byte;
		int len;

		rc = read(ip_data->fd, &byte, 1);
		if (rc == -1)
			return -1;
		if (rc == 0)
			return 0;
		if (byte != 0) {
			len = ((int)byte) * 16;
			ip_data->metadata[0] = 0;
			rc = read_all(ip_data->fd, ip_data->metadata, len);
			if (rc == -1)
				return -1;
			if (rc < len) {
				ip_data->metadata[0] = 0;
				return 0;
			}
			ip_data->metadata[len] = 0;
			ip_data->metadata_changed = 1;
		}
		ip_data->counter = 0;
	}
	if (count + ip_data->counter > ip_data->metaint)
		count = ip_data->metaint - ip_data->counter;
	rc = read(ip_data->fd, buffer, count);
	if (rc > 0)
		ip_data->counter += rc;
	return rc;
}
