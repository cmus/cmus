/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2004-2005 Timo Hirvonen
 *
 * mixer_sun.c by alex <pukpuk@gmx.de>
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

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/audioio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "../debug.h"
#include "../mixer.h"
#include "../op.h"
#include "../sf.h"
#include "../xmalloc.h"

static int sun_mixer_device_id = -1;
static int sun_mixer_channels = -1;
static int sun_mixer_volume_delta = -1;
static int mixer_fd = -1;

static char *sun_mixer_device = NULL;
static char *sun_mixer_channel = NULL;

static int mixer_open(const char *);
static int min_delta(int, int, int);
static int sun_device_exists(const char *);
static int sun_mixer_init(void);
static int sun_mixer_exit(void);
static int sun_mixer_open(int *);
static int sun_mixer_close(void);
static int sun_mixer_set_volume(int, int);
static int sun_mixer_get_volume(int *, int *);

static int mixer_open(const char *dev)
{
	struct mixer_devinfo minf;
	int output_class;

	mixer_fd = open(dev, O_RDWR);
	if (mixer_fd == -1)
		return -1;

	output_class = -1;
	sun_mixer_device_id = -1;
	/* determine output class */
	minf.index = 0;
	while (ioctl(mixer_fd, AUDIO_MIXER_DEVINFO, &minf) != -1) {
		if (minf.type == AUDIO_MIXER_CLASS) {
			if (strcmp(minf.label.name, AudioCoutputs) == 0)
				output_class = minf.index;
		}
		++minf.index;
	}
	/* no output class found?? something must be wrong */
	if (output_class == -1)
		return -1;

	minf.index = 0;
	/* query all mixer devices and try choose the correct one */
	while (ioctl(mixer_fd, AUDIO_MIXER_DEVINFO, &minf) != -1) {
		/* only scan output channels */
		if (minf.type == AUDIO_MIXER_VALUE && minf.prev == AUDIO_MIXER_LAST &&
		    minf.mixer_class == output_class) {
			if (strcasecmp(minf.label.name, sun_mixer_channel) == 0) {
				sun_mixer_volume_delta = minf.un.v.delta;
				sun_mixer_device_id = minf.index;
				sun_mixer_channels = minf.un.v.num_channels;
			}
		}
		++minf.index;
	}

	if (sun_mixer_device_id == -1)
		return -1;

	d_print("sun: found mixer-channel: %s, devid: %d, delta: %d, channels: %d\n", sun_mixer_channel,
	    sun_mixer_device_id, sun_mixer_volume_delta, sun_mixer_channels);

	if (sun_mixer_volume_delta == 0)
		sun_mixer_volume_delta = 1;

	return 0;

}

static int min_delta(int oval, int nval, int delta)
{
	if (oval > nval && oval - nval < delta)
		nval -= delta;
	else if (oval < nval && nval - oval < delta)
		nval += delta;

	nval = (nval < 0) ? 0 : nval;
	nval = (nval > AUDIO_MAX_GAIN) ? AUDIO_MAX_GAIN : nval;

	return nval;
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

static int sun_mixer_init(void)
{
	const char *mixer_dev = "/dev/mixer";

	if (sun_mixer_device != NULL) {
		if (sun_device_exists(sun_mixer_device))
			return 0;
		free(sun_mixer_device);
		sun_mixer_device = NULL;
		return -1;
	}
	if (sun_device_exists(mixer_dev)) {
		sun_mixer_device = xstrdup(mixer_dev);
		return 0;
	}

	return -1;
}

static int sun_mixer_exit(void)
{
	if (sun_mixer_device != NULL) {
		free(sun_mixer_device);
		sun_mixer_device = NULL;
	}
	if (sun_mixer_channel != NULL) {
		free(sun_mixer_channel);
		sun_mixer_channel = NULL;
	}

	return 0;
}

static int sun_mixer_open(int *vol_max)
{
	const char *mixer_channel = "master";

	/* set default mixer channel */
	if (sun_mixer_channel == NULL)
		sun_mixer_channel = xstrdup(mixer_channel);

	if (mixer_open(sun_mixer_device) == 0) {
		*vol_max = AUDIO_MAX_GAIN;
		return 0;
	}

	return -1;
}

static int sun_mixer_close(void)
{
	if (mixer_fd != -1) {
		close(mixer_fd);
		mixer_fd = -1;
	}

	return 0;
}

static int sun_mixer_set_volume(int l, int r)
{
	struct mixer_ctrl minf;
	int ovall, ovalr;

	if (sun_mixer_get_volume(&ovall, &ovalr) == -1)
		return -1;

	/* OpenBSD mixer values are `discrete' */
	l = min_delta(ovall, l, sun_mixer_volume_delta);
	r = min_delta(ovalr, r, sun_mixer_volume_delta);

	minf.type = AUDIO_MIXER_VALUE;
	minf.dev = sun_mixer_device_id;

	if (sun_mixer_channels == 1)
		minf.un.value.level[AUDIO_MIXER_LEVEL_MONO] = l;
	else {
		minf.un.value.level[AUDIO_MIXER_LEVEL_LEFT] = l;
		minf.un.value.level[AUDIO_MIXER_LEVEL_RIGHT] = r;
	}
	minf.un.value.num_channels = sun_mixer_channels;

	if (ioctl(mixer_fd, AUDIO_MIXER_WRITE, &minf) == -1)
		return -1;

	return 0;
}

static int sun_mixer_get_volume(int *l, int *r)
{
	struct mixer_ctrl minf;

	minf.dev = sun_mixer_device_id;
	minf.type = AUDIO_MIXER_VALUE;
	minf.un.value.num_channels = sun_mixer_channels;

	if (ioctl(mixer_fd, AUDIO_MIXER_READ, &minf) == -1)
		return -1;

	if (sun_mixer_channels == 1) {
		*l = minf.un.value.level[AUDIO_MIXER_LEVEL_MONO];
		*r = *l;
	} else {
		*l = minf.un.value.level[AUDIO_MIXER_LEVEL_LEFT];
		*r = minf.un.value.level[AUDIO_MIXER_LEVEL_RIGHT];
	}

	return 0;
}

static int sun_mixer_set_channel(const char *val)
{
	if (sun_mixer_channel != NULL)
		free(sun_mixer_channel);
	sun_mixer_channel = xstrdup(val);
	return 0;
}

static int sun_mixer_get_channel(char **val)
{
	if (sun_mixer_channel)
		*val = xstrdup(sun_mixer_channel);
	return 0;
}

static int sun_mixer_set_device(const char *val)
{
	free(sun_mixer_device);
	sun_mixer_device = xstrdup(val);
	return 0;
}

static int sun_mixer_get_device(char **val)
{
	if (sun_mixer_device)
		*val = xstrdup(sun_mixer_device);
	return 0;
}

const struct mixer_plugin_ops op_mixer_ops = {
	.init = sun_mixer_init,
	.exit = sun_mixer_exit,
	.open = sun_mixer_open,
	.close = sun_mixer_close,
	.set_volume = sun_mixer_set_volume,
	.get_volume = sun_mixer_get_volume,
};

const struct mixer_plugin_opt op_mixer_options[] = {
	OPT(sun_mixer, channel),
	OPT(sun_mixer, device),
	{ NULL },
};
