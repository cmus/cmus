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

#include <op.h>
#include <utils.h>
#include <xmalloc.h>
#include <sf.h>
#include <debug.h>

#define ALSA_PCM_NEW_HW_PARAMS_API
#define ALSA_PCM_NEW_SW_PARAMS_API

#include <alsa/asoundlib.h>

/* without one of these play-back won't start */
#define SET_BUFFER_TIME
#define SET_PERIOD_TIME

#define SET_AVAIL_MIN

/* with this alsa hangs sometimes (ogg, not enough data with first write?) */
/* #define SET_START_THRESHOLD */

static inline const char *state_to_str(snd_pcm_state_t state)
{
	static const char *states[] = {
		"SND_PCM_STATE_OPEN(0)",
		"SND_PCM_STATE_SETUP(1)",
		"SND_PCM_STATE_PREPARED(2)",
		"SND_PCM_STATE_RUNNING(3)",
		"SND_PCM_STATE_XRUN(4)",
		"SND_PCM_STATE_DRAINING(5)",
		"SND_PCM_STATE_PAUSED(6)",
		"SND_PCM_STATE_SUSPENDED(7)",
		"SND_PCM_STATE_DISCONNECTED(8)"
	};

	BUG_ON(state > 8);
	return states[state];
}

static sample_format_t alsa_sf;
static snd_pcm_t *alsa_handle;
static snd_pcm_format_t alsa_fmt;
static int alsa_can_pause;
static snd_pcm_status_t *status;

/* bytes (bits * channels / 8) */
static int alsa_frame_size;

/* configuration */
static char *alsa_dsp_device = NULL;

#ifdef SET_START_THRESHOLD
static int alsa_buffer_size;
#endif

#ifdef SET_AVAIL_MIN
static int alsa_period_size;
#endif

#if 0
#define debug_ret(func, ret) \
	d_print("%s returned %d %s\n", func, ret, ret < 0 ? snd_strerror(ret) : "")
#else
#define debug_ret(func, ret) do { } while (0)
#endif

static int alsa_error_to_op_error(int err)
{
	BUG_ON(err >= 0);
	err = -err;
	if (err < SND_ERROR_BEGIN) {
		errno = err;
		return -OP_ERROR_ERRNO;
	}
	return -OP_ERROR_INTERNAL;
}

static int op_alsa_init(void)
{
	int rc;

	if (alsa_dsp_device == NULL)
		alsa_dsp_device = xstrdup("default");
	rc = snd_pcm_status_malloc(&status);
	if (rc < 0) {
		free(alsa_dsp_device);
		alsa_dsp_device = NULL;
		errno = ENOMEM;
		return -OP_ERROR_ERRNO;
	}
	return 0;
}

static int op_alsa_exit(void)
{
	snd_pcm_status_free(status);
	free(alsa_dsp_device);
	alsa_dsp_device = NULL;
	return 0;
}

/* randomize hw params */
static int alsa_set_hw_params(void)
{
	snd_pcm_hw_params_t *hwparams;
	const char *cmd;
	unsigned int rate;
	int rc, dir;
#if defined(SET_AVAIL_MIN) || defined(SET_START_THRESHOLD)
	snd_pcm_uframes_t frames;
#endif
#ifdef SET_BUFFER_TIME
	unsigned int alsa_buffer_time = 500e3;
#endif
#ifdef SET_PERIOD_TIME
	unsigned int alsa_period_time = 50e3;
#endif

	snd_pcm_hw_params_alloca(&hwparams);

	cmd = "snd_pcm_hw_params_any";
	rc = snd_pcm_hw_params_any(alsa_handle, hwparams);
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

#ifdef SET_BUFFER_TIME
	cmd = "snd_pcm_hw_params_set_buffer_time_near";
	dir = 0;
	rc = snd_pcm_hw_params_set_buffer_time_near(alsa_handle, hwparams, &alsa_buffer_time, &dir);
	if (rc < 0)
		goto error;
#endif

#ifdef SET_PERIOD_TIME
	cmd = "snd_pcm_hw_params_set_period_time_near";
	dir = 0;
	rc = snd_pcm_hw_params_set_period_time_near(alsa_handle, hwparams, &alsa_period_time, &dir);
	if (rc < 0)
		goto error;
#endif

#ifdef SET_AVAIL_MIN
	rc = snd_pcm_hw_params_get_period_size(hwparams, &frames, &dir);
	if (rc < 0) {
		alsa_period_size = -1;
	} else {
		alsa_period_size = frames * alsa_frame_size;
	}
	d_print("period_size = %d (dir = %d)\n", alsa_period_size, dir);
#endif

#ifdef SET_START_THRESHOLD
	rc = snd_pcm_hw_params_get_buffer_size(hwparams, &frames);
	if (rc < 0) {
		alsa_buffer_size = -1;
	} else {
		alsa_buffer_size = frames * alsa_frame_size;
	}
	d_print("buffer_size = %d\n", alsa_buffer_size);
#endif

	cmd = "snd_pcm_hw_params";
	rc = snd_pcm_hw_params(alsa_handle, hwparams);
	if (rc < 0)
		goto error;
	return 0;
error:
	d_print("%s: error: %s\n", cmd, snd_strerror(rc));
	return rc;
}

/* randomize sw params */
static int alsa_set_sw_params(void)
{
#if defined(SET_START_THRESHOLD) || defined(SET_AVAIL_MIN)
	snd_pcm_sw_params_t *swparams;
	const char *cmd;
	int rc;

	/* allocate the software parameter structure */
	snd_pcm_sw_params_alloca(&swparams);

	/* fetch the current software parameters */
	cmd = "snd_pcm_sw_params_current";
	rc = snd_pcm_sw_params_current(alsa_handle, swparams);
	if (rc < 0)
		goto error;

#ifdef SET_START_THRESHOLD
	if (alsa_buffer_size > 0) {
		/* start the transfer when N frames available */
		cmd = "snd_pcm_sw_params_set_start_threshold";
		/* start playing when hardware buffer is full (64 kB, 372 ms) */
		rc = snd_pcm_sw_params_set_start_threshold(alsa_handle, swparams, alsa_buffer_size / alsa_frame_size);
		if (rc < 0)
			goto error;
	}
#endif

#ifdef SET_AVAIL_MIN
	if (alsa_period_size > 0) {
		snd_pcm_uframes_t frames = alsa_period_size / alsa_frame_size;

		/* minimum avail frames to consider pcm ready. must be power of 2 */
		cmd = "snd_pcm_sw_params_set_avail_min";
		/* underrun when available is <8192 B or 46.5 ms */
		rc = snd_pcm_sw_params_set_avail_min(alsa_handle, swparams, frames);
		if (rc < 0)
			goto error;

		cmd = "snd_pcm_sw_params_set_silence_threshold";
		rc = snd_pcm_sw_params_set_silence_threshold(alsa_handle, swparams, frames);
		if (rc < 0)
			goto error;
	}
#endif

	/* commit the params structure to ALSA */
	cmd = "snd_pcm_sw_params";
	rc = snd_pcm_sw_params(alsa_handle, swparams);
	if (rc < 0)
		goto error;
	return 0;
error:
	d_print("%s: error: %s\n", cmd, snd_strerror(rc));
	return rc;
#else
	return 0;
#endif
}

static int op_alsa_open(sample_format_t sf)
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
	rc = alsa_set_sw_params();
	if (rc)
		goto close_error;

	rc = snd_pcm_prepare(alsa_handle);
	if (rc < 0)
		goto close_error;
	return 0;
close_error:
	snd_pcm_close(alsa_handle);
error:
	return alsa_error_to_op_error(rc);
}

static unsigned int period_fill = 0;

static int op_alsa_write(const char *buffer, int count);

static int op_alsa_close(void)
{
	int rc;

	if (period_fill) {
		char buf[8192];
		int silence_bytes = alsa_period_size - period_fill;

		if (silence_bytes > sizeof(buf)) {
			d_print("silence buf not big enough %d\n", silence_bytes);
			silence_bytes = sizeof(buf);
		}
		d_print("silencing %d bytes\n", silence_bytes);
		snd_pcm_format_set_silence(alsa_fmt, buf, silence_bytes / sf_get_sample_size(alsa_sf));
		op_alsa_write(buf, silence_bytes);
		period_fill = 0;
	}

	rc = snd_pcm_drain(alsa_handle);
	debug_ret("snd_pcm_drain", rc);

	rc = snd_pcm_close(alsa_handle);
	debug_ret("snd_pcm_close", rc);
	return 0;
}

static int op_alsa_drop(void)
{
	int rc;

	period_fill = 0;

	/* infinite timeout */
	rc = snd_pcm_wait(alsa_handle, -1);
	debug_ret("snd_pcm_wait", rc);

	rc = snd_pcm_drop(alsa_handle);
	debug_ret("snd_pcm_drop", rc);

	rc = snd_pcm_prepare(alsa_handle);
	debug_ret("snd_pcm_prepare", rc);

	/* drop set state to SETUP
	 * prepare set state to PREPARED
	 *
	 * so if old state was PAUSED we can't UNPAUSE (see op_alsa_unpause)
	 */
	return 0;
}

static int op_alsa_write(const char *buffer, int count)
{
	int rc, len;
	int prepared = 0;

	len = count / alsa_frame_size;
again:
	rc = snd_pcm_writei(alsa_handle, buffer, len);
	if (rc < 0) {
		if (!prepared && rc == -EPIPE) {
			d_print("underrun. resetting stream\n");
			snd_pcm_prepare(alsa_handle);
			prepared++;
			goto again;
		}

		/* this handles EAGAIN too which is not critical error */
		return alsa_error_to_op_error(rc);
	}

	rc *= alsa_frame_size;
	period_fill += rc;
	period_fill %= alsa_period_size;
	return rc;
}

static int op_alsa_buffer_space(void)
{
	int rc;
	snd_pcm_uframes_t f;

	rc = snd_pcm_status(alsa_handle, status);
	if (rc < 0) {
		debug_ret("snd_pcm_status", rc);
		return alsa_error_to_op_error(rc);
	}

	f = snd_pcm_status_get_avail(status);
	if (f > 1e6) {
		d_print("snd_pcm_status_get_avail returned huge number: %lu\n", f);
		f = 1024;
	}
	return f * alsa_frame_size;
}

static int op_alsa_pause(void)
{
	if (alsa_can_pause) {
		snd_pcm_state_t state;
		int rc;

		state = snd_pcm_state(alsa_handle);
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
		}
	}
	return 0;
}

static int op_alsa_unpause(void)
{
	if (alsa_can_pause) {
		snd_pcm_state_t state;
		int rc;

		state = snd_pcm_state(alsa_handle);
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
		}
	}
	return 0;
}

static int op_alsa_set_option(int key, const char *val)
{
	switch (key) {
	case 0:
		free(alsa_dsp_device);
		alsa_dsp_device = xstrdup(val);
		break;
	default:
		return -OP_ERROR_NOT_OPTION;
	}
	return 0;
}

static int op_alsa_get_option(int key, char **val)
{
	switch (key) {
	case 0:
		if (alsa_dsp_device)
			*val = xstrdup(alsa_dsp_device);
		break;
	default:
		return -OP_ERROR_NOT_OPTION;
	}
	return 0;
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
	.set_option = op_alsa_set_option,
	.get_option = op_alsa_get_option
};

const char * const op_pcm_options[] = {
	"device",
	NULL
};

const int op_priority = 0;
