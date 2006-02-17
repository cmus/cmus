/* 
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <op.h>
#include <sf.h>
#include <xmalloc.h>
#include <debug.h>

#if defined(__OpenBSD__)
#include <soundcard.h>
#else
#include <sys/soundcard.h>
#endif
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

static sample_format_t oss_sf;
static int oss_fd = -1;

/* configuration */
static char *oss_dsp_device = NULL;

static int oss_close(void);

static int oss_reset(void)
{
	if (fcntl(oss_fd, SNDCTL_DSP_RESET, 0) == -1) {
		return -1;
	}
	return 0;
}

static int oss_set_sf(sample_format_t sf)
{
	int tmp, log2_fragment_size, nr_fragments, bytes_per_second;

	oss_reset();
	oss_sf = sf;
	tmp = sf_get_channels(oss_sf) - 1;
	if (ioctl(oss_fd, SNDCTL_DSP_STEREO, &tmp) == -1) {
		d_print("SNDCTL_DSP_STEREO failed: %s\n", strerror(errno));
		return -1;
	}
	if (sf_get_bits(oss_sf) == 16) {
		if (sf_get_signed(oss_sf)) {
			if (sf_get_bigendian(oss_sf)) {
				tmp = AFMT_S16_BE;
			} else {
				tmp = AFMT_S16_LE;
			}
		} else {
			if (sf_get_bigendian(oss_sf)) {
				tmp = AFMT_U16_BE;
			} else {
				tmp = AFMT_U16_LE;
			}
		}
	} else if (sf_get_bits(oss_sf) == 8) {
		if (sf_get_signed(oss_sf)) {
			tmp = AFMT_S8;
		} else {
			tmp = AFMT_U8;
		}
	} else {
		d_print("bits is neither 8 nor 16\n");
		return -1;
	}
	if (ioctl(oss_fd, SNDCTL_DSP_SAMPLESIZE, &tmp) == -1) {
		d_print("SNDCTL_DSP_SAMPLESIZE failed: %s\n", strerror(errno));
		return -1;
	}
	tmp = sf_get_rate(oss_sf);
	if (ioctl(oss_fd, SNDCTL_DSP_SPEED, &tmp) == -1) {
		d_print("SNDCTL_DSP_SPEED failed: %s\n", strerror(errno));
		return -1;
	}
#if 1
	bytes_per_second = sf_get_second_size(oss_sf);
	log2_fragment_size = 0;
	while (1 << log2_fragment_size < bytes_per_second / 25)
		log2_fragment_size++;
	log2_fragment_size--;
	nr_fragments = 32;
/* 	device_buffer_size = (1 << log2_fragment_size) * (nr_fragments + 1); */
	/* bits 0..15 = size of fragment, 16..31 = log2(number of fragments) */
	tmp = (nr_fragments << 16) + log2_fragment_size;
	d_print("fragment: %d\n", tmp);
	if (ioctl(oss_fd, SNDCTL_DSP_SETFRAGMENT, &tmp) == -1) {
		d_print("fragment: %d\n", tmp);
		d_print("SNDCTL_DSP_SETFRAGMENT failed: %s\n", strerror(errno));
		return -1;
	}
#endif
	return 0;
}

static int oss_device_exists(const char *device)
{
	struct stat s;

	if (stat(device, &s) == 0) {
		d_print("device %s exists\n", device);
		return 1;
	}
	d_print("device %s does not exist\n", device);
	return 0;
}

static int oss_init(void)
{
	const char *new_dsp_dev = "/dev/sound/dsp";
	const char *dsp_dev = "/dev/dsp";

	if (oss_dsp_device) {
		if (oss_device_exists(oss_dsp_device))
			return 0;
		free(oss_dsp_device);
		oss_dsp_device = NULL;
		return -1;
	}
	if (oss_device_exists(new_dsp_dev)) {
		oss_dsp_device = xstrdup(new_dsp_dev);
		return 0;
	}
	if (oss_device_exists(dsp_dev)) {
		oss_dsp_device = xstrdup(dsp_dev);
		return 0;
	}
	return -1;
}

static int oss_exit(void)
{
	free(oss_dsp_device);
	oss_dsp_device = NULL;
	return 0;
}

static int oss_open(sample_format_t sf)
{
	oss_fd = open(oss_dsp_device, O_WRONLY);
	if (oss_fd == -1)
		return -1;
	if (oss_set_sf(sf) == -1) {
		oss_close();
		return -1;
	}
	return 0;
}

static int oss_close(void)
{
	close(oss_fd);
	oss_fd = -1;
	return 0;
}

static int oss_write(const char *buffer, int count)
{
	int rc;

	count /= 4;
	count *= 4;
	rc = write(oss_fd, buffer, count);
	return rc;
}

static int oss_pause(void)
{
	if (ioctl(oss_fd, SNDCTL_DSP_POST, NULL) == -1)
		return -1;
	return 0;
}

static int oss_unpause(void)
{
	return 0;
}

static int oss_buffer_space(void)
{
	audio_buf_info info;
	int space;

	if (ioctl(oss_fd, SNDCTL_DSP_GETOSPACE, &info) == -1)
		return -1;
	space = info.bytes;
/* 	d_print("%d\n", space); */
	return space;
}

static int op_oss_set_option(int key, const char *val)
{
	switch (key) {
	case 0:
		free(oss_dsp_device);
		oss_dsp_device = xstrdup(val);
		break;
	default:
		return -OP_ERROR_NOT_OPTION;
	}
	return 0;
}

static int op_oss_get_option(int key, char **val)
{
	switch (key) {
	case 0:
		if (oss_dsp_device)
			*val = xstrdup(oss_dsp_device);
		break;
	default:
		return -OP_ERROR_NOT_OPTION;
	}
	return 0;
}

const struct output_plugin_ops op_pcm_ops = {
	.init = oss_init,
	.exit = oss_exit,
	.open = oss_open,
	.close = oss_close,
	.write = oss_write,
	.pause = oss_pause,
	.unpause = oss_unpause,
	.buffer_space = oss_buffer_space,
	.set_option = op_oss_set_option,
	.get_option = op_oss_get_option
};

const char * const op_pcm_options[] = {
	"device",
	NULL
};

const int op_priority = 1;
