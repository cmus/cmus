/*
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include "op.h"
#include "xmalloc.h"
#include "utils.h"
#include "misc.h"

/*
 * <ao/ao.h> uses FILE but doesn't include stdio.h.
 * Also we use snprintf().
 */
#include <stdio.h>
#include <strings.h>
#include <ao/ao.h>

static ao_device *libao_device;
static char *wav_dir = NULL;
static int wav_counter = 1;
static int is_wav = 0;

/* configuration */
static char *libao_driver = NULL;
static int libao_buffer_space = 16384;


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

static int op_ao_open(sample_format_t sf)
{
	ao_sample_format format;
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

	memset(&format, 0, sizeof format);

	format.bits = sf_get_bits(sf);
	format.rate = sf_get_rate(sf);
	format.channels = sf_get_channels(sf);
	format.byte_format = sf_get_bigendian(sf) ? AO_FMT_BIG : AO_FMT_LITTLE;

	if (is_wav) {
		char file[512];

		if (wav_dir == NULL)
			wav_dir = xstrdup(home_dir);
		snprintf(file, sizeof(file), "%s/%02d.wav", wav_dir, wav_counter);
		libao_device = ao_open_file(driver, file, 0, &format, NULL);
	} else {
		libao_device = ao_open_live(driver, &format, NULL);
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
	if (is_wav)
		return 128 * 1024;
	return libao_buffer_space;
}

static int op_ao_set_option(int key, const char *val)
{
	long int ival;

	switch (key) {
	case 0:
		if (str_to_int(val, &ival) || ival < 4096) {
			errno = EINVAL;
			return -OP_ERROR_ERRNO;
		}
		libao_buffer_space = ival;
		break;
	case 1:
		free(libao_driver);
		libao_driver = NULL;
		if (val[0])
			libao_driver = xstrdup(val);
		break;
	case 2:
		if (str_to_int(val, &ival)) {
			errno = EINVAL;
			return -OP_ERROR_ERRNO;
		}
		wav_counter = ival;
		break;
	case 3:
		free(wav_dir);
		wav_dir = xstrdup(val);
		break;
	default:
		return -OP_ERROR_NOT_OPTION;
	}
	return 0;
}

static int op_ao_get_option(int key, char **val)
{
	switch (key) {
	case 0:
		*val = xnew(char, 22);
		snprintf(*val, 22, "%d", libao_buffer_space);
		break;
	case 1:
		if (libao_driver)
			*val = xstrdup(libao_driver);
		break;
	case 2:
		*val = xnew(char, 22);
		snprintf(*val, 22, "%d", wav_counter);
		break;
	case 3:
		if (wav_dir == NULL)
			wav_dir = xstrdup(home_dir);
		*val = xstrdup(wav_dir);
		break;
	default:
		return -OP_ERROR_NOT_OPTION;
	}
	return 0;
}

const struct output_plugin_ops op_pcm_ops = {
	.init = op_ao_init,
	.exit = op_ao_exit,
	.open = op_ao_open,
	.close = op_ao_close,
	.write = op_ao_write,
	.buffer_space = op_ao_buffer_space,
	.set_option = op_ao_set_option,
	.get_option = op_ao_get_option
};

const char * const op_pcm_options[] = {
	"buffer_size",
	"driver",
	"wav_counter",
	"wav_dir",
	NULL
};

const int op_priority = 3;
