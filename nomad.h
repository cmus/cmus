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

#ifndef _NOMAD_H
#define _NOMAD_H

#include <mad.h>
#include <sys/types.h>

#define INPUT_BUFFER_SIZE	(5 * 8192)

/* default callbacks use read, lseek, close */
struct nomad_callbacks {
	ssize_t (*read)(void *datasource, void *buffer, size_t count);
	off_t (*lseek)(void *datasource, off_t offset, int whence);
	int (*close)(void *datasource);
};

/* always 16-bit signed little-endian */
struct nomad_info {
	double duration;
	int sample_rate;
	int channels;
	int nr_frames;
	int layer;
	/* -1 if fast = 1 */
	int vbr;
	/* -1 if fast = 1 */
	int avg_bitrate;
	/* -1 if file not seekable */
	int filesize;
	unsigned int joint_stereo : 1;
	unsigned int dual_channel : 1;
};

enum {
	NOMAD_ERROR_SUCCESS,
	NOMAD_ERROR_ERRNO,
	NOMAD_ERROR_FILE_FORMAT
};

struct nomad;

/* -NOMAD_ERROR_ERRNO -NOMAD_ERROR_FILE_FORMAT */
int nomad_open(struct nomad **nomadp, int fd, int fast);
int nomad_open_callbacks(struct nomad **nomadp, void *datasource, int fast,
		struct nomad_callbacks *cbs);

void nomad_close(struct nomad *nomad);
void nomad_info(struct nomad *nomad, struct nomad_info *info);

/* -NOMAD_ERROR_ERRNO */
int nomad_read(struct nomad *nomad, char *buffer, int count);

/* -NOMAD_ERROR_ERRNO */
int nomad_time_seek(struct nomad *nomad, double pos);

double nomad_time_tell(struct nomad *nomad);
double nomad_time_total(struct nomad *nomad);

#endif
