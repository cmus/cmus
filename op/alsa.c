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

#include <strings.h>

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

static snd_mixer_t *alsa_mixer_handle;
static snd_mixer_elem_t *mixer_elem = NULL;
static long mixer_vol_min, mixer_vol_max;

/* configuration */
static char *alsa_mixer_device = NULL;
static char *alsa_mixer_element = NULL;

static int alsa_mixer_init(void)
{
	if (alsa_mixer_device == NULL)
		alsa_mixer_device = xstrdup("default");
	if (alsa_mixer_element == NULL)
		alsa_mixer_element = xstrdup("PCM");
	/* FIXME: check device */
	return 0;
}

static int alsa_mixer_exit(void)
{
	free(alsa_mixer_device);
	alsa_mixer_device = NULL;
	free(alsa_mixer_element);
	alsa_mixer_element = NULL;
	return 0;
}

static snd_mixer_elem_t *find_mixer_elem_by_name(const char *goal_name)
{
	snd_mixer_elem_t *elem;
	snd_mixer_selem_id_t *sid = NULL;

	snd_mixer_selem_id_malloc(&sid);

	for (elem = snd_mixer_first_elem(alsa_mixer_handle); elem;
		 elem = snd_mixer_elem_next(elem)) {

		const char *name;

		snd_mixer_selem_get_id(elem, sid);
		name = snd_mixer_selem_id_get_name(sid);
		d_print("name = %s\n", name);
		d_print("has playback volume = %d\n", snd_mixer_selem_has_playback_volume(elem));
		d_print("has playback switch = %d\n", snd_mixer_selem_has_playback_switch(elem));

		if (strcasecmp(name, goal_name) == 0) {
			if (!snd_mixer_selem_has_playback_volume(elem)) {
				d_print("mixer element `%s' does not have playback volume\n", name);
				elem = NULL;
			}
			break;
		}
	}

	snd_mixer_selem_id_free(sid);
	return elem;
}

static int alsa_mixer_open(int *volume_max)
{
	snd_mixer_elem_t *elem;
	int count;
	int rc;

	rc = snd_mixer_open(&alsa_mixer_handle, 0);
	if (rc < 0)
		goto error;
	rc = snd_mixer_attach(alsa_mixer_handle, alsa_mixer_device);
	if (rc < 0)
		goto error;
	rc = snd_mixer_selem_register(alsa_mixer_handle, NULL, NULL);
	if (rc < 0)
		goto error;
	rc = snd_mixer_load(alsa_mixer_handle);
	if (rc < 0)
		goto error;
	count = snd_mixer_get_count(alsa_mixer_handle);
	if (count == 0) {
		d_print("error: mixer does not have elements\n");
		return -2;
	}

	elem = find_mixer_elem_by_name(alsa_mixer_element);
	if (!elem) {
		d_print("mixer element `%s' not found, trying `Master'\n", alsa_mixer_element);
		elem = find_mixer_elem_by_name("Master");
		if (!elem) {
			d_print("error: cannot find suitable mixer element\n");
			return -2;
		}
	}
	snd_mixer_selem_get_playback_volume_range(elem, &mixer_vol_min, &mixer_vol_max);
	/* FIXME: get number of channels */
	mixer_elem = elem;
	*volume_max = mixer_vol_max - mixer_vol_min;

	return 0;

error:
	d_print("error: %s\n", snd_strerror(rc));
	return -1;
}

static int alsa_mixer_close(void)
{
	snd_mixer_close(alsa_mixer_handle);
	return 0;
}

static int alsa_mixer_get_fds(int what, int *fds)
{
	struct pollfd pfd[NR_MIXER_FDS];
	int count, i;

	switch (what) {
	case MIXER_FDS_VOLUME:
		count = snd_mixer_poll_descriptors(alsa_mixer_handle, pfd, NR_MIXER_FDS);
		for (i = 0; i < count; i++)
			fds[i] = pfd[i].fd;
		return count;
	default:
		return 0;
	}
}

static int alsa_mixer_set_volume(int l, int r)
{
	if (mixer_elem == NULL) {
		return -1;
	}
	l += mixer_vol_min;
	r += mixer_vol_min;
	if (l > mixer_vol_max)
		d_print("error: left volume too high (%d > %ld)\n",
				l, mixer_vol_max);
	if (r > mixer_vol_max)
		d_print("error: right volume too high (%d > %ld)\n",
				r, mixer_vol_max);
	snd_mixer_selem_set_playback_volume(mixer_elem, SND_MIXER_SCHN_FRONT_LEFT, l);
	snd_mixer_selem_set_playback_volume(mixer_elem, SND_MIXER_SCHN_FRONT_RIGHT, r);
	return 0;
}

static int alsa_mixer_get_volume(int *l, int *r)
{
	long lv, rv;

	if (mixer_elem == NULL)
		return -1;
	snd_mixer_handle_events(alsa_mixer_handle);
	snd_mixer_selem_get_playback_volume(mixer_elem, SND_MIXER_SCHN_FRONT_LEFT, &lv);
	snd_mixer_selem_get_playback_volume(mixer_elem, SND_MIXER_SCHN_FRONT_RIGHT, &rv);
	*l = lv - mixer_vol_min;
	*r = rv - mixer_vol_min;
	return 0;
}

static int alsa_mixer_set_channel(const char *val)
{
	free(alsa_mixer_element);
	alsa_mixer_element = xstrdup(val);
	return 0;
}

static int alsa_mixer_get_channel(char **val)
{
	if (alsa_mixer_element)
		*val = xstrdup(alsa_mixer_element);
	return 0;
}

static int alsa_mixer_set_device(const char *val)
{
	free(alsa_mixer_device);
	alsa_mixer_device = xstrdup(val);
	return 0;
}

static int alsa_mixer_get_device(char **val)
{
	if (alsa_mixer_device)
		*val = xstrdup(alsa_mixer_device);
	return 0;
}

CMUS_OP_DEFINE(
	.priority = 0,
	.pcm_ops = &(struct output_plugin_ops){
		.init = op_alsa_init,
		.exit = op_alsa_exit,
		.open = op_alsa_open,
		.close = op_alsa_close,
		.drop = op_alsa_drop,
		.write = op_alsa_write,
		.buffer_space = op_alsa_buffer_space,
		.pause = op_alsa_pause,
		.unpause = op_alsa_unpause,
	},
	.pcm_options = (struct output_plugin_opt[]){
		OPT(op_alsa, device),
		{ NULL },
	},
	.mixer_ops = &(struct mixer_plugin_ops){
		.init = alsa_mixer_init,
		.exit = alsa_mixer_exit,
		.open = alsa_mixer_open,
		.close = alsa_mixer_close,
		.get_fds = alsa_mixer_get_fds,
		.set_volume = alsa_mixer_set_volume,
		.get_volume = alsa_mixer_get_volume,
	},
	.mixer_options = (struct mixer_plugin_opt[]) {
		OPT(alsa_mixer, channel),
		OPT(alsa_mixer, device),
		{ NULL },
	},
);
