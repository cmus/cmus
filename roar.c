/*
 * Copyright 2006 Johannes Weißl
 * Copyright 2010 Philipp 'ph3-der-loewe' Schafft
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

#include <roaraudio.h>

#include "op.h"
#include "mixer.h"
#include "xmalloc.h"
#include "utils.h"
#include "misc.h"
#include "debug.h"

// we do not use native 2^16-1 here as they use signed ints with 16 bit
// so we use 2^(16-1)-1 here.
#define MIXER_BASE_VOLUME 32767

static roar_vs_t *vss = NULL;
static int err;
static sample_format_t format;

/* configuration */
static char *host = NULL;
static char *role = NULL;

static inline void _err_to_errno(void)
{
	roar_errno = err;
	roar_err_to_errno();
}

static int op_roar_dummy(void)
{
	return 0;
}

static ssize_t op_roar_debug_write(struct roar_vio_calls *vio, void *buf_, size_t count)
{
	char *buf = (char *) buf_;
	int len = count;
	if (len > 0 && buf[len-1] == '\n')
		len--;
	if (len > 0)
		d_print("%*s\n", len, buf);
	return count;
}

static struct roar_vio_calls op_roar_debug_cbs = {
	.write = op_roar_debug_write
};

static int op_roar_init(void)
{
#if DEBUG > 1
	roar_debug_set_stderr_mode(ROAR_DEBUG_MODE_VIO);
	roar_debug_set_stderr_vio(&op_roar_debug_cbs);
#else
	roar_debug_set_stderr_mode(ROAR_DEBUG_MODE_SYSLOG);
#endif
	return 0;
}

static int op_roar_exit(void)
{
	if (host != NULL)
		free(host);
	if (role != NULL)
		free(role);
	return 0;
}

static int _set_role(void)
{
	int roleid = ROAR_ROLE_UNKNOWN;

	if (role == NULL)
		return 0;

	roleid = roar_str2role(role);

	if (roleid == ROAR_ROLE_UNKNOWN) {
		// TODO: warn if role is invalid.
		return 0;
	}

	if (roar_vs_role(vss, roleid, &err) == -1) {
		return -OP_ERROR_ERRNO;
	}

	return 0;
}

static int op_roar_open(sample_format_t sf)
{
	int codec = -1;
	int ret;

	ROAR_DBG("op_roar_open(*) = ?");

	format = sf;

	if (sf_get_bigendian(sf)) {
		if (sf_get_signed(sf)) {
			codec = ROAR_CODEC_PCM_S_BE;
		} else {
			codec = ROAR_CODEC_PCM_U_BE;
		}
	} else {
		if (sf_get_signed(sf)) {
			codec = ROAR_CODEC_PCM_S_LE;
		} else {
			codec = ROAR_CODEC_PCM_U_LE;
		}
	}

	ROAR_DBG("op_roar_open(*) = ?");

	vss = roar_vs_new_playback(host, "C* Music Player (cmus)", sf_get_rate(sf), sf_get_channels(sf), codec, sf_get_bits(sf), &err);

	if (vss == NULL) {
		ROAR_DBG("op_roar_open(*) = ?");

		_err_to_errno();
		return -OP_ERROR_ERRNO;
	}

	ROAR_DBG("op_roar_open(*) = ?");

	if (roar_vs_buffer(vss, 2048*8, &err) == -1) {
		roar_vs_close(vss, ROAR_VS_TRUE, NULL);
		_err_to_errno();
		return -OP_ERROR_ERRNO;
	}

	ROAR_DBG("op_roar_open(*) = ?");

	ret = _set_role();
	if (ret != 0) {
		roar_vs_close(vss, ROAR_VS_TRUE, NULL);
		_err_to_errno();
		return ret;
	}

	ROAR_DBG("op_roar_open(*) = ?");

	roar_vs_blocking(vss, ROAR_VS_FALSE, &err);

	ROAR_DBG("op_roar_open(*) = 0");

	return 0;
}

static int op_roar_close(void)
{
	roar_vs_close(vss, ROAR_VS_FALSE, &err);
	return 0;
}

static int op_roar_drop(void)
{
	roar_vs_reset_buffer(vss, ROAR_VS_TRUE, ROAR_VS_TRUE, &err);
	return 0;
}

static int op_roar_write(const char *buffer, int count)
{
	int ret;
	int i;
	ret = roar_vs_write(vss, buffer, count, &err);
	for (i = 0; i < 8; i++)
		roar_vs_iterate(vss, ROAR_VS_NOWAIT, NULL);
	return ret;
}

static int op_roar_buffer_space(void)
{
	ssize_t ret;
	int i;
	int fs = sf_get_frame_size(format);

	for (i = 0; i < 8; i++)
		roar_vs_iterate(vss, ROAR_VS_NOWAIT, NULL);

	ret = roar_vs_get_avail_write(vss, &err);

	ret -= ret % fs;

	return ret;
}

static int op_roar_pause(void) {
	if (roar_vs_pause(vss, ROAR_VS_TRUE, &err) == -1) {
		_err_to_errno();
		return -OP_ERROR_ERRNO;
	}
	return 0;
}
static int op_roar_unpause(void) {
	if (roar_vs_pause(vss, ROAR_VS_FALSE, &err) == -1) {
		_err_to_errno();
		return -OP_ERROR_ERRNO;
	}
	return 0;
}


static int op_roar_set_option(int key, const char *val)
{
	switch (key) {
	case 0:
		if (host != NULL)
			free(host);
		host = xstrdup(val);
		break;
	case 1:
		if (role != NULL)
			free(role);
		role = xstrdup(val);
		_set_role();
		break;
	default:
		return -OP_ERROR_NOT_OPTION;
	}
	return 0;
}

static int op_roar_get_option(int key, char **val)
{
	switch (key) {
	case 0:
		if (host != NULL)
			*val = xstrdup(host);
		break;
	case 1:
		if (role != NULL)
			*val = xstrdup(role);
		break;
	default:
		return -OP_ERROR_NOT_OPTION;
	}
	return 0;
}

static int op_roar_mixer_open(int *volume_max)
{
	*volume_max = MIXER_BASE_VOLUME;
	return 0;
}

static int op_roar_mixer_set_volume(int l, int r)
{
	float lf, rf;

	if (vss == NULL)
		return -OP_ERROR_NOT_OPEN;

	lf = (float)l / (float)MIXER_BASE_VOLUME;
	rf = (float)r / (float)MIXER_BASE_VOLUME;

	if (roar_vs_volume_stereo(vss, lf, rf, &err) == -1) {
		_err_to_errno();
		return -OP_ERROR_ERRNO;
	}

	return 0;
}
static int op_roar_mixer_get_volume(int *l, int *r)
{
	float lf, rf;

	if (vss == NULL)
		return -OP_ERROR_NOT_OPEN;

	if (roar_vs_volume_get(vss, &lf, &rf, &err) == -1) {
		_err_to_errno();
		return -OP_ERROR_ERRNO;
	}

	lf *= (float)MIXER_BASE_VOLUME;
	rf *= (float)MIXER_BASE_VOLUME;

	*l = lf;
	*r = rf;

	return 0;
}

static int op_roar_mixer_set_option(int key, const char *val)
{
	return -OP_ERROR_NOT_OPTION;
}
static int op_roar_mixer_get_option(int key, char **val)
{
	return -OP_ERROR_NOT_OPTION;
}

const struct output_plugin_ops op_pcm_ops = {
	.init = op_roar_init,
	.exit = op_roar_exit,
	.open = op_roar_open,
	.close = op_roar_close,
	.drop = op_roar_drop,
	.write = op_roar_write,
	.buffer_space = op_roar_buffer_space,
	.pause = op_roar_pause,
	.unpause = op_roar_unpause,
	.set_option = op_roar_set_option,
	.get_option = op_roar_get_option
};

const char * const op_pcm_options[] = {
	"server",
	"role",
	NULL
};

const struct mixer_plugin_ops op_mixer_ops = {
	.init = op_roar_dummy,
	.exit = op_roar_dummy,
	.open = op_roar_mixer_open,
	.close = op_roar_dummy,
	.get_fds = NULL,
	.set_volume = op_roar_mixer_set_volume,
	.get_volume = op_roar_mixer_get_volume,
	.set_option = op_roar_mixer_set_option,
	.get_option = op_roar_mixer_get_option
};

const char * const op_mixer_options[] = {
	NULL
};

const int op_priority = -1;
