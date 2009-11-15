/*
 * Copyright 2009 Gregory Petrosyan
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

#include <sys/types.h>
#include <unistd.h>
#include <string.h>

#include <pulse/pulseaudio.h>

#include "op.h"
#include "mixer.h"
#include "debug.h"

static pa_threaded_mainloop	*pa_ml;
static pa_context		*pa_ctx;
static pa_stream		*pa_s;
static pa_channel_map		 pa_cmap;
static pa_cvolume		 pa_vol;

static int		 _pa_context_running;
static int		 _pa_stream_running;

#define ret_pa_error(err)						\
	do {								\
		d_print("pulseaudio error: %s\n", pa_strerror(err));	\
		return -OP_ERROR_INTERNAL;				\
	} while (0)

static int __pa_ret_volume(int *l, int *r)
{
	*l = (int)pa_cvolume_get_position(&pa_vol, &pa_cmap, PA_CHANNEL_POSITION_FRONT_LEFT);
	*r = (int)pa_cvolume_get_position(&pa_vol, &pa_cmap, PA_CHANNEL_POSITION_FRONT_RIGHT);

	return OP_ERROR_SUCCESS;
}

static pa_proplist *__create_app_proplist(void)
{
	const size_t	 BUFSIZE = 1024;

	pa_proplist	*pl;
	char		 buf[BUFSIZE];
	int		 rc;

	pl = pa_proplist_new();
	BUG_ON(!pl);

	rc = pa_proplist_sets(pl, PA_PROP_APPLICATION_NAME, "C* Music Player");
	BUG_ON(rc);

	rc = pa_proplist_sets(pl, PA_PROP_APPLICATION_VERSION, VERSION);
	BUG_ON(rc);

	rc = pa_proplist_setf(pl, PA_PROP_APPLICATION_PROCESS_ID, "%ld", (long)getpid());
	BUG_ON(rc);

	rc = gethostname(buf, BUFSIZE);
	BUG_ON(rc);
	rc = pa_proplist_sets(pl, PA_PROP_APPLICATION_PROCESS_HOST, buf);
	BUG_ON(rc);

	if (pa_get_binary_name(buf, BUFSIZE)) {
		rc = pa_proplist_sets(pl, PA_PROP_APPLICATION_PROCESS_BINARY, buf);
		BUG_ON(rc);
	}

	if (pa_get_user_name(buf, BUFSIZE)) {
		rc = pa_proplist_sets(pl, PA_PROP_APPLICATION_PROCESS_USER, buf);
		BUG_ON(rc);
	}

	/*
	 * Possible todo:
	 * 	- PA_PROP_APPLICATION_PROCESS_SESSION_ID
	 * 	- PA_PROP_APPLICATION_PROCESS_MACHINE_ID
	 */

	return pl;
}

static pa_proplist *__create_stream_proplist(void)
{
	pa_proplist	*pl;
	int		 rc;

	pl = pa_proplist_new();
	BUG_ON(!pl);

	rc = pa_proplist_sets(pl, PA_PROP_MEDIA_ROLE, "music");
	BUG_ON(rc);

	return pl;
}

static const char *__pa_context_state_str(pa_context_state_t s)
{
	switch (s) {
	case PA_CONTEXT_AUTHORIZING:
		return "PA_CONTEXT_AUTHORIZING";
	case PA_CONTEXT_CONNECTING:
		return "PA_CONTEXT_CONNECTING";
	case PA_CONTEXT_FAILED:
		return "PA_CONTEXT_FAILED";
	case PA_CONTEXT_READY:
		return "PA_CONTEXT_READY";
	case PA_CONTEXT_SETTING_NAME:
		return "PA_CONTEXT_SETTING_NAME";
	case PA_CONTEXT_TERMINATED:
		return "PA_CONTEXT_TERMINATED";
	case PA_CONTEXT_UNCONNECTED:
		return "PA_CONTEXT_UNCONNECTED";
	}

	return "unknown";
}

static void __pa_context_running_cb(pa_context *c, void *data)
{
	pa_context_state_t	 s;
	int			*running;

	running = (int*)data;

	s = pa_context_get_state(c);
	switch (s) {
	case PA_CONTEXT_UNCONNECTED:
	case PA_CONTEXT_CONNECTING:
	case PA_CONTEXT_AUTHORIZING:
	case PA_CONTEXT_SETTING_NAME:
		break;
	case PA_CONTEXT_FAILED:
	case PA_CONTEXT_TERMINATED:
		*running = -1;
		break;
	case PA_CONTEXT_READY:
		*running = 1;
		break;
	}

	d_print("pulse: context state has changed to %s\n", __pa_context_state_str(s));

	pa_threaded_mainloop_signal(pa_ml, 0);
}

static const char *__pa_stream_state_str(pa_stream_state_t s)
{
	switch (s) {
	case PA_STREAM_CREATING:
		return "PA_STREAM_CREATING";
	case PA_STREAM_FAILED:
		return "PA_STREAM_FAILED";
	case PA_STREAM_READY:
		return "PA_STREAM_READY";
	case PA_STREAM_TERMINATED:
		return "PA_STREAM_TERMINATED";
	case PA_STREAM_UNCONNECTED:
		return "PA_STREAM_UNCONNECTED";
	}

	return "unknown";
}

static void __pa_stream_running_cb(pa_stream *s, void *data)
{
	pa_stream_state_t	 ss;
	int			*running;

	running = (int*)data;

	ss = pa_stream_get_state(s);
	switch (ss) {
	case PA_STREAM_UNCONNECTED:
	case PA_STREAM_CREATING:
		break;
	case PA_STREAM_FAILED:
	case PA_STREAM_TERMINATED:
		*running = -1;
		break;
	case PA_STREAM_READY:
		*running = 1;
		break;
	}

	d_print("pulse: stream state has changed to %s\n", __pa_stream_state_str(ss));

	pa_threaded_mainloop_signal(pa_ml, 0);
}

static void __pa_sink_input_info_cb(pa_context *c,
				    const pa_sink_input_info *i,
				    int eol,
				    void *data)
{
	if (i)
		memcpy(&pa_vol, &i->volume, sizeof(pa_vol));

	pa_threaded_mainloop_signal(pa_ml, 0);
}

static pa_sample_format_t __pa_sample_format(sample_format_t sf)
{
	const int signed_	= sf_get_signed(sf);
	const int big_endian	= sf_get_bigendian(sf);
	const int sample_size	= sf_get_sample_size(sf) * 8;

	if (!signed_ && sample_size == 8)
		return PA_SAMPLE_U8;

	if (signed_) {
		switch (sample_size) {
		case 16:
			return big_endian ? PA_SAMPLE_S16BE : PA_SAMPLE_S16LE;
		case 24:
			return big_endian ? PA_SAMPLE_S24BE : PA_SAMPLE_S24LE;
		case 32:
			return big_endian ? PA_SAMPLE_S32BE : PA_SAMPLE_S32LE;
		}
	}

	return PA_SAMPLE_INVALID;
}

static uint32_t __pa_sample_rate(sample_format_t sf)
{
	return sf_get_rate(sf);
}

static uint8_t __pa_nchannels(sample_format_t sf)
{
	return sf_get_channels(sf);
}

static int __pa_stream_cork(int pause_)
{
	pa_operation *o;

	pa_threaded_mainloop_lock(pa_ml);

	o = pa_stream_cork(pa_s, pause_, NULL, NULL);
	if (!o) {
		pa_threaded_mainloop_unlock(pa_ml);
		return -OP_ERROR_INTERNAL;
	}

	pa_operation_unref(o);

	pa_threaded_mainloop_unlock(pa_ml);

	return OP_ERROR_SUCCESS;
}

static void __pa_shutdown(void)
{
	pa_threaded_mainloop_lock(pa_ml);

	if (pa_s) {
		pa_stream_disconnect(pa_s);
		pa_s = NULL;
	}

	if (pa_ctx) {
		pa_context_disconnect(pa_ctx);
		pa_context_unref(pa_ctx);
		pa_ctx = NULL;
	}

	pa_threaded_mainloop_unlock(pa_ml);

	if (pa_ml) {
		pa_threaded_mainloop_stop(pa_ml);
		pa_threaded_mainloop_free(pa_ml);
		pa_ml = NULL;
	}

	_pa_context_running	= 0;
	_pa_stream_running	= 0;
}

static int __pa_init(void)
{
	pa_channel_map	*cm;
	pa_mainloop_api	*api;
	pa_proplist	*pl;
	int		 rc;

	cm = pa_channel_map_init_stereo(&pa_cmap);
	if (!cm)
		return -OP_ERROR_INTERNAL;

	pa_cvolume_reset(&pa_vol, 2);

	pl = __create_app_proplist();

	pa_ml = pa_threaded_mainloop_new();
	BUG_ON(!pa_ml);

	api = pa_threaded_mainloop_get_api(pa_ml);
	BUG_ON(!api);

	rc = pa_threaded_mainloop_start(pa_ml);
	if (rc < 0) {
		pa_threaded_mainloop_free(pa_ml);
		ret_pa_error(rc);
	}

	pa_threaded_mainloop_lock(pa_ml);

	pa_ctx = pa_context_new_with_proplist(api, "C* Music Player", pl);
	BUG_ON(!pa_ctx);
	pa_proplist_free(pl);

	pa_context_set_state_callback(pa_ctx, __pa_context_running_cb, &_pa_context_running);

	_pa_context_running = 0;
	rc = pa_context_connect(pa_ctx, NULL, PA_CONTEXT_NOFLAGS, NULL);
	if (rc < 0)
		goto out_fail;

	while (_pa_context_running == 0)
		pa_threaded_mainloop_wait(pa_ml);

	if (_pa_context_running < 0)
		goto out_fail;

	pa_threaded_mainloop_unlock(pa_ml);

	return OP_ERROR_SUCCESS;

out_fail:
	pa_threaded_mainloop_unlock(pa_ml);

	__pa_shutdown();

	ret_pa_error(rc);
}

static int op_pulse_init(void)
{
	/*
	 * op_pulse_mixer_init should already be called
	 */
	return OP_ERROR_SUCCESS;
}

static int op_pulse_exit(void)
{
	/*
	 * op_pulse_mixer_exit should take care of cleaning up
	 */
	return OP_ERROR_SUCCESS;
}

static int op_pulse_open(sample_format_t sf)
{
	pa_proplist	*pl;
	int		 rc;

	/*
	 * Configure PulseAudio for highest possible latency
	 */
	const pa_buffer_attr ba = {
		.maxlength	= -1,
		.tlength	= -1,
		.prebuf		= -1,
		.minreq		= -1,
		.fragsize	= -1
	};

	const pa_sample_spec ss = {
		.format		= __pa_sample_format(sf),
		.rate		= __pa_sample_rate(sf),
		.channels	= __pa_nchannels(sf)
	};

	if (!pa_sample_spec_valid(&ss))
		return -OP_ERROR_SAMPLE_FORMAT;

	pl = __create_stream_proplist();

	pa_threaded_mainloop_lock(pa_ml);

	pa_s = pa_stream_new_with_proplist(pa_ctx, "playback", &ss, &pa_cmap, pl);
	pa_proplist_free(pl);
	if (!pa_s) {
		pa_threaded_mainloop_unlock(pa_ml);
		return -OP_ERROR_INTERNAL;
	}

	pa_stream_set_state_callback(pa_s, __pa_stream_running_cb, &_pa_stream_running);

	_pa_stream_running = 0;
	rc = pa_stream_connect_playback(pa_s,
					NULL,
					&ba,
					PA_STREAM_NOFLAGS,
					&pa_vol,
					NULL);
	if (rc < 0)
		goto out_fail;

	while (_pa_stream_running == 0)
		pa_threaded_mainloop_wait(pa_ml);

	if (_pa_stream_running < 0)
		goto out_fail;

	pa_threaded_mainloop_unlock(pa_ml);

	return OP_ERROR_SUCCESS;

out_fail:
	pa_stream_unref(pa_s);

	pa_threaded_mainloop_unlock(pa_ml);

	ret_pa_error(rc);
}

static int op_pulse_close(void)
{
	pa_threaded_mainloop_lock(pa_ml);

	_pa_stream_running = 0;
	pa_stream_disconnect(pa_s);
	while (_pa_stream_running == 0)
		pa_threaded_mainloop_wait(pa_ml);

	pa_threaded_mainloop_unlock(pa_ml);

	return OP_ERROR_SUCCESS;
}

static int op_pulse_drop(void)
{
	pa_operation *o;

	pa_threaded_mainloop_lock(pa_ml);

	o = pa_stream_flush(pa_s, NULL, NULL);
	if (!o) {
		pa_threaded_mainloop_unlock(pa_ml);
		return -OP_ERROR_INTERNAL;
	}

	pa_operation_unref(o);

	pa_threaded_mainloop_unlock(pa_ml);

	return OP_ERROR_SUCCESS;
}

static int op_pulse_write(const char *buf, int count)
{
	int rc;

	pa_threaded_mainloop_lock(pa_ml);
	rc = pa_stream_write(pa_s, buf, count, NULL, 0, PA_SEEK_RELATIVE);
	pa_threaded_mainloop_unlock(pa_ml);

	if (rc < 0)
		ret_pa_error(rc);
	else
		return count;
}

static int op_pulse_buffer_space(void)
{
	int s;

	pa_threaded_mainloop_lock(pa_ml);
	s = (int)pa_stream_writable_size(pa_s);
	pa_threaded_mainloop_unlock(pa_ml);

	return s;
}

static int op_pulse_pause(void)
{
	return __pa_stream_cork(1);
}

static int op_pulse_unpause(void)
{
	return __pa_stream_cork(0);
}

static int op_pulse_set_option(int key, const char *val)
{
	return -OP_ERROR_NOT_OPTION;
}

static int op_pulse_get_option(int key, char **val)
{
	return -OP_ERROR_NOT_OPTION;
}

static int op_pulse_mixer_init(void)
{
	return __pa_init();
}

static int op_pulse_mixer_exit(void)
{
	__pa_shutdown();

	return OP_ERROR_SUCCESS;
}

static int op_pulse_mixer_open(int *volume_max)
{
	*volume_max = PA_VOLUME_NORM;

	return OP_ERROR_SUCCESS;
}

static int op_pulse_mixer_close(void)
{
	return OP_ERROR_SUCCESS;
}

static int op_pulse_mixer_get_fds(int *fds)
{
	return -OP_ERROR_NOT_SUPPORTED;
}

static int op_pulse_mixer_set_volume(int l, int r)
{
	pa_operation *o;

	pa_threaded_mainloop_lock(pa_ml);

	pa_cvolume_set_position(&pa_vol,
				&pa_cmap,
				PA_CHANNEL_POSITION_FRONT_LEFT,
				(pa_volume_t)l);

	pa_cvolume_set_position(&pa_vol,
				&pa_cmap,
				PA_CHANNEL_POSITION_FRONT_RIGHT,
				(pa_volume_t)r);

	if (!pa_s) {
		pa_threaded_mainloop_unlock(pa_ml);

		return OP_ERROR_SUCCESS;
	} else {
		o = pa_context_set_sink_input_volume(pa_ctx,
						     pa_stream_get_index(pa_s),
						     &pa_vol,
						     NULL,
						     NULL);
		if (!o) {
			pa_threaded_mainloop_unlock(pa_ml);
			return -OP_ERROR_INTERNAL;
		}

		pa_operation_unref(o);

		pa_threaded_mainloop_unlock(pa_ml);

		return OP_ERROR_SUCCESS;
	}
}

static int op_pulse_mixer_get_volume(int *l, int *r)
{
	pa_operation *o;

	if (!pa_s)
		return __pa_ret_volume(l, r);
	else {
		pa_threaded_mainloop_lock(pa_ml);

		o = pa_context_get_sink_input_info(pa_ctx,
						   pa_stream_get_index(pa_s),
						   __pa_sink_input_info_cb,
						   NULL);
		if (!o) {
			pa_threaded_mainloop_unlock(pa_ml);
			return -OP_ERROR_INTERNAL;
		}

		while (pa_operation_get_state(o) == PA_OPERATION_RUNNING)
			pa_threaded_mainloop_wait(pa_ml);

		pa_operation_unref(o);

		pa_threaded_mainloop_unlock(pa_ml);

		return __pa_ret_volume(l, r);
	}
}

static int op_pulse_mixer_set_option(int key, const char *val)
{
	return -OP_ERROR_NOT_OPTION;
}

static int op_pulse_mixer_get_option(int key, char **val)
{
	return -OP_ERROR_NOT_OPTION;
}

const struct output_plugin_ops op_pcm_ops = {
	.init		= op_pulse_init,
	.exit		= op_pulse_exit,
	.open		= op_pulse_open,
	.close		= op_pulse_close,
	.drop		= op_pulse_drop,
	.write		= op_pulse_write,
	.buffer_space	= op_pulse_buffer_space,
	.pause		= op_pulse_pause,
	.unpause	= op_pulse_unpause,
	.set_option	= op_pulse_set_option,
	.get_option	= op_pulse_get_option
};

const struct mixer_plugin_ops op_mixer_ops = {
	.init		= op_pulse_mixer_init,
	.exit		= op_pulse_mixer_exit,
	.open		= op_pulse_mixer_open,
	.close		= op_pulse_mixer_close,
	.get_fds	= op_pulse_mixer_get_fds,
	.set_volume	= op_pulse_mixer_set_volume,
	.get_volume	= op_pulse_mixer_get_volume,
	.set_option	= op_pulse_mixer_set_option,
	.get_option	= op_pulse_mixer_get_option
};

const char * const op_pcm_options[] = {
	NULL
};

const char * const op_mixer_options[] = {
	NULL
};

const int op_priority = -1;

