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

#include "../op.h"
#include "../mixer.h"
#include "../debug.h"
#include "../utils.h"
#include "../xmalloc.h"

static pa_threaded_mainloop *mainloop;
static pa_context *context;
static pa_stream *stream;
static pa_channel_map channel_map;
static pa_cvolume volume;
static pa_sample_spec sample_spec;

static int mixer_notify_in;
static int mixer_notify_out;

static int mixer_notify_output_in;
static int mixer_notify_output_out;
static long last_output_idx;

/* configuration */
static int pa_restore_volume = 1;

#define RET_PA_ERROR(err)						\
	do {								\
		d_print("PulseAudio error: %s\n", pa_strerror(err));	\
		return -OP_ERROR_INTERNAL;				\
	} while (0)

#define RET_PA_LAST_ERROR() RET_PA_ERROR(pa_context_errno(context))

static int pulse_wait_and_unlock(pa_operation *op)
{
	pa_operation_state_t state;

	if (!op) {
		pa_threaded_mainloop_unlock(mainloop);
		RET_PA_LAST_ERROR();
	}

	while ((state = pa_operation_get_state(op)) == PA_OPERATION_RUNNING)
		pa_threaded_mainloop_wait(mainloop);

	pa_operation_unref(op);
	pa_threaded_mainloop_unlock(mainloop);

	if (state == PA_OPERATION_DONE)
		return OP_ERROR_SUCCESS;
	else
		RET_PA_LAST_ERROR();
}

static int pulse_nowait_and_unlock(pa_operation *op)
{
	if (!op) {
		pa_threaded_mainloop_unlock(mainloop);
		RET_PA_LAST_ERROR();
	}

	pa_operation_unref(op);
	pa_threaded_mainloop_unlock(mainloop);

	return OP_ERROR_SUCCESS;
}

static pa_sample_format_t convert_sample_format(sample_format_t sf)
{
	const int _signed = sf_get_signed(sf);
	const int big_endian = sf_get_bigendian(sf);
	const int sample_size = sf_get_sample_size(sf) * 8;

	if (!_signed && sample_size == 8)
		return PA_SAMPLE_U8;

	if (_signed) {
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

#define RET_IF(x) case CHANNEL_POSITION_ ## x: return PA_CHANNEL_POSITION_ ## x

static pa_channel_position_t convert_channel_position(channel_position_t p)
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

static pa_proplist *pulse_create_app_proplist(void)
{
	pa_proplist *pl = pa_proplist_new();
	BUG_ON(!pl);

	int rc = pa_proplist_sets(pl, PA_PROP_APPLICATION_ID, "cmus");
	BUG_ON(rc);

	rc = pa_proplist_sets(pl, PA_PROP_APPLICATION_NAME, "C* Music Player");
	BUG_ON(rc);

	rc = pa_proplist_sets(pl, PA_PROP_APPLICATION_VERSION, VERSION);
	BUG_ON(rc);

	return pl;
}

static pa_proplist *pulse_create_stream_proplist(void)
{
	pa_proplist *pl = pa_proplist_new();
	BUG_ON(!pl);

	int rc = pa_proplist_sets(pl, PA_PROP_MEDIA_ROLE, "music");
	BUG_ON(rc);

	rc = pa_proplist_sets(pl, PA_PROP_MEDIA_ICON_NAME, "audio-x-generic");
	BUG_ON(rc);

	return pl;
}

static const char *pa_context_state_str(pa_context_state_t s)
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

static void pulse_context_state_cb(pa_context *c, void *data)
{
	const pa_context_state_t cs = pa_context_get_state(c);

	d_print("context state has changed to %s\n", pa_context_state_str(cs));

	switch (cs) {
	case PA_CONTEXT_READY:
	case PA_CONTEXT_FAILED:
	case PA_CONTEXT_TERMINATED:
		pa_threaded_mainloop_signal(mainloop, 0);
	default:
		return;
	}
}

static const char *pa_stream_state_str(pa_stream_state_t s)
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

static void pulse_stream_state_cb(pa_stream *s, void *data)
{
	const pa_stream_state_t ss = pa_stream_get_state(s);

	d_print("stream state has changed to %s\n", pa_stream_state_str(ss));

	switch (ss) {
	case PA_STREAM_READY:
	case PA_STREAM_FAILED:
	case PA_STREAM_TERMINATED:
		pa_threaded_mainloop_signal(mainloop, 0);
	default:
		return;
	}
}

static void pulse_sink_input_info_cb(pa_context *c,
		const pa_sink_input_info *i, int eol, void *data)
{
	if (!i)
		return;

	memcpy(&volume, &i->volume, sizeof(volume));
	notify_via_pipe(mixer_notify_in);

	if (last_output_idx != i->sink) {
		if (last_output_idx != -1)
			notify_via_pipe(mixer_notify_output_in);
		last_output_idx = i->sink;
	}
}

static void pulse_context_subscription_cb(pa_context *ctx,
		pa_subscription_event_type_t type, uint32_t idx, void *data)
{
	type &= PA_SUBSCRIPTION_EVENT_TYPE_MASK;
	if (type != PA_SUBSCRIPTION_EVENT_CHANGE)
		return;

	if (stream && idx == pa_stream_get_index(stream)) {
		pa_context_get_sink_input_info(ctx, idx,
				pulse_sink_input_info_cb, NULL);
	}
}

static int pulse_create_context(void)
{
	pa_mainloop_api *api = pa_threaded_mainloop_get_api(mainloop);
	BUG_ON(!api);

	pa_proplist *pl = pulse_create_app_proplist();

	pa_threaded_mainloop_lock(mainloop);

	context = pa_context_new_with_proplist(api, "C* Music Player", pl);
	pa_proplist_free(pl);
	BUG_ON(!context);

	pa_context_set_state_callback(context, pulse_context_state_cb, NULL);

	int rc = pa_context_connect(context, NULL, PA_CONTEXT_NOFLAGS, NULL);
	if (rc)
		goto err_free_ctx;

	for (;;) {
		pa_context_state_t s = pa_context_get_state(context);
		if (s == PA_CONTEXT_READY)
			break;
		if (s == PA_CONTEXT_TERMINATED || s == PA_CONTEXT_FAILED)
			goto err_disconnect_ctx;

		pa_threaded_mainloop_wait(mainloop);
	}

	pa_context_set_subscribe_callback(context,
			pulse_context_subscription_cb, NULL);
	pa_operation *op = pa_context_subscribe(context,
			PA_SUBSCRIPTION_MASK_SINK_INPUT, NULL, NULL);
	if (!op)
		goto err_disconnect_ctx;
	pa_operation_unref(op);

	pa_threaded_mainloop_unlock(mainloop);

	return OP_ERROR_SUCCESS;

err_disconnect_ctx:
	pa_context_disconnect(context);

err_free_ctx:
	pa_context_unref(context);
	context = NULL;

	pa_threaded_mainloop_unlock(mainloop);
	RET_PA_LAST_ERROR();
}

static void pulse_stream_success_cb(pa_stream *s, int success, void *data)
{
	pa_threaded_mainloop_signal(mainloop, 0);
}

static int op_pulse_init(void)
{
	mainloop = pa_threaded_mainloop_new();
	BUG_ON(!mainloop);

	int rc = pa_threaded_mainloop_start(mainloop);
	if (rc) {
		pa_threaded_mainloop_free(mainloop);
		RET_PA_ERROR(rc);
	}

	return OP_ERROR_SUCCESS;
}

static int op_pulse_exit(void)
{
	if (mainloop) {
		pa_threaded_mainloop_stop(mainloop);
		pa_threaded_mainloop_free(mainloop);
		mainloop = NULL;
	}

	return OP_ERROR_SUCCESS;
}

static int op_pulse_open(sample_format_t sf, const channel_position_t *cmap)
{
	pa_proplist *pl;
	int rc, i;

	const pa_sample_spec ss = {
		.format = convert_sample_format(sf),
		.rate = sf_get_rate(sf),
		.channels = sf_get_channels(sf)
	};

	if (!pa_sample_spec_valid(&ss))
		return -OP_ERROR_SAMPLE_FORMAT;

	sample_spec = ss;
	if (cmap && channel_map_valid(cmap)) {
		pa_channel_map_init(&channel_map);
		channel_map.channels = ss.channels;
		for (i = 0; i < channel_map.channels; i++)
			channel_map.map[i] = convert_channel_position(cmap[i]);
	} else {
		pa_channel_map_init_auto(&channel_map, ss.channels,
				PA_CHANNEL_MAP_ALSA);
	}

	last_output_idx = -1;
	rc = pulse_create_context();
	if (rc)
		return rc;

	pl = pulse_create_stream_proplist();

	pa_threaded_mainloop_lock(mainloop);

	stream = pa_stream_new_with_proplist(context, "playback",
			&ss, &channel_map, pl);
	pa_proplist_free(pl);
	if (!stream)
		goto err;

	pa_stream_set_state_callback(stream, pulse_stream_state_cb, NULL);

	rc = pa_stream_connect_playback(stream, NULL, NULL, PA_STREAM_NOFLAGS,
			pa_restore_volume ? NULL : &volume, NULL);
	if (rc)
		goto err_free_stream;

	for (;;) {
		pa_stream_state_t s = pa_stream_get_state(stream);
		if (s == PA_STREAM_READY)
			break;
		if (s == PA_STREAM_FAILED || s == PA_STREAM_TERMINATED)
			goto err_free_stream;

		pa_threaded_mainloop_wait(mainloop);
	}

	pa_context_get_sink_input_info(context, pa_stream_get_index(stream),
			pulse_sink_input_info_cb, NULL);

	pa_threaded_mainloop_unlock(mainloop);

	return OP_ERROR_SUCCESS;

err_free_stream:
	pa_stream_unref(stream);
err:
	pa_threaded_mainloop_unlock(mainloop);
	RET_PA_LAST_ERROR();
}

static int op_pulse_close(void)
{
	pa_threaded_mainloop_lock(mainloop);

	if (stream) {
		pa_stream_disconnect(stream);
		pa_stream_unref(stream);
		stream = NULL;
	}

	if (context) {
		pa_context_disconnect(context);
		pa_context_unref(context);
		context = NULL;
	}

	pa_threaded_mainloop_unlock(mainloop);

	return OP_ERROR_SUCCESS;
}

static int op_pulse_drop(void)
{
	pa_threaded_mainloop_lock(mainloop);

	pa_operation *op = pa_stream_flush(stream, pulse_stream_success_cb, NULL);
	return pulse_wait_and_unlock(op);
}

static int op_pulse_write(const char *buf, int count)
{
	pa_threaded_mainloop_lock(mainloop);
	int rc = pa_stream_write(stream, buf, count, NULL, 0, PA_SEEK_RELATIVE);
	pa_threaded_mainloop_unlock(mainloop);

	if (rc)
		RET_PA_ERROR(rc);
	else
		return count;
}

static int op_pulse_buffer_space(void)
{
	pa_threaded_mainloop_lock(mainloop);
	size_t s = pa_stream_writable_size(stream);
	pa_threaded_mainloop_unlock(mainloop);

	if (s == (size_t)-1)
		RET_PA_LAST_ERROR();
	else
		return s;
}

static int pulse_stream_cork(int pause)
{
	pa_threaded_mainloop_lock(mainloop);

	pa_operation *op = pa_stream_cork(stream, pause,
			pulse_stream_success_cb, NULL);
	return pulse_wait_and_unlock(op);
}

static int op_pulse_pause(void)
{
	return pulse_stream_cork(1);
}

static int op_pulse_unpause(void)
{
	return pulse_stream_cork(0);
}

static int op_pulse_mixer_init(void)
{
	if (!pa_channel_map_init_stereo(&channel_map))
		RET_PA_LAST_ERROR();

	pa_cvolume_reset(&volume, 2);

	init_pipes(&mixer_notify_out, &mixer_notify_in);
	init_pipes(&mixer_notify_output_out, &mixer_notify_output_in);

	return OP_ERROR_SUCCESS;
}

static int op_pulse_mixer_exit(void)
{
	close(mixer_notify_out);
	close(mixer_notify_in);

	close(mixer_notify_output_out);
	close(mixer_notify_output_in);

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

static int op_pulse_mixer_get_fds(int what, int *fds)
{
	switch (what) {
	case MIXER_FDS_VOLUME:
		fds[0] = mixer_notify_out;
		return 1;
	case MIXER_FDS_OUTPUT:
		fds[0] = mixer_notify_output_out;
		return 1;
	default:
		return 0;
	}
}

static int op_pulse_mixer_set_volume(int l, int r)
{
	if (!stream && pa_restore_volume)
		return -OP_ERROR_NOT_OPEN;

	pa_cvolume_set(&volume, sample_spec.channels, (pa_volume_t)((l + r) / 2));
	pa_cvolume_set_position(&volume, &channel_map,
			PA_CHANNEL_POSITION_FRONT_LEFT, (pa_volume_t)l);
	pa_cvolume_set_position(&volume, &channel_map,
			PA_CHANNEL_POSITION_FRONT_RIGHT, (pa_volume_t)r);

	if (!stream) {
		return OP_ERROR_SUCCESS;
	} else {
		pa_threaded_mainloop_lock(mainloop);

		pa_operation *op = pa_context_set_sink_input_volume(
				context, pa_stream_get_index(stream),
				&volume, NULL, NULL);
		return pulse_nowait_and_unlock(op);
	}
}

static int op_pulse_mixer_get_volume(int *l, int *r)
{
	clear_pipe(mixer_notify_out, -1);

	if (!stream && pa_restore_volume)
		return -OP_ERROR_NOT_OPEN;

	*l = pa_cvolume_get_position(&volume, &channel_map,
			PA_CHANNEL_POSITION_FRONT_LEFT);
	*r = pa_cvolume_get_position(&volume, &channel_map,
			PA_CHANNEL_POSITION_FRONT_RIGHT);

	return OP_ERROR_SUCCESS;
}

static int op_pulse_set_restore_volume(const char *val)
{
	pa_restore_volume = is_freeform_true(val);
	return 0;
}

static int op_pulse_get_restore_volume(char **val)
{
	*val = xstrdup(pa_restore_volume ? "1" : "0");
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
};

const struct mixer_plugin_ops op_mixer_ops = {
	.init		= op_pulse_mixer_init,
	.exit		= op_pulse_mixer_exit,
	.open		= op_pulse_mixer_open,
	.close		= op_pulse_mixer_close,
	.get_fds.abi_2	= op_pulse_mixer_get_fds,
	.set_volume	= op_pulse_mixer_set_volume,
	.get_volume	= op_pulse_mixer_get_volume,
};

const struct output_plugin_opt op_pcm_options[] = {
	{ NULL },
};

const struct mixer_plugin_opt op_mixer_options[] = {
	OPT(op_pulse, restore_volume),
	{ NULL },
};

const int op_priority = -2;
const unsigned op_abi_version = OP_ABI_VERSION;
