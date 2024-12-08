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

#include "../op.h"
#include "../sf.h"
#include "../xmalloc.h"
#include "../debug.h"
#include "../utils.h"

#include <strings.h>
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
	if (ioctl(oss_fd, SNDCTL_DSP_RESET, 0) == -1) {
		return -1;
	}
	return 0;
}

#if defined(__linux__)
/* defined only in OSSv4, but seem to work in OSSv3 (Linux) */
#ifndef AFMT_S32_LE
#define AFMT_S32_LE	0x00001000
#endif
#ifndef AFMT_S32_BE
#define AFMT_S32_BE	0x00002000
#endif
#ifndef AFMT_S24_PACKED
#define AFMT_S24_PACKED	0x00040000
#endif
#endif

struct oss_fmt {
	int fmt, bits, sig, be;
};
static struct oss_fmt oss_fmts[] = {
	{ AFMT_S16_BE, 16, 1, 1 },
	{ AFMT_S16_LE, 16, 1, 0 },
#ifdef AFMT_S24_PACKED
	{ AFMT_S24_PACKED, 24, 1, 0 },
#endif
#ifdef AFMT_S24_BE
	{ AFMT_S24_BE, 24, 1, 1 },
#endif
#ifdef AFMT_S24_LE
	{ AFMT_S24_LE, 24, 1, 0 },
#endif
#ifdef AFMT_S32_BE
	{ AFMT_S32_BE, 32, 1, 1 },
#endif
#ifdef AFMT_S32_LE
	{ AFMT_S32_LE, 32, 1, 0 },
#endif

	{ AFMT_U16_BE, 16, 0, 1 },
	{ AFMT_U16_LE, 16, 0, 0 },
#ifdef AFMT_U24_BE
	{ AFMT_U24_BE, 24, 0, 1 },
#endif
#ifdef AFMT_U24_LE
	{ AFMT_U24_LE, 24, 0, 0 },
#endif
#ifdef AFMT_U32_BE
	{ AFMT_U32_BE, 32, 0, 1 },
#endif
#ifdef AFMT_U32_LE
	{ AFMT_U32_LE, 32, 0, 0 },
#endif
	{ AFMT_S8, 8, 1, 0 },
	{ AFMT_S8, 8, 1, 1 },
	{ AFMT_U8, 8, 0, 0 },
	{ AFMT_U8, 8, 0, 1 },
};

static int oss_set_sf(sample_format_t sf)
{
	int found, tmp, log2_fragment_size, nr_fragments, bytes_per_second;
	size_t i;

	oss_reset();
	oss_sf = sf;

#ifdef SNDCTL_DSP_CHANNELS
	tmp = sf_get_channels(oss_sf);
	if (ioctl(oss_fd, SNDCTL_DSP_CHANNELS, &tmp) == -1)
		return -1;
#else
	tmp = sf_get_channels(oss_sf) - 1;
	if (ioctl(oss_fd, SNDCTL_DSP_STEREO, &tmp) == -1)
		return -1;
#endif

	found = 0;
	for (i = 0; i < N_ELEMENTS(oss_fmts); i++) {
		if (sf_get_bits(oss_sf) == oss_fmts[i].bits &&
		    sf_get_signed(oss_sf) == oss_fmts[i].sig &&
		    sf_get_bigendian(oss_sf) == oss_fmts[i].be) {
			found = 1;
			tmp = oss_fmts[i].fmt;
			break;
		}
	}
	if (!found) {
		d_print("unsupported sample format: %c%u_%s\n",
			sf_get_signed(oss_sf) ? 'S' : 'U', sf_get_bits(oss_sf),
			sf_get_bigendian(oss_sf) ? "BE" : "LE");
		return -1;
	}
	if (ioctl(oss_fd, SNDCTL_DSP_SAMPLESIZE, &tmp) == -1)
		return -1;

	tmp = sf_get_rate(oss_sf);
	if (ioctl(oss_fd, SNDCTL_DSP_SPEED, &tmp) == -1)
		return -1;

	bytes_per_second = sf_get_second_size(oss_sf);
	log2_fragment_size = 0;
	while (1 << log2_fragment_size < bytes_per_second / 25)
		log2_fragment_size++;
	log2_fragment_size--;
	nr_fragments = 32;

	/* bits 0..15 = size of fragment, 16..31 = log2(number of fragments) */
	tmp = (nr_fragments << 16) + log2_fragment_size;
	if (ioctl(oss_fd, SNDCTL_DSP_SETFRAGMENT, &tmp) == -1)
		return -1;
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

static int oss_open(sample_format_t sf, const channel_position_t *channel_map)
{
	int oss_version = 0;
	oss_fd = open(oss_dsp_device, O_WRONLY);
	if (oss_fd == -1)
		return -1;
	ioctl(oss_fd, OSS_GETVERSION, &oss_version);
	d_print("oss version: %#08x\n", oss_version);
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

	count -= count % sf_get_frame_size(oss_sf);
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
	space = (info.fragments - 1) * info.fragsize;
	return space;
}

static int op_oss_set_device(const char *val)
{
	free(oss_dsp_device);
	oss_dsp_device = xstrdup(val);
	return 0;
}

static int op_oss_get_device(char **val)
{
	if (oss_dsp_device)
		*val = xstrdup(oss_dsp_device);
	return 0;
}

enum {
	OSS_MIXER_CHANNEL_VOLUME,
	OSS_MIXER_CHANNEL_BASS,
	OSS_MIXER_CHANNEL_TREBLE,
	OSS_MIXER_CHANNEL_SYNTH,
	OSS_MIXER_CHANNEL_PCM,
	OSS_MIXER_CHANNEL_SPEAKER,
	OSS_MIXER_CHANNEL_LINE,
	OSS_MIXER_CHANNEL_MIC,
	OSS_MIXER_CHANNEL_CD,
	OSS_MIXER_CHANNEL_IMIX,
	OSS_MIXER_CHANNEL_ALTPCM,
	OSS_MIXER_CHANNEL_RECLEV,
	OSS_MIXER_CHANNEL_IGAIN,
	OSS_MIXER_CHANNEL_OGAIN,
	OSS_MIXER_CHANNEL_LINE1,
	OSS_MIXER_CHANNEL_LINE2,
	OSS_MIXER_CHANNEL_LINE3,
	OSS_MIXER_CHANNEL_DIGITAL1,
	OSS_MIXER_CHANNEL_DIGITAL2,
	OSS_MIXER_CHANNEL_DIGITAL3,
	OSS_MIXER_CHANNEL_PHONEIN,
	OSS_MIXER_CHANNEL_PHONEOUT,
	OSS_MIXER_CHANNEL_VIDEO,
	OSS_MIXER_CHANNEL_RADIO,
	OSS_MIXER_CHANNEL_MONITOR,
	OSS_MIXER_CHANNEL_MAX
};

static int mixer_fd = -1;
static int mixer_devmask;
/* static int mixer_recmask; */
/* static int mixer_recsrc; */
/* static int mixer_stereodevs; */
static char mixer_channels[OSS_MIXER_CHANNEL_MAX];

/* configuration */
static char *oss_mixer_device = NULL;
static int oss_volume_controls_pcm = 1;

static int mixer_open(const char *device)
{
	int i;

	mixer_fd = open(device, O_RDWR);
	if (mixer_fd == -1)
		return -1;
	ioctl(mixer_fd, SOUND_MIXER_READ_DEVMASK, &mixer_devmask);
/* 	ioctl(mixer_fd, SOUND_MIXER_READ_RECMASK, &mixer_recmask); */
/* 	ioctl(mixer_fd, SOUND_MIXER_READ_RECSRC, &mixer_recsrc); */
/* 	ioctl(mixer_fd, SOUND_MIXER_READ_STEREODEVS, &mixer_stereodevs); */
	i = 0;
	while (i < min_i(SOUND_MIXER_NRDEVICES, OSS_MIXER_CHANNEL_MAX)) {
		mixer_channels[i] = (mixer_devmask >> i) & 1;
		i++;
	}
	while (i < OSS_MIXER_CHANNEL_MAX)
		mixer_channels[i++] = 0;
	return 0;
}

static int mixer_set_level(int channel, int l, int r)
{
	int tmp;

	tmp = (l & 0x7f) + ((r & 0x7f) << 8);
	if (ioctl(mixer_fd, MIXER_WRITE(channel), &tmp) == -1)
		return -1;
	return 0;
}

static int mixer_get_level(int channel, int *l, int *r)
{
	int tmp;

	if (ioctl(mixer_fd, MIXER_READ(channel), &tmp) == -1)
		return -1;
	*l = tmp & 0x7f;
	*r = (tmp >> 8) & 0x7f;
	return 0;
}

static int oss_mixer_init(void)
{
	const char *new_mixer_dev = "/dev/sound/mixer";
	const char *mixer_dev = "/dev/mixer";

	if (oss_mixer_device) {
		if (oss_device_exists(oss_mixer_device))
			return 0;
		free(oss_mixer_device);
		oss_mixer_device = NULL;
		return -1;
	}
	if (oss_device_exists(new_mixer_dev)) {
		oss_mixer_device = xstrdup(new_mixer_dev);
		return 0;
	}
	if (oss_device_exists(mixer_dev)) {
		oss_mixer_device = xstrdup(mixer_dev);
		return 0;
	}
	return -1;
}

static int oss_mixer_exit(void)
{
	if (oss_mixer_device) {
		free(oss_mixer_device);
		oss_mixer_device = NULL;
	}
	return 0;
}

static int oss_mixer_open(int *volume_max)
{
	*volume_max = 100;
	if (mixer_open(oss_mixer_device) == 0)
		return 0;
	return -1;
}

static int oss_mixer_close(void)
{
	close(mixer_fd);
	mixer_fd = -1;
	return 0;
}

static int oss_mixer_set_volume(int l, int r)
{
	if (oss_volume_controls_pcm) {
		return mixer_set_level(OSS_MIXER_CHANNEL_PCM, l, r);
	} else {
		return mixer_set_level(OSS_MIXER_CHANNEL_VOLUME, l, r);
	}
}

static int oss_mixer_get_volume(int *l, int *r)
{
	if (oss_volume_controls_pcm) {
		return mixer_get_level(OSS_MIXER_CHANNEL_PCM, l, r);
	} else {
		return mixer_get_level(OSS_MIXER_CHANNEL_VOLUME, l, r);
	}
}

static int oss_mixer_set_channel(const char *val)
{
	if (strcasecmp(val, "pcm") == 0) {
		oss_volume_controls_pcm = 1;
	} else if (strcasecmp(val, "master") == 0) {
		oss_volume_controls_pcm = 0;
	} else {
		errno = EINVAL;
		return -OP_ERROR_ERRNO;
	}
	return 0;
}

static int oss_mixer_get_channel(char **val)
{
	if (oss_volume_controls_pcm) {
		*val = xstrdup("PCM");
	} else {
		*val = xstrdup("Master");
	}
	return 0;
}

static int oss_mixer_set_device(const char *val)
{
	free(oss_mixer_device);
	oss_mixer_device = xstrdup(val);
	return 0;
}

static int oss_mixer_get_device(char **val)
{
	if (oss_mixer_device)
		*val = xstrdup(oss_mixer_device);
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
};

const struct output_plugin_opt op_pcm_options[] = {
	OPT(op_oss, device),
	{ NULL },
};

const struct mixer_plugin_ops op_mixer_ops = {
	.init = oss_mixer_init,
	.exit = oss_mixer_exit,
	.open = oss_mixer_open,
	.close = oss_mixer_close,
	.set_volume = oss_mixer_set_volume,
	.get_volume = oss_mixer_get_volume,
};

const struct mixer_plugin_opt op_mixer_options[] = {
	OPT(oss_mixer, channel),
	OPT(oss_mixer, device),
	{ NULL },
};

const int op_priority = 1;
const unsigned op_abi_version = OP_ABI_VERSION;
