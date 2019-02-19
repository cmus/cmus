/*
 * Copyright 2008-2013 Various Authors
 * Copyright 2008 Jonathan Kleinehellefort
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

/*
 * snd_pcm_state_t:
 *
 * Open
 * SND_PCM_STATE_OPEN = 0,
 *
 * Setup installed
 * SND_PCM_STATE_SETUP = 1,
 *
 * Ready to start
 * SND_PCM_STATE_PREPARED = 2,
 *
 * Running
 * SND_PCM_STATE_RUNNING = 3,
 *
 * Stopped: underrun (playback) or overrun (capture) detected
 * SND_PCM_STATE_XRUN = 4,
 *
 * Draining: running (playback) or stopped (capture)
 * SND_PCM_STATE_DRAINING = 5,
 *
 * Paused
 * SND_PCM_STATE_PAUSED = 6,
 *
 * Hardware is suspended
 * SND_PCM_STATE_SUSPENDED = 7,
 *
 * Hardware is disconnected
 * SND_PCM_STATE_DISCONNECTED = 8,
 */

#include "../op.h"
#include "../utils.h"
#include "../xmalloc.h"
#include "../sf.h"
#include "../debug.h"

#define ALSA_PCM_NEW_HW_PARAMS_API
#define ALSA_PCM_NEW_SW_PARAMS_API

#include <alsa/asoundlib.h>

static sample_format_t alsa_sf;
static snd_pcm_t *alsa_handle;
static snd_pcm_format_t alsa_fmt;
static int alsa_can_pause;
static snd_pcm_status_t *status;

/* bytes (bits * channels / 8) */
static int alsa_frame_size;

/* configuration */
static char *alsa_dsp_device = NULL;

#if 0
#define debug_ret(func, ret) \
	d_print("%s returned %d %s\n", func, ret, ret < 0 ? snd_strerror(ret) : "")
#else
#define debug_ret(func, ret) do { } while (0)
#endif

static int alsa_error_to_op_error(int err)
{
	if (!err)
		return OP_ERROR_SUCCESS;
	err = -err;
	if (err < SND_ERROR_BEGIN) {
		errno = err;
		return -OP_ERROR_ERRNO;
	}
	return -OP_ERROR_INTERNAL;
}

/* we don't want error messages to stderr */
static void error_handler(const char *file, int line, const char *function, int err, const char *fmt, ...)
{
}

static int op_alsa_init(void)
{
	int rc;

	snd_lib_error_set_handler(error_handler);

	if (alsa_dsp_device == NULL)
		alsa_dsp_device = xstrdup("default");
	rc = snd_pcm_status_malloc(&status);
	if (rc < 0) {
		free(alsa_dsp_device);
		alsa_dsp_device = NULL;
		errno = ENOMEM;
		return -OP_ERROR_ERRNO;
	}
	return OP_ERROR_SUCCESS;
}

static int op_alsa_exit(void)
{
	snd_pcm_status_free(status);
	free(alsa_dsp_device);
	alsa_dsp_device = NULL;
	return OP_ERROR_SUCCESS;
}

/* randomize hw params */
static int alsa_set_hw_params(void)
{
	snd_pcm_hw_params_t *hwparams = NULL;
	unsigned int buffer_time_max = 300 * 1000; /* us */
	const char *cmd;
	unsigned int rate;
	int rc, dir;

	snd_pcm_hw_params_malloc(&hwparams);

	cmd = "snd_pcm_hw_params_any";
	rc = snd_pcm_hw_params_any(alsa_handle, hwparams);
	if (rc < 0)
		goto error;

	cmd = "snd_pcm_hw_params_set_buffer_time_max";
	rc = snd_pcm_hw_params_set_buffer_time_max(alsa_handle, hwparams,
	                                           &buffer_time_max, &dir);
	if (rc < 0)
		goto error;

	alsa_can_pause = snd_pcm_hw_params_can_pause(hwparams);
	d_print("can pause = %d\n", alsa_can_pause);

	cmd = "snd_pcm_hw_params_set_access";
	rc = snd_pcm_hw_params_set_access(alsa_handle, hwparams,
			SND_PCM_ACCESS_RW_INTERLEAVED);
	if (rc < 0)
		goto error;

	alsa_fmt = snd_pcm_build_linear_format(sf_get_bits(alsa_sf), sf_get_bits(alsa_sf),
			sf_get_signed(alsa_sf) ? 0 : 1,
			sf_get_bigendian(alsa_sf));
	cmd = "snd_pcm_hw_params_set_format";
	rc = snd_pcm_hw_params_set_format(alsa_handle, hwparams, alsa_fmt);
	if (rc < 0)
		goto error;

	cmd = "snd_pcm_hw_params_set_channels";
	rc = snd_pcm_hw_params_set_channels(alsa_handle, hwparams, sf_get_channels(alsa_sf));
	if (rc < 0)
		goto error;

	cmd = "snd_pcm_hw_params_set_rate";
	rate = sf_get_rate(alsa_sf);
	dir = 0;
	rc = snd_pcm_hw_params_set_rate_near(alsa_handle, hwparams, &rate, &dir);
	if (rc < 0)
		goto error;
	d_print("rate=%d\n", rate);

	cmd = "snd_pcm_hw_params";
	rc = snd_pcm_hw_params(alsa_handle, hwparams);
	if (rc < 0)
		goto error;
	goto out;
error:
	d_print("%s: error: %s\n", cmd, snd_strerror(rc));
out:
	snd_pcm_hw_params_free(hwparams);
	return rc;
}

static int op_alsa_open(sample_format_t sf, const channel_position_t *channel_map)
{
	int rc;

	alsa_sf = sf;
	alsa_frame_size = sf_get_frame_size(alsa_sf);

	rc = snd_pcm_open(&alsa_handle, alsa_dsp_device, SND_PCM_STREAM_PLAYBACK, 0);
	if (rc < 0)
		goto error;

	rc = alsa_set_hw_params();
	if (rc)
		goto close_error;

	rc = snd_pcm_prepare(alsa_handle);
	if (rc < 0)
		goto close_error;
	return OP_ERROR_SUCCESS;
close_error:
	snd_pcm_close(alsa_handle);
error:
	return alsa_error_to_op_error(rc);
}

static int op_alsa_write(const char *buffer, int count);

static int op_alsa_close(void)
{
	int rc;

	rc = snd_pcm_drain(alsa_handle);
	debug_ret("snd_pcm_drain", rc);

	rc = snd_pcm_close(alsa_handle);
	debug_ret("snd_pcm_close", rc);
	return alsa_error_to_op_error(rc);
}

static int op_alsa_drop(void)
{
	int rc;

	rc = snd_pcm_drop(alsa_handle);
	debug_ret("snd_pcm_drop", rc);

	rc = snd_pcm_prepare(alsa_handle);
	debug_ret("snd_pcm_prepare", rc);

	/* drop set state to SETUP
	 * prepare set state to PREPARED
	 *
	 * so if old state was PAUSED we can't UNPAUSE (see op_alsa_unpause)
	 */
	return alsa_error_to_op_error(rc);
}

static int op_alsa_write(const char *buffer, int count)
{
	int rc, len;
	int recovered = 0;

	len = count / alsa_frame_size;
again:
	rc = snd_pcm_writei(alsa_handle, buffer, len);
	if (rc < 0) {
		// rc _should_ be either -EBADFD, -EPIPE or -ESTRPIPE
		if (!recovered && (rc == -EINTR || rc == -EPIPE || rc == -ESTRPIPE)) {
			d_print("snd_pcm_writei failed: %s, trying to recover\n",
					snd_strerror(rc));
			recovered++;
			// this handles -EINTR, -EPIPE and -ESTRPIPE
			// for other errors it just returns the error code
			rc = snd_pcm_recover(alsa_handle, rc, 1);
			if (!rc)
				goto again;
		}

		/* this handles EAGAIN too which is not critical error */
		return alsa_error_to_op_error(rc);
	}

	rc *= alsa_frame_size;
	return rc;
}

static int op_alsa_buffer_space(void)
{
	int rc;
	snd_pcm_sframes_t f;

	f = snd_pcm_avail_update(alsa_handle);
	while (f < 0) {
		d_print("snd_pcm_avail_update failed: %s, trying to recover\n",
			snd_strerror(f));
		rc = snd_pcm_recover(alsa_handle, f, 1);
		if (rc < 0) {
			d_print("recovery failed: %s\n", snd_strerror(rc));
			return alsa_error_to_op_error(rc);
		}
		f = snd_pcm_avail_update(alsa_handle);
	}

	return f * alsa_frame_size;
}

static int op_alsa_pause(void)
{
	int rc = 0;
	if (alsa_can_pause) {
		snd_pcm_state_t state = snd_pcm_state(alsa_handle);
		if (state == SND_PCM_STATE_PREPARED) {
			// state is PREPARED -> no need to pause
		} else if (state == SND_PCM_STATE_RUNNING) {
			// state is RUNNING - > pause

			// infinite timeout
			rc = snd_pcm_wait(alsa_handle, -1);
			debug_ret("snd_pcm_wait", rc);

			rc = snd_pcm_pause(alsa_handle, 1);
			debug_ret("snd_pcm_pause", rc);
		} else {
			d_print("error: state is not RUNNING or PREPARED\n");
			rc = -OP_ERROR_INTERNAL;
		}
	} else {
		rc = snd_pcm_drop(alsa_handle);
		debug_ret("snd_pcm_drop", rc);
	}
	return alsa_error_to_op_error(rc);
}

static int op_alsa_unpause(void)
{
	int rc = 0;
	if (alsa_can_pause) {
		snd_pcm_state_t state = snd_pcm_state(alsa_handle);
		if (state == SND_PCM_STATE_PREPARED) {
			// state is PREPARED -> no need to unpause
		} else if (state == SND_PCM_STATE_PAUSED) {
			// state is PAUSED -> unpause

			// infinite timeout
			rc = snd_pcm_wait(alsa_handle, -1);
			debug_ret("snd_pcm_wait", rc);

			rc = snd_pcm_pause(alsa_handle, 0);
			debug_ret("snd_pcm_pause", rc);
		} else {
			d_print("error: state is not PAUSED nor PREPARED\n");
			rc = -OP_ERROR_INTERNAL;
		}
	} else {
		rc = snd_pcm_prepare(alsa_handle);
		debug_ret("snd_pcm_prepare", rc);
	}
	return alsa_error_to_op_error(rc);
}

static int op_alsa_set_device(const char *val)
{
	free(alsa_dsp_device);
	alsa_dsp_device = xstrdup(val);
	return OP_ERROR_SUCCESS;
}

static int op_alsa_get_device(char **val)
{
	if (alsa_dsp_device)
		*val = xstrdup(alsa_dsp_device);
	return OP_ERROR_SUCCESS;
}

const struct output_plugin_ops op_pcm_ops = {
	.init = op_alsa_init,
	.exit = op_alsa_exit,
	.open = op_alsa_open,
	.close = op_alsa_close,
	.drop = op_alsa_drop,
	.write = op_alsa_write,
	.buffer_space = op_alsa_buffer_space,
	.pause = op_alsa_pause,
	.unpause = op_alsa_unpause,
};

const struct output_plugin_opt op_pcm_options[] = {
	OPT(op_alsa, device),
	{ NULL },
};

const int op_priority = 0;
const unsigned op_abi_version = OP_ABI_VERSION;
