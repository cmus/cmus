/*
 * Copyright (C) 2008-2013 Various Authors
 * Copyright (C) 2009 Gregory Petrosyan
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

#include <string.h>

#include <pulse/pulseaudio.h>

#include "op.h"
#include "mixer.h"
#include "debug.h"
#include "utils.h"
#include "xmalloc.h"

static pa_threaded_mainloop	*pa_ml;
static pa_context		*pa_ctx;
static pa_stream		*pa_s;
static pa_channel_map		 pa_cmap;
static pa_cvolume		 pa_vol;
static pa_sample_spec		 pa_ss;

/* configuration */
static int pa_restore_volume = 1;

#define ret_pa_error(err)						\
	do {								\
		d_print("PulseAudio error: %s\n", pa_strerror(err));	\
		return -OP_ERROR_INTERNAL;				\
	} while (0)

#define ret_pa_last_error() ret_pa_error(pa_context_errno(pa_ctx))

static pa_proplist *__create_app_proplist(void)
{
	pa_proplist	*pl;
	int		 rc;

	pl = pa_proplist_new();
	BUG_ON(!pl);

	rc = pa_proplist_sets(pl, PA_PROP_APPLICATION_NAME, "C* Music Player");
	BUG_ON(rc);

	rc = pa_proplist_sets(pl, PA_PROP_APPLICATION_VERSION, VERSION);
	BUG_ON(rc);

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

	rc = pa_proplist_sets(pl, PA_PROP_MEDIA_ICON_NAME, "audio-x-generic");
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
	const pa_context_state_t cs = pa_context_get_state(c);

	d_print("pulse: context state has changed to %s\n", __pa_context_state_str(cs));

	switch (cs) {
	case PA_CONTEXT_READY:
	case PA_CONTEXT_FAILED:
	case PA_CONTEXT_TERMINATED:
		pa_threaded_mainloop_signal(pa_ml, 0);
	default:
		return;
	}
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
	const pa_stream_state_t ss = pa_stream_get_state(s);

	d_print("pulse: stream state has changed to %s\n", __pa_stream_state_str(ss));

	switch (ss) {
	case PA_STREAM_READY:
	case PA_STREAM_FAILED:
	case PA_STREAM_TERMINATED:
		pa_threaded_mainloop_signal(pa_ml, 0);
	default:
		return;
	}
}

static void __pa_sink_input_info_cb(pa_context *c,
				    const pa_sink_input_info *i,
				    int eol,
				    void *data)
{
	if (i) {
		memcpy(&pa_vol, &i->volume, sizeof(pa_vol));
		pa_threaded_mainloop_signal(pa_ml, 0);
	}
}

static void __pa_stream_success_cb(pa_stream *s, int success, void *data)
{
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

static int __pa_wait_unlock(pa_operation *o)
{
	pa_operation_state_t state;

	if (!o) {
		pa_threaded_mainloop_unlock(pa_ml);
		ret_pa_last_error();
	}

	while ((state = pa_operation_get_state(o)) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(pa_ml);

	pa_operation_unref(o);
	pa_threaded_mainloop_unlock(pa_ml);

	if (state == PA_OPERATION_DONE)
		return OP_ERROR_SUCCESS;
	else
		ret_pa_last_error();
}

static int __pa_nowait_unlock(pa_operation *o)
{
	if (!o) {
		pa_threaded_mainloop_unlock(pa_ml);
		ret_pa_last_error();
	}

	pa_operation_unref(o);
	pa_threaded_mainloop_unlock(pa_ml);

	return OP_ERROR_SUCCESS;
}

static int __pa_stream_cork(int pause_)
{
	pa_threaded_mainloop_lock(pa_ml);

	return __pa_wait_unlock(pa_stream_cork(pa_s, pause_, __pa_stream_success_cb, NULL));
}

static int __pa_stream_drain(void)
{
	pa_threaded_mainloop_lock(pa_ml);

	return __pa_wait_unlock(pa_stream_drain(pa_s, __pa_stream_success_cb, NULL));
}

static int __pa_create_context(void)
{
	pa_mainloop_api	*api;
	pa_proplist	*pl;
	int		 rc;

	pl = __create_app_proplist();

	api = pa_threaded_mainloop_get_api(pa_ml);
	BUG_ON(!api);

	pa_threaded_mainloop_lock(pa_ml);

	pa_ctx = pa_context_new_with_proplist(api, "C* Music Player", pl);
	BUG_ON(!pa_ctx);
	pa_proplist_free(pl);

	pa_context_set_state_callback(pa_ctx, __pa_context_running_cb, NULL);

	rc = pa_context_connect(pa_ctx, NULL, PA_CONTEXT_NOFAIL, NULL);
	if (rc)
		goto out_fail;

	for (;;) {
		pa_context_state_t state;
		state = pa_context_get_state(pa_ctx);
		if (state == PA_CONTEXT_READY)
			break;
		if (!PA_CONTEXT_IS_GOOD(state))
			goto out_fail_connected;
		pa_threaded_mainloop_wait(pa_ml);
	}

	pa_threaded_mainloop_unlock(pa_ml);

	return OP_ERROR_SUCCESS;

out_fail_connected:
	pa_context_disconnect(pa_ctx);

out_fail:
	pa_context_unref(pa_ctx);
	pa_ctx = NULL;

	pa_threaded_mainloop_unlock(pa_ml);

	ret_pa_last_error();
}

static int op_pulse_init(void)
{
	int rc;

	pa_ml = pa_threaded_mainloop_new();
	BUG_ON(!pa_ml);

	rc = pa_threaded_mainloop_start(pa_ml);
	if (rc) {
		pa_threaded_mainloop_free(pa_ml);
		ret_pa_error(rc);
	}

	return OP_ERROR_SUCCESS;
}

static int op_pulse_exit(void)
{
	if (pa_ml) {
		pa_threaded_mainloop_stop(pa_ml);
		pa_threaded_mainloop_free(pa_ml);
		pa_ml = NULL;
	}

	return OP_ERROR_SUCCESS;
}

#define RET_IF(x) case CHANNEL_POSITION_ ## x: return PA_CHANNEL_POSITION_ ## x

static pa_channel_position_t pulse_channel_position(channel_position_t p)
{
	switch (p) {
	RET_IF(MONO);
	RET_IF(FRONT_LEFT); RET_IF(FRONT_RIGHT); RET_IF(FRONT_CENTER);
	RET_IF(REAR_CENTER); RET_IF(REAR_LEFT); RET_IF(REAR_RIGHT);
	RET_IF(LFE);
	RET_IF(FRONT_LEFT_OF_CENTER); RET_IF(FRONT_RIGHT_OF_CENTER);
	RET_IF(SIDE_LEFT); RET_IF(SIDE_RIGHT);
	RET_IF(TOP_CENTER);
	RET_IF(TOP_FRONT_LEFT); RET_IF(TOP_FRONT_RIGHT); RET_IF(TOP_FRONT_CENTER);
	RET_IF(TOP_REAR_LEFT); RET_IF(TOP_REAR_RIGHT); RET_IF(TOP_REAR_CENTER);
	default:
		return PA_CHANNEL_POSITION_INVALID;
	}
}

static int op_pulse_open(sample_format_t sf, const channel_position_t *channel_map)
{
	pa_proplist	*pl;
	int		 rc, i;

	const pa_sample_spec ss = {
		.format		= __pa_sample_format(sf),
		.rate		= sf_get_rate(sf),
		.channels	= sf_get_channels(sf)
	};

	if (!pa_sample_spec_valid(&ss))
		return -OP_ERROR_SAMPLE_FORMAT;

	pa_ss = ss;
	if (channel_map && channel_map_valid(channel_map)) {
		pa_channel_map_init(&pa_cmap);
		pa_cmap.channels = ss.channels;
		for (i = 0; i < pa_cmap.channels; i++)
			pa_cmap.map[i] = pulse_channel_position(channel_map[i]);
	} else
		pa_channel_map_init_auto(&pa_cmap, ss.channels, PA_CHANNEL_MAP_ALSA);

	rc = __pa_create_context();
	if (rc)
		return rc;

	pl = __create_stream_proplist();

	pa_threaded_mainloop_lock(pa_ml);

	pa_s = pa_stream_new_with_proplist(pa_ctx, "playback", &ss, &pa_cmap, pl);
	pa_proplist_free(pl);
	if (!pa_s) {
		pa_threaded_mainloop_unlock(pa_ml);
		ret_pa_last_error();
	}

	pa_stream_set_state_callback(pa_s, __pa_stream_running_cb, NULL);

	rc = pa_stream_connect_playback(pa_s,
					NULL,
					NULL,
					PA_STREAM_NOFLAGS,
					pa_restore_volume ? NULL : &pa_vol,
					NULL);
	if (rc)
		goto out_fail;

	pa_threaded_mainloop_wait(pa_ml);

	if (pa_stream_get_state(pa_s) != PA_STREAM_READY)
		goto out_fail;

	pa_threaded_mainloop_unlock(pa_ml);

	return OP_ERROR_SUCCESS;

out_fail:
	pa_stream_unref(pa_s);

	pa_threaded_mainloop_unlock(pa_ml);

	ret_pa_last_error();
}

static int op_pulse_close(void)
{
	/*
	 * If this __pa_stream_drain() will be moved below following
	 * pa_threaded_mainloop_lock(), PulseAudio 0.9.19 will hang.
	 */
	if (pa_s)
		__pa_stream_drain();

	pa_threaded_mainloop_lock(pa_ml);

	if (pa_s) {
		pa_stream_disconnect(pa_s);
		pa_stream_unref(pa_s);
		pa_s = NULL;
	}

	if (pa_ctx) {
		pa_context_disconnect(pa_ctx);
		pa_context_unref(pa_ctx);
		pa_ctx = NULL;
	}

	pa_threaded_mainloop_unlock(pa_ml);

	return OP_ERROR_SUCCESS;
}

static int op_pulse_drop(void)
{
	pa_threaded_mainloop_lock(pa_ml);

	return __pa_wait_unlock(pa_stream_flush(pa_s, __pa_stream_success_cb, NULL));
}

static int op_pulse_write(const char *buf, int count)
{
	int rc;

	pa_threaded_mainloop_lock(pa_ml);
	rc = pa_stream_write(pa_s, buf, count, NULL, 0, PA_SEEK_RELATIVE);
	pa_threaded_mainloop_unlock(pa_ml);

	if (rc)
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
	if (!pa_channel_map_init_stereo(&pa_cmap))
		ret_pa_last_error();

	pa_cvolume_reset(&pa_vol, 2);

	return OP_ERROR_SUCCESS;
}

static int op_pulse_mixer_exit(void)
{
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
	if (!pa_s && pa_restore_volume)
		return -OP_ERROR_NOT_OPEN;

	pa_cvolume_set(&pa_vol, pa_ss.channels, (pa_volume_t) ((l + r) / 2));
	pa_cvolume_set_position(&pa_vol,
				&pa_cmap,
				PA_CHANNEL_POSITION_FRONT_LEFT,
				(pa_volume_t)l);

	pa_cvolume_set_position(&pa_vol,
				&pa_cmap,
				PA_CHANNEL_POSITION_FRONT_RIGHT,
				(pa_volume_t)r);

	if (!pa_s) {
		return OP_ERROR_SUCCESS;
	} else {
		pa_threaded_mainloop_lock(pa_ml);

		return __pa_nowait_unlock(pa_context_set_sink_input_volume(pa_ctx,
								           pa_stream_get_index(pa_s),
								           &pa_vol,
								           NULL,
								           NULL));
	}
}

static int op_pulse_mixer_get_volume(int *l, int *r)
{
	int rc = OP_ERROR_SUCCESS;

	if (!pa_s && pa_restore_volume)
		return -OP_ERROR_NOT_OPEN;

	if (pa_s) {
		pa_threaded_mainloop_lock(pa_ml);

		rc = __pa_wait_unlock(pa_context_get_sink_input_info(pa_ctx,
								     pa_stream_get_index(pa_s),
								     __pa_sink_input_info_cb,
								     NULL));
	}

	*l = pa_cvolume_get_position(&pa_vol, &pa_cmap, PA_CHANNEL_POSITION_FRONT_LEFT);
	*r = pa_cvolume_get_position(&pa_vol, &pa_cmap, PA_CHANNEL_POSITION_FRONT_RIGHT);

	return rc;
}

static int op_pulse_mixer_set_option(int key, const char *val)
{
	switch (key) {
	case 0:
		pa_restore_volume = is_freeform_true(val);
		break;
	default:
		return -OP_ERROR_NOT_OPTION;
	}
	return 0;
}

static int op_pulse_mixer_get_option(int key, char **val)
{
	switch (key) {
	case 0:
		*val = xstrdup(pa_restore_volume ? "1" : "0");
		break;
	default:
		return -OP_ERROR_NOT_OPTION;
	}
	return 0;
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
	"restore_volume",
	NULL
};

const int op_priority = -2;

