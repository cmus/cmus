/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2006 Johannes Weißl
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
#include "../xmalloc.h"
#include "../utils.h"
#include "../misc.h"
#include "../debug.h"

/*
 * <ao/ao.h> uses FILE but doesn't include stdio.h.
 * Also we use snprintf().
 */
#include <stdio.h>
#include <strings.h>
#include <ao/ao.h>

#ifdef AO_EBADFORMAT
#define AO_API_1
#endif

static ao_device *libao_device;
static char *wav_dir = NULL;
static int wav_counter = 1;
static int is_wav = 0;

/* configuration */
static char *libao_driver = NULL;
static int libao_buffer_space = 16384;
static int libao_cur_buffer_space = 0;
static char *libao_device_interface = NULL;


static int op_ao_init(void)
{
	/* ignore config value */
	wav_counter = 1;

	ao_initialize();
	return 0;
}

static int op_ao_exit(void)
{
	free(libao_driver);
	ao_shutdown();
	return 0;
}

/* http://www.xiph.org/ao/doc/ao_sample_format.html */
static const struct {
	channel_position_t pos;
	const char *str;
} ao_channel_mapping[] = {
	{ CHANNEL_POSITION_LEFT,			"L" },
	{ CHANNEL_POSITION_RIGHT,			"R" },
	{ CHANNEL_POSITION_CENTER,			"C" },
	{ CHANNEL_POSITION_MONO,			"M" },
	{ CHANNEL_POSITION_FRONT_LEFT_OF_CENTER,	"CL" },
	{ CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER,	"CR" },
	{ CHANNEL_POSITION_REAR_LEFT,			"BL" },
	{ CHANNEL_POSITION_REAR_RIGHT,			"BR" },
	{ CHANNEL_POSITION_REAR_CENTER,			"BC" },
	{ CHANNEL_POSITION_SIDE_LEFT,			"SL" },
	{ CHANNEL_POSITION_SIDE_RIGHT,			"SR" },
	{ CHANNEL_POSITION_LFE,				"LFE" },
	{ CHANNEL_POSITION_INVALID,			"X" },
};

#ifdef AO_API_1
static char *ao_channel_matrix(int channels, const channel_position_t *map)
{
	int i, j;
	char buf[256] = "";

	if (!map || !channel_map_valid(map))
		return NULL;

	for (i = 0; i < channels; i++) {
		const channel_position_t pos = map[i];
		int found = 0;
		for (j = 0; j < N_ELEMENTS(ao_channel_mapping); j++) {
			if (pos == ao_channel_mapping[j].pos) {
				strcat(buf, ao_channel_mapping[j].str);
				strcat(buf, ",");
				found = 1;
				break;
			}
		}
		if (!found)
			strcat(buf, "M,");
	}
	buf[strlen(buf)-1] = '\0';

	return xstrdup(buf);
}
#endif

static int op_ao_open(sample_format_t sf, const channel_position_t *channel_map)
{
	ao_sample_format format = {
		.bits        = sf_get_bits(sf),
		.rate        = sf_get_rate(sf),
		.channels    = sf_get_channels(sf),
		.byte_format = sf_get_bigendian(sf) ? AO_FMT_BIG : AO_FMT_LITTLE,
#ifdef AO_API_1
		.matrix      = ao_channel_matrix(sf_get_channels(sf), channel_map)
#endif
	};
	int driver;

	if (libao_driver == NULL) {
		driver = ao_default_driver_id();
	} else {
		driver = ao_driver_id(libao_driver);
		is_wav = strcasecmp(libao_driver, "wav") == 0;
	}
	if (driver == -1) {
		errno = ENODEV;
		return -OP_ERROR_ERRNO;
	}

	if (is_wav) {
		char file[512];

		if (wav_dir == NULL)
			wav_dir = xstrdup(home_dir);
		snprintf(file, sizeof(file), "%s/%02d.wav", wav_dir, wav_counter);
		libao_device = ao_open_file(driver, file, 0, &format, NULL);
	} else {
		ao_option *ao_options = NULL;
		if (libao_device_interface) {
			ao_append_option(&ao_options, "dev", libao_device_interface);
		}
		libao_device = ao_open_live(driver, &format, ao_options);
	}

	if (libao_device == NULL) {
		switch (errno) {
		case AO_ENODRIVER:
		case AO_ENOTFILE:
		case AO_ENOTLIVE:
		case AO_EOPENDEVICE:
			errno = ENODEV;
			return -OP_ERROR_ERRNO;
		case AO_EBADOPTION:
			errno = EINVAL;
			return -OP_ERROR_ERRNO;
		case AO_EOPENFILE:
			errno = EACCES;
			return -OP_ERROR_ERRNO;
		case AO_EFILEEXISTS:
			errno = EEXIST;
			return -OP_ERROR_ERRNO;
		case AO_EFAIL:
		default:
			return -OP_ERROR_INTERNAL;
		}
	}

	/* ensure that the buffer size is a multiple of the frame size */
	libao_cur_buffer_space = is_wav ? 128 * 1024 : libao_buffer_space;
	libao_cur_buffer_space -= libao_cur_buffer_space % sf_get_frame_size(sf);

#ifdef AO_API_1
	d_print("channel matrix: %s\n", format.matrix ? format.matrix : "default");
#endif
	return 0;
}

static int op_ao_close(void)
{
	ao_close(libao_device);
	if (is_wav)
		wav_counter++;
	return 0;
}

static int op_ao_write(const char *buffer, int count)
{
	if (ao_play(libao_device, (void *)buffer, count) == 0)
		return -1;
	return count;
}

static int op_ao_buffer_space(void)
{
	return libao_cur_buffer_space;
}

static int op_ao_set_buffer_size(const char *val)
{
	long int ival;
	if (str_to_int(val, &ival) || ival < 4096) {
		errno = EINVAL;
		return -OP_ERROR_ERRNO;
	}
	libao_buffer_space = ival;
	return 0;
}

static int op_ao_get_buffer_size(char **val)
{
	*val = xnew(char, 22);
	snprintf(*val, 22, "%d", libao_buffer_space);
	return 0;
}

static int op_ao_set_driver(const char *val)
{
	free(libao_driver);
	libao_driver = NULL;
	if (val[0])
		libao_driver = xstrdup(val);
	return 0;
}

static int op_ao_get_driver(char **val)
{
	if (libao_driver)
		*val = xstrdup(libao_driver);
	return 0;
}

static int op_ao_set_wav_counter(const char *val)
{
	long int ival;
	if (str_to_int(val, &ival)) {
		errno = EINVAL;
		return -OP_ERROR_ERRNO;
	}
	wav_counter = ival;
	return 0;
}

static int op_ao_get_wav_counter(char **val)
{
	*val = xnew(char, 22);
	snprintf(*val, 22, "%d", wav_counter);
	return 0;
}

static int op_ao_set_wav_dir(const char *val)
{
	free(wav_dir);
	wav_dir = xstrdup(val);
	return 0;
}

static int op_ao_get_wav_dir(char **val)
{
	if (wav_dir == NULL)
		wav_dir = xstrdup(home_dir);
	*val = expand_filename(wav_dir);
	return 0;
}

static int op_ao_set_device_interface(const char *val)
{
	free(libao_device_interface);
	libao_device_interface = NULL;
	if (val[0])
		libao_device_interface = xstrdup(val);
	return 0;
}

static int op_ao_get_device_interface(char **val)
{
	if (libao_device_interface)
		*val = xstrdup(libao_device_interface);
	return 0;
}

const struct output_plugin_ops op_pcm_ops = {
	.init = op_ao_init,
	.exit = op_ao_exit,
	.open = op_ao_open,
	.close = op_ao_close,
	.write = op_ao_write,
	.buffer_space = op_ao_buffer_space,
};

const struct output_plugin_opt op_pcm_options[] = {
	OPT(op_ao, buffer_size),
	OPT(op_ao, driver),
	OPT(op_ao, wav_counter),
	OPT(op_ao, wav_dir),
	OPT(op_ao, device_interface),
	{ NULL },
};

const int op_priority = 3;
const unsigned op_abi_version = OP_ABI_VERSION;
