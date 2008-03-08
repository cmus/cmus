/*
 * Copyright 2004-2005 Timo Hirvonen
 *
 * sun.c by alex <pukpuk@gmx.de>
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

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/audioio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "debug.h"
#include "op.h"
#include "sf.h"
#include "xmalloc.h"

static sample_format_t sun_sf;
static int sun_fd = -1;

static char *sun_audio_device = NULL;

static int sun_reset(void)
{
	if (ioctl(sun_fd, AUDIO_FLUSH, NULL) == -1)
		return -1;

	return 0;
}

static int sun_set_sf(sample_format_t sf)
{
	struct audio_info ainf;

	AUDIO_INITINFO(&ainf);

	sun_reset();
	sun_sf = sf;

	ainf.play.channels = sf_get_channels(sun_sf);
	ainf.play.sample_rate = sf_get_rate(sun_sf);
	ainf.play.pause = 0;
	ainf.mode = AUMODE_PLAY;

	switch (sf_get_bits(sun_sf)) {
	case 16:
		ainf.play.precision = 16;
		if (sf_get_signed(sun_sf)) {
			if (sf_get_bigendian(sun_sf))
				ainf.play.encoding = AUDIO_ENCODING_SLINEAR_BE;
			else
				ainf.play.encoding = AUDIO_ENCODING_SLINEAR_LE;
		} else {
			if (sf_get_bigendian(sun_sf))
				ainf.play.encoding = AUDIO_ENCODING_ULINEAR_BE;
			else
				ainf.play.encoding = AUDIO_ENCODING_ULINEAR_LE;
		}
		break;
	case 8:
		ainf.play.precision = 8;
		if (sf_get_signed(sun_sf))
			ainf.play.encoding = AUDIO_ENCODING_SLINEAR;
		else
			ainf.play.encoding = AUDIO_ENCODING_ULINEAR;
		break;
	default:
		d_print("sun: bits is neither 8 nor 16\n");
		return -1;
	}

	if (ioctl(sun_fd, AUDIO_SETINFO, &ainf) == -1) {
		d_print("sun: AUDIO_SETINFO failed: %s", strerror(errno));
		return -1;
	}

	if (ioctl(sun_fd, AUDIO_GETINFO, &ainf) == -1)
		return -1;
	/* XXX: check if sample rate is supported */
	d_print("sun: sample_rate: %d chanels: %d\n", ainf.play.sample_rate,
	    ainf.play.channels);

	return 0;
}

static int sun_device_exists(const char *dev)
{
	struct stat s;

	if (stat(dev, &s) == 0) {
		d_print("device %s exists\n", dev);
		return 1;
	}
	d_print("device %s does not exist\n", dev);

	return 0;
}

static int sun_init(void)
{
	const char *audio_dev = "/dev/audio";

	if (sun_audio_device != NULL) {
		if (sun_device_exists(sun_audio_device))
			return 0;
		free(sun_audio_device);
		sun_audio_device = NULL;
		return -1;
	}
	if (sun_device_exists(audio_dev)) {
		sun_audio_device = xstrdup(audio_dev);
		return 0;
	}

	return -1;
}

static int sun_exit(void)
{
	if (sun_audio_device != NULL) {
		free(sun_audio_device);
		sun_audio_device = NULL;
	}

	return 0;
}

static int sun_open(sample_format_t sf)
{
	sun_fd = open(sun_audio_device, O_WRONLY);
	if (sun_fd == -1)
		return -1;
	if (sun_set_sf(sf) == -1) {
		sun_close();
		return -1;
	}

	return 0;
}

static int sun_close(void)
{
	if (sun_fd != -1) {
		close(sun_fd);
		sun_fd = -1;
	}

	return 0;
}

static int sun_write(const char *buf, int cnt)
{
	const char *t;

	cnt /= 4;
	cnt *= 4;
	t = buf;
	while (cnt > 0) {
		int rc = write(sun_fd, buf, cnt);
		if (rc == -1) {
			if (errno == EINTR)
				continue;
			else
				return rc;
		}
		buf += rc;
		cnt -= rc;
	}

	return (buf - t);
}

static int sun_pause(void)
{
	struct audio_info ainf;

	AUDIO_INITINFO(&ainf);

	ainf.play.pause = 1;
	if (ioctl(sun_fd, AUDIO_SETINFO, &ainf) == -1)
		return -1;

	return 0;
}

static int sun_unpause(void)
{
	struct audio_info ainf;

	AUDIO_INITINFO(&ainf);

	ainf.play.pause = 0;
	if (ioctl(sun_fd, AUDIO_SETINFO, &ainf) == -1)
		return -1;

	return 0;
}

static int sun_buffer_space(void)
{
	struct audio_info ainf;
	int sp;

	AUDIO_INITINFO(&ainf);

	if (ioctl(sun_fd, AUDIO_GETINFO, &ainf) == -1)
		return -1;
	sp = ainf.play.buffer_size;

	return sp;
}

static int op_sun_set_option(int key, const char *val)
{
	switch (key) {
	case 0:
		free(sun_audio_device);
		sun_audio_device = xstrdup(val);
		break;
	default:
		return -OP_ERROR_NOT_OPTION;
	}

	return 0;
}

static int op_sun_get_option(int key, char **val)
{
	switch (key) {
	case 0:
		if (sun_audio_device)
			*val = xstrdup(sun_audio_device);
		break;
	default:
		return -OP_ERROR_NOT_OPTION;
	}

	return 0;
}

const struct output_plugin_ops op_pcm_ops = {
	.init = sun_init,
	.exit = sun_exit,
	.open = sun_open,
	.close = sun_close,
	.write = sun_write,
	.pause = sun_pause,
	.unpause = sun_unpause,
	.buffer_space = sun_buffer_space,
	.set_option = op_sun_set_option,
	.get_option = op_sun_get_option
};

const char * const op_pcm_options[] = {
	"device",
	NULL
};

const int op_priority = 0;
