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

#include <op_alsa.h>
#include <utils.h>
#include <xmalloc.h>
#include <sf.h>
#include <debug.h>

#define ALSA_PCM_NEW_HW_PARAMS_API
#define ALSA_PCM_NEW_SW_PARAMS_API

#include <alsa/asoundlib.h>

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

static int alsa_buffer_size;
static int alsa_period_size;

/* configuration */
static char *alsa_dsp_device = NULL;
static unsigned int alsa_buffer_time = 500e3;
static unsigned int alsa_period_time = 50e3;

#define SET_BUFFERTIME

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
		return -1;
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
	snd_pcm_uframes_t frames;
	const char *cmd;
	unsigned int rate;
	int rc, fmt, dir;

	snd_pcm_hw_params_alloca(&hwparams);

	cmd = "snd_pcm_hw_params_any";
	rc = snd_pcm_hw_params_any(alsa_handle, hwparams);
	if (rc < 0)
		goto error;

	cmd = "snd_pcm_hw_params_set_access";
	rc = snd_pcm_hw_params_set_access(alsa_handle, hwparams,
			SND_PCM_ACCESS_RW_INTERLEAVED);
	if (rc < 0)
		goto error;

	fmt = snd_pcm_build_linear_format(sf_get_bits(alsa_sf), sf_get_bits(alsa_sf),
			sf_get_signed(alsa_sf) ? 0 : 1,
			sf_get_bigendian(alsa_sf));
	cmd = "snd_pcm_hw_params_set_format";
	rc = snd_pcm_hw_params_set_format(alsa_handle, hwparams, fmt);
	if (rc < 0)
		goto error;

	alsa_fmt = fmt;

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

#if defined(SET_BUFFERTIME)
	/* fscking alsa */
	alsa_buffer_time = 500e3;
	alsa_period_time = 50e3;

	cmd = "snd_pcm_hw_params_set_buffer_time_near";
	rc = snd_pcm_hw_params_set_buffer_time_near(alsa_handle, hwparams, &alsa_buffer_time, 0);
	if (rc < 0)
		goto error;

	cmd = "snd_pcm_hw_params_set_period_time_near";
	rc = snd_pcm_hw_params_set_period_time_near(alsa_handle, hwparams, &alsa_period_time, 0);
	if (rc < 0)
		goto error;
#endif

	alsa_can_pause = snd_pcm_hw_params_can_pause(hwparams);
	d_print("can pause = %d\n", alsa_can_pause);

	rc = snd_pcm_hw_params_get_period_size(hwparams, &frames, &dir);
	if (rc < 0) {
		alsa_period_size = -1;
	} else {
		alsa_period_size = frames * alsa_frame_size;
	}

	rc = snd_pcm_hw_params_get_buffer_size(hwparams, &frames);
	if (rc < 0) {
		alsa_buffer_size = -1;
	} else {
		alsa_buffer_size = frames * alsa_frame_size;
	}
	d_print("period_size = %d (dir = %d), buffer_size = %d\n",
			alsa_period_size, dir, alsa_buffer_size);

	cmd = "snd_pcm_hw_params";
	rc = snd_pcm_hw_params(alsa_handle, hwparams);
	if (rc < 0)
		goto error;
	return 0;
error:
	d_print("%s: error: %s\n", cmd, snd_strerror(rc));
	snd_pcm_close(alsa_handle);
	/* FIXME: return code */
	errno = EINVAL;
	return -1;
}

/* randomize sw params */
static int alsa_set_sw_params(void)
{
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

#if 0
	if (alsa_buffer_size > 0) {
		/* start the transfer when N frames available */
		cmd = "snd_pcm_sw_params_set_start_threshold";
		/* start playing when hardware buffer is full (64 kB, 372 ms) */
		rc = snd_pcm_sw_params_set_start_threshold(alsa_handle, swparams, alsa_buffer_size / alsa_frame_size);
		if (rc < 0)
			goto error;
	}
#endif
#if 1
	if (alsa_period_size > 0) {
		/* minimum avail frames to consider pcm ready. must be power of 2 */
		cmd = "snd_pcm_sw_params_set_avail_min";
		/* underrun when available is <8192 B or 46.5 ms */
		rc = snd_pcm_sw_params_set_avail_min(alsa_handle, swparams, alsa_period_size / alsa_frame_size);
		if (rc < 0)
			goto error;
	}
#endif
#if 0

	/* do not align transfers */
	cmd = "snd_pcm_sw_params_set_xfer_align";
	rc = snd_pcm_sw_params_set_xfer_align(alsa_handle, swparams, 1);
	if (rc < 0)
		goto error;
#endif

	/* commit the params structure to ALSA */
	cmd = "snd_pcm_sw_params";
	rc = snd_pcm_sw_params(alsa_handle, swparams);
	if (rc < 0)
		goto error;
	return 0;
error:
	d_print("%s: error: %s\n", cmd, snd_strerror(rc));
	snd_pcm_close(alsa_handle);
	/* FIXME: return code */
	errno = EINVAL;
	return -1;
}

static int op_alsa_open(sample_format_t sf)
{
	int rc;

	alsa_sf = sf;
	alsa_frame_size = sf_get_frame_size(alsa_sf);

	rc = snd_pcm_open(&alsa_handle, alsa_dsp_device, SND_PCM_STREAM_PLAYBACK, 0);
	if (rc < 0)
		return rc;

	rc = alsa_set_hw_params();
	if (rc)
		return rc;
	rc = alsa_set_sw_params();
	if (rc)
		return rc;

	rc = snd_pcm_prepare(alsa_handle);
	if (rc < 0) {
		d_print("%s: error: %s\n", "snd_pcm_prepare", snd_strerror(rc));
		snd_pcm_close(alsa_handle);
		/* FIXME: return code */
		return -1;
	}
	d_print("opened\n");
	return 0;
}

static int op_alsa_close(void)
{
	int rc;

	rc = snd_pcm_drain(alsa_handle);
/* 	d_print("snd_pcm_drain returned %d %s\n", rc, rc < 0 ? snd_strerror(rc) : ""); */
	rc = snd_pcm_close(alsa_handle);
/* 	d_print("snd_pcm_close returned %d %s\n", rc, rc < 0 ? snd_strerror(rc) : ""); */
	return 0;
}

static int op_alsa_drop(void)
{
	int rc;
/* 	snd_pcm_state_t old_state; */

	/* infinite timeout */
	rc = snd_pcm_wait(alsa_handle, -1);
/* 	d_print("snd_pcm_wait returned %d %s\n", rc, rc < 0 ? snd_strerror(rc) : ""); */

/*
 * 	old_state = snd_pcm_state(alsa_handle);
 * 	d_print("pcm state is %s\n", state_to_str(snd_pcm_state(alsa_handle)));
 */

	rc = snd_pcm_drop(alsa_handle);
/* 	d_print("snd_pcm_drop returned %d %s\n", rc, rc < 0 ? snd_strerror(rc) : ""); */
	rc = snd_pcm_prepare(alsa_handle);
/* 	d_print("snd_pcm_prepare returned %d %s\n", rc, rc < 0 ? snd_strerror(rc) : ""); */

/* 	d_print("pcm state is %s\n", state_to_str(snd_pcm_state(alsa_handle))); */

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

/* 	d_print("start\n"); */

	/* is this necessary? doesn't snd_pcm_writei return -EAGAIN if
	 * hardware not ready? */
	//rc = snd_pcm_wait(alsa_handle, 5);

	len = count / alsa_frame_size;
	rc = snd_pcm_writei(alsa_handle, buffer, len);
	if (rc >= 0) {
		return rc * alsa_frame_size;
	} else if (rc == -EPIPE) {
		d_print("underrun. resetting stream\n");
		snd_pcm_prepare(alsa_handle);
		rc = snd_pcm_writei(alsa_handle, buffer, len);
		if (rc < 0) {
			d_print("write error: %s\n", snd_strerror(rc));
			return -1;
		}
		return rc * alsa_frame_size;
	} else if (rc == -EAGAIN) {
		errno = EAGAIN;
		return -1;
	} else {
		return -1;
	}
}

static int op_alsa_buffer_space(void)
{
	int rc;
	snd_pcm_uframes_t f;

	rc = snd_pcm_status(alsa_handle, status);
	if (rc < 0) {
		d_print("snd_pcm_status returned %d %s\n", rc, snd_strerror(rc));
		return -1;
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
/* 		d_print("pcm state is %s\n", state_to_str(state)); */
		if (state == SND_PCM_STATE_PREPARED) {
			// state is PREPARED -> no need to pause
		} else if (state == SND_PCM_STATE_RUNNING) {
			// state is RUNNING - > pause

			// infinite timeout
			rc = snd_pcm_wait(alsa_handle, -1);
/* 			d_print("snd_pcm_wait returned %d %s\n", rc, rc < 0 ? snd_strerror(rc) : ""); */

			rc = snd_pcm_pause(alsa_handle, 1);
/* 			d_print("snd_pcm_pause returned %d %s\n", rc, rc < 0 ? snd_strerror(rc) : ""); */
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
/* 		d_print("pcm state is %s\n", state_to_str(state)); */
		if (state == SND_PCM_STATE_PREPARED) {
			// state is PREPARED -> no need to unpause
		} else if (state == SND_PCM_STATE_PAUSED) {
			// state is PAUSED -> unpause

			// infinite timeout
			rc = snd_pcm_wait(alsa_handle, -1);
/* 			d_print("snd_pcm_wait returned %d %s\n", rc, rc < 0 ? snd_strerror(rc) : ""); */

			rc = snd_pcm_pause(alsa_handle, 0);
/* 			d_print("snd_pcm_pause returned %d %s\n", rc, rc < 0 ? snd_strerror(rc) : ""); */
		} else {
			d_print("error: state is not PAUSED nor PREPARED\n");
		}
	}
	return 0;
}

static int op_alsa_set_option(int key, const char *val)
{
	long int ival;

	switch (key) {
	case 0:
		if (str_to_int(val, &ival) || ival <= 0) {
			errno = EINVAL;
			return -OP_ERROR_ERRNO;
		}
		alsa_buffer_time = ival;
		break;
	case 1:
		free(alsa_dsp_device);
		alsa_dsp_device = xstrdup(val);
		break;
	case 2:
		if (str_to_int(val, &ival) || ival <= 0) {
			errno = EINVAL;
			return -OP_ERROR_ERRNO;
		}
		alsa_period_time = ival;
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
		*val = xnew(char, 22);
		snprintf(*val, 22, "%d", alsa_buffer_time);
		break;
	case 1:
		*val = xstrdup(alsa_dsp_device);
		break;
	case 2:
		*val = xnew(char, 22);
		snprintf(*val, 22, "%d", alsa_period_time);
		break;
	default:
		*val = NULL;
		return -OP_ERROR_NOT_OPTION;
	}
	return 0;
}

const struct output_plugin_ops op_alsa_ops = {
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

const char *op_alsa_options[] = {
	"buffer_time",
	"device",
	"period_time",
	NULL
};
