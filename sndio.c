/*
 * Copyright (C) 2011 Donovan "Tsomi" Watteau <tsoomi@gmail.com>
 *
 * Based on Thomas Pfaff's work for XMMS, and some suggestions from
 * Alexandre Ratchov.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/audioio.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sndio.h>

#include "op.h"
#include "mixer.h"
#include "sf.h"
#include "xmalloc.h"

static sample_format_t sndio_sf;
static struct sio_par par;
static struct sio_hdl *hdl = NULL;
static int sndio_volume = 100;
static int sndio_paused;

static int sndio_mixer_set_volume(int l, int r)
{
	sndio_volume = l > r ? l : r;

	if (hdl != NULL)
		sio_setvol(hdl, sndio_volume * SIO_MAXVOL / 100);

	return 0;
}

static int sndio_mixer_get_volume(int *l, int *r)
{
	*l = *r = sndio_volume;

	return 0;
}

static int sndio_set_sf(sample_format_t sf)
{
	struct sio_par apar;

	sndio_sf = sf;

	sio_initpar(&par);

	par.pchan = sf_get_channels(sndio_sf);
	par.rate = sf_get_rate(sndio_sf);
	sndio_paused = 0;

	if (sf_get_signed(sndio_sf))
		par.sig = 1;
	else
		par.sig = 0;

	if (sf_get_bigendian(sndio_sf))
		par.le = 0;
	else
		par.le = 1;

	switch (sf_get_bits(sndio_sf)) {
	case 16:
		par.bits = 16;
		break;
	case 8:
		par.bits = 8;
		break;
	default:
		return -1;
	}

	par.appbufsz = par.rate * 300 / 1000;
	apar = par;

	if (!sio_setpar(hdl, &par))
		return -1;

	if (!sio_getpar(hdl, &par))
		return -1;

	if (apar.rate != par.rate || apar.pchan != par.pchan ||
	    apar.bits != par.bits || (par.bits > 8 && apar.le != par.le) ||
	    apar.sig != par.sig)
		return -1;

	sndio_mixer_set_volume(sndio_volume, sndio_volume);

	if (!sio_start(hdl))
		return -1;

	return 0;
}

static int sndio_init(void)
{
	return 0;
}

static int sndio_exit(void)
{
	return 0;
}

static int sndio_close(void)
{
	if (hdl != NULL) {
		sio_close(hdl);
		hdl = NULL;
	}

	return 0;
}

static int sndio_open(sample_format_t sf, const channel_position_t *channel_map)
{
	hdl = sio_open(NULL, SIO_PLAY, 0);
	if (hdl == NULL)
		return -1;

	if (sndio_set_sf(sf) == -1) {
		sndio_close();
		return -1;
	}

	return 0;
}

static int sndio_write(const char *buf, int cnt)
{
	size_t rc;

	rc = sio_write(hdl, buf, cnt);
	if (rc == 0)
		return -1;

	return rc;
}

static int op_sndio_set_option(int key, const char *val)
{
	return -OP_ERROR_NOT_OPTION;
}

static int op_sndio_get_option(int key, char **val)
{
	return -OP_ERROR_NOT_OPTION;
}

static int sndio_pause(void)
{
	if (!sndio_paused) {
		sio_stop(hdl);
		sndio_paused = 1;
	}

	return 0;
}

static int sndio_unpause(void)
{
	if (sndio_paused) {
		sio_start(hdl);
		sndio_paused = 0;
	}

	return 0;
}

static int sndio_buffer_space(void)
{
	/*
	 * Do as if there's always some space and let sio_write() block.
	 */
	return par.bufsz * par.bps * par.pchan;
}

static int sndio_mixer_init(void)
{
	return OP_ERROR_SUCCESS;
}

static int sndio_mixer_exit(void)
{
	return OP_ERROR_SUCCESS;
}

static int sndio_mixer_open(int *volume_max)
{
	*volume_max = 100; 

	return OP_ERROR_SUCCESS;
}

static int sndio_mixer_close(void)
{
	return OP_ERROR_SUCCESS;
}

static int sndio_mixer_set_option(int key, const char *val)
{
	return -OP_ERROR_NOT_OPTION;
}

static int sndio_mixer_get_option(int key, char **val)
{
	return -OP_ERROR_NOT_OPTION;
}

const struct output_plugin_ops op_pcm_ops = {
	.init = sndio_init,
	.exit = sndio_exit,
	.open = sndio_open,
	.close = sndio_close,
	.write = sndio_write,
	.pause = sndio_pause,
	.unpause = sndio_unpause,
	.buffer_space = sndio_buffer_space,
	.set_option = op_sndio_set_option,
	.get_option = op_sndio_get_option
};

const struct mixer_plugin_ops op_mixer_ops = {
	.init = sndio_mixer_init,
	.exit = sndio_mixer_exit,
	.open = sndio_mixer_open,
	.close = sndio_mixer_close,
	.set_volume = sndio_mixer_set_volume,
	.get_volume = sndio_mixer_get_volume,
	.set_option = sndio_mixer_set_option,
	.get_option = sndio_mixer_get_option
};

const char * const op_pcm_options[] = {
	NULL
};

const char * const op_mixer_options[] = {
	NULL
};

const int op_priority = 2;
