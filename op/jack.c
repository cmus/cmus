/*
 * Copyright 2014 Niko Efthymiou <nefthy-cmus@nefthy.de>
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

/* TODO
 *
 * - configurable maping of channels to ports
 */

#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>

#if HAVE_CONFIG
#include "../config/samplerate.h"
#ifdef HAVE_SAMPLERATE
#include <samplerate.h>
#endif
#endif

#include "../op.h"
#include "../utils.h"
#include "../channelmap.h"
#include "../xmalloc.h"
#include "../debug.h"

#define CHANNELS 2
#define BUFFER_MULTIPLYER (sizeof(jack_default_audio_sample_t) * 16)
#define BUFFER_SIZE_MIN 16384

static char               *server_name;

static jack_client_t      *client;
static jack_port_t        *output_ports[CHANNELS];
static jack_ringbuffer_t  *ringbuffer[CHANNELS];

static jack_nframes_t     jack_sample_rate;

#ifdef HAVE_SAMPLERATE
static SRC_STATE*         src_state[CHANNELS];
static int                src_quality = SRC_SINC_BEST_QUALITY;
static float              resample_ratio = 1.0f;
#endif

static size_t                    buffer_size;
static sample_format_t           sample_format;
static unsigned int              sample_bytes;
static const channel_position_t  *channel_map;
static volatile bool             paused = true;
static volatile bool             drop = false;
static volatile bool             drop_done = true;

/* fail on the next call */
static int fail;

/* function pointer to appropriate read function */
static float (*read_sample) (const char *buff);

static int op_jack_init(void);
static int op_jack_exit(void);
static int op_jack_open(sample_format_t sf, const channel_position_t* cm);
static int op_jack_close(void);
static int op_jack_write(const char *buffer, int count);
static int op_jack_drop(void);
static int op_jack_buffer_space(void);
static int op_jack_pause(void);
static int op_jack_unpause(void);

/* read functions for various sample formats */

static jack_default_audio_sample_t read_sample_le16(const char *buffer)
{
	int16_t s = (int16_t)read_le16(buffer);
	uint16_t upper_bound = (uint16_t)INT16_MAX + (s <= 0);
	return (jack_default_audio_sample_t)s / (jack_default_audio_sample_t)upper_bound;
}

static jack_default_audio_sample_t read_sample_le24(const char *buffer)
{
	int32_t s = read_le24i(buffer);
	uint32_t upper_bound = 0x7FFFFF + (s < 0);
	return (jack_default_audio_sample_t) s / (jack_default_audio_sample_t)upper_bound;
}

static jack_default_audio_sample_t read_sample_le32(const char *buffer)
{
	int32_t s = (int32_t)read_le32(buffer);
	uint32_t upper_bound = (uint32_t)INT32_MAX + (s <= 0);
	return (jack_default_audio_sample_t)s / (jack_default_audio_sample_t)upper_bound;
}

static jack_default_audio_sample_t read_sample_le16u(const char *buffer)
{
	uint32_t u = read_le16(buffer);
	return (((jack_default_audio_sample_t) u)
		/ ((jack_default_audio_sample_t) UINT16_MAX)) * 2.0 - 2.0;
}

static jack_default_audio_sample_t read_sample_le24u(const char *buffer)
{
	uint32_t u = read_le24(buffer);
	return (((jack_default_audio_sample_t) u)
		/ ((jack_default_audio_sample_t) 0xFFFFFFU)) * 2.0 - 2.0;
}

static jack_default_audio_sample_t read_sample_le32u(const char *buffer)
{
	uint32_t u = read_le32(buffer);
	return (((jack_default_audio_sample_t) u)
		/ ((jack_default_audio_sample_t) UINT32_MAX)) * 2.0 - 2.0;
}

#ifdef HAVE_SAMPLERATE
static void op_jack_reset_src(void) {
	for (int c = 0; c < CHANNELS; c++) {
		src_reset(src_state[c]);
	}
}
#endif

/* jack callbacks */
static void op_jack_error_cb(const char *msg) {
	d_print("jackd error: %s\n", msg);
	fail = 1;
}

static int op_jack_cb(jack_nframes_t frames, void *arg)
{
	size_t bytes_want = frames * sizeof(jack_default_audio_sample_t);

	if (drop) {
		for (int i = 0; i < CHANNELS; i++) {
			jack_ringbuffer_reset(ringbuffer[i]);
		}
		drop = false;
		drop_done = true;
	}

	size_t bytes_min = SIZE_MAX;
	for (int i = 0; i < CHANNELS; i++) {
		size_t bytes_available = jack_ringbuffer_read_space(ringbuffer[i]);
		if (bytes_available < bytes_min) {
			bytes_min = bytes_available;
		}
	}

	/* if there is less than frames available play silence */
	if (paused || bytes_min < bytes_want) {
		for (int i = 0; i < CHANNELS; i++) {
			jack_default_audio_sample_t* jack_buf = jack_port_get_buffer(output_ports[i], frames);
			memset(jack_buf, 0, bytes_want);
		}
		return 0;
	}

	for (int i = 0; i < CHANNELS; i++) {
		jack_default_audio_sample_t* jack_buf = jack_port_get_buffer(output_ports[i], frames);
		size_t bytes_read = jack_ringbuffer_read(ringbuffer[i], (char*) jack_buf, bytes_want);

		if (bytes_read < bytes_want) {
			/* This should not happen[TM] - just in case set fail = 1 */
			d_print("underrun! wanted %zu only got %zu bytes\n", bytes_want, bytes_read);
			fail = 1;
		}
	}

	return 0;
}

/* init or resize buffers if needed */
static int op_jack_buffer_init(jack_nframes_t samples, void *arg)
{
	if (buffer_size > samples * BUFFER_MULTIPLYER) {
		/* we just don't shrink buffers, since this could result
		 * in gaps and they won't get that big anyway
		 */
		return 0;
	}

	buffer_size = samples * BUFFER_MULTIPLYER;
	if (buffer_size < BUFFER_SIZE_MIN) {
		buffer_size = BUFFER_SIZE_MIN;
	}
	d_print("new buffer size %zu\n", buffer_size);

	char *tmp = xmalloc(buffer_size);

	for (int i = 0; i < CHANNELS; i++) {
		jack_ringbuffer_t *new_buffer = jack_ringbuffer_create(buffer_size);

		if (!new_buffer) {
			d_print("ringbuffer alloc failed\n");
			free(tmp);
			fail = 1;
			op_jack_exit();
			return 1;
		}
		if (ringbuffer[i] != NULL) {
			size_t length = jack_ringbuffer_read_space(ringbuffer[i]);

			/* actually this could both read/write less than length.
			 * In that case, which should not happen[TM], there will
			 * be a gap in playback.
			 */
			jack_ringbuffer_read(ringbuffer[i], tmp, length);
			jack_ringbuffer_write(new_buffer, tmp, length);

			jack_ringbuffer_free(ringbuffer[i]);
		}

		ringbuffer[i] = new_buffer;
	}

	free(tmp);
	return 0;
}

static int op_jack_sample_rate_cb(jack_nframes_t samples, void *arg)
{
#ifdef HAVE_SAMPLERATE
	resample_ratio = (float) sf_get_rate(sample_format) / (float) samples;
#else
	if (jack_sample_rate != samples) {
		/* this cannot be handled */
		fail = 1;
		return 1;
	}
#endif
	return 0;
}

static void op_jack_shutdown_cb(void *arg)
{
	d_print("jackd went away");

	/* calling op_jack_exit() will cause a segfault if op_jack_write or
	 * anything else is in the middle of something...
	 * the fail flag is checked by op_jack_write and op_jack_buffer_space
	 *
	 * op_jack_open will try to reinitialize jack
	 */
	fail = 1;
}

/* cmus callbacks */

static int op_jack_init(void)
{
#ifdef HAVE_SAMPLERATE
	for (int i = 0; i < CHANNELS; i++) {
		src_state[i] = src_new(src_quality, 1, NULL);
		if (src_state[i] == NULL) {
			d_print("src_new failed");
			for (i = i - 1; i >= 0; i--) {
				src_delete(src_state[i]);
			}
			return -OP_ERROR_INTERNAL;
		}
	}
#endif
	jack_set_error_function(op_jack_error_cb);

	jack_options_t options = JackNullOption;
	if (fail) {
		/* since jackd failed, it will not be autostarted. Either jackd
		 * was killed intentionally or it died by heartattack.
		 * Until it is restarted, init will happily fail again
		 * and again and again..
		 */
		options |= JackNoStartServer;
	}

	jack_status_t status;
	client = jack_client_open("cmus", options, &status, server_name);
	if (client == NULL) {
		d_print("jack_client_new failed\n");
		return -OP_ERROR_INTERNAL;
	}

	if (status & JackServerStarted) {
		d_print("jackd started\n");
	}

	jack_nframes_t jack_buffer_size = jack_get_buffer_size(client);
	jack_sample_rate = jack_get_sample_rate(client);
	op_jack_buffer_init(jack_buffer_size, NULL);

	jack_set_process_callback(client, op_jack_cb, NULL);
	jack_set_sample_rate_callback(client, op_jack_sample_rate_cb, NULL);
	jack_set_buffer_size_callback(client, op_jack_buffer_init, NULL);
	jack_on_shutdown(client, op_jack_shutdown_cb, NULL);

	for (int i = 0; i < CHANNELS; i++) {
		char port_name[20];
		snprintf(port_name, sizeof(port_name)-1, "output %d", i);

		output_ports[i] = jack_port_register(
			client,
			port_name,
			JACK_DEFAULT_AUDIO_TYPE,
			JackPortIsOutput,
			0
		);
		if (output_ports[i] == NULL) {
			d_print("no jack ports available\n");
			return -OP_ERROR_INTERNAL;
		}
	}

	if (jack_activate(client)) {
		d_print("jack_client_activate failed\n");
		return -OP_ERROR_INTERNAL;
	}

	const char **ports = jack_get_ports(client, NULL, NULL, JackPortIsPhysical | JackPortIsInput);
	if (ports == NULL) {
		d_print("cannot get playback ports\n");
		return -OP_ERROR_INTERNAL;
	}

	for (int i = 0; i < CHANNELS; i++) {
		if (ports[i] == NULL) {
			d_print("could not connect output %d. too few ports.\n", i);
			break;
		}
		if (jack_connect(client, jack_port_name(output_ports[i]), ports[i])) {
			d_print("cannot connect port %s\n", ports[i]);
			jack_free(ports);
			return -OP_ERROR_INTERNAL;
		}
	}

	jack_free(ports);
	fail = 0;

	return OP_ERROR_SUCCESS;
}

static int op_jack_exit(void)
{
	if (client != NULL) {
		jack_deactivate(client);
		for (int i = 0; i < CHANNELS; i++) {
			if (output_ports[i] != NULL) {
				jack_port_unregister(client, output_ports[i]);
			}
		}
		jack_client_close(client);
	}

	for (int i = 0; i < CHANNELS; i++) {
		if (ringbuffer[i] != NULL) {
			jack_ringbuffer_free(ringbuffer[i]);
		}
		ringbuffer[i] = NULL;
	}

	buffer_size = 0;
	client = NULL;

	return OP_ERROR_SUCCESS;
}

static int op_jack_open(sample_format_t sf, const channel_position_t *cm)
{
	sample_format = sf;

	if (fail) {
		/* jack went away so lets see if we can recover */
		if (client != NULL) {
			op_jack_exit();
		}
		if (op_jack_init() != OP_ERROR_SUCCESS) {
			return -OP_ERROR_INTERNAL;
		}
	}

	if (cm == NULL) {
		d_print("no channel_map\n");
		return -OP_ERROR_NOT_SUPPORTED;
	}
	channel_map = cm;

#ifdef HAVE_SAMPLERATE
	op_jack_reset_src();
	resample_ratio = (float) jack_sample_rate / (float) sf_get_rate(sf);
#else
	if (jack_sample_rate != sf_get_rate(sf)) {
		d_print("jack sample rate of %d does not match %d\n", jack_get_sample_rate(client), sf_get_rate(sf));
		return -OP_ERROR_SAMPLE_FORMAT;
	}
#endif

	if (sf_get_channels(sf) < CHANNELS) {
		d_print("%d channels not supported\n", sf_get_channels(sf));
		return -OP_ERROR_SAMPLE_FORMAT;
	}

	int bits = sf_get_bits(sf);

	if (bits == 16) {
		sample_bytes = 2;
		read_sample = sf_get_signed(sf) ? &read_sample_le16 : &read_sample_le16u;
	} else if (bits == 24) {
		sample_bytes = 3;
		read_sample = sf_get_signed(sf) ? &read_sample_le24 : &read_sample_le24u;
	} else if (bits == 32) {
		sample_bytes = 4;
		read_sample = sf_get_signed(sf) ? &read_sample_le32 : &read_sample_le32u;
	} else {
		d_print("%d bits not supported\n", sf_get_bits(sf));
		return -OP_ERROR_SAMPLE_FORMAT;
	}

	paused = false;
	return OP_ERROR_SUCCESS;
}

static int op_jack_close(void)
{
	paused = true;
	return OP_ERROR_SUCCESS;
}

static int op_jack_drop(void)
{
	if (!drop_done) {
		return OP_ERROR_SUCCESS;
	}
	drop_done = false;
	drop = true;
	while (!drop_done) {
		/* wait till op_jack_cb is done with dropping */
		usleep(1000);
	}
	return OP_ERROR_SUCCESS;
}

static int op_jack_write(const char *buffer, int count)
{
	if (fail) {
		op_jack_exit();
		return -OP_ERROR_INTERNAL;
	}

	if (!drop_done) {
		return 0;
	}

	int frame_size = sf_get_frame_size(sample_format);
	int channels = sf_get_channels(sample_format);
	size_t frames = count / frame_size;

	/* since this is the only place where the ringbuffers get
	 * written, available space will only grow, therefore frames_min
	 * is safe.
	 */
	size_t frames_min = SIZE_MAX;
	for (int c = 0; c < CHANNELS; c++) {
		size_t frames_available = jack_ringbuffer_write_space(ringbuffer[c]) / sizeof(jack_default_audio_sample_t);
		if (frames_available < frames_min) {
			frames_min = frames_available;
		}
	}

	if (frames > frames_min) {
		frames = frames_min;
	}

	jack_default_audio_sample_t buf[CHANNELS][buffer_size];

	/* demux and convert to float */
	for (int pos = 0; pos < count; ) {
		int frame = pos / frame_size;
		for (int c = 0; c < channels; c++) {
			int idx = pos + c * sample_bytes;
			/* for now, only 2 channels and mono are supported */
			if (channel_map[c] == CHANNEL_POSITION_LEFT || channel_map[c] == CHANNEL_POSITION_MONO) {
				buf[0][frame] = read_sample(&buffer[idx]);
			} else if (channel_map[c] == CHANNEL_POSITION_RIGHT || channel_map[c] == CHANNEL_POSITION_MONO) {
				buf[1][frame] = read_sample(&buffer[idx]);
			}
		}
		pos += frame_size;
	}

#ifdef HAVE_SAMPLERATE
	if (resample_ratio > 1.01f || resample_ratio < 0.99) {
		jack_default_audio_sample_t converted[buffer_size];
		SRC_DATA src_data;
		for (int c = 0; c < CHANNELS; c++) {
			src_data.data_in = buf[c];
			src_data.data_out = converted;
			src_data.input_frames = frames;
			src_data.output_frames = frames_min;
			src_data.src_ratio = resample_ratio;
			src_data.end_of_input = 0;

			int err = src_process(src_state[c], &src_data);
			if (err) {
				d_print("libsamplerate err %s\n", src_strerror(err));
			}

			int byte_length = src_data.output_frames_gen * sizeof(jack_default_audio_sample_t);
			jack_ringbuffer_write(ringbuffer[c], (const char*) converted, byte_length);
		}
		return src_data.input_frames_used * frame_size;
	} else {
#endif
		int byte_length = frames * sizeof(jack_default_audio_sample_t);
		for (int c = 0; c < CHANNELS; c++) {
			jack_ringbuffer_write(ringbuffer[c], (const char*) buf[c], byte_length);
		}

		return frames * frame_size;
#ifdef HAVE_SAMPLERATE
	}
#endif
}

static int op_jack_buffer_space(void)
{
	if (fail) {
		op_jack_exit();
		return -OP_ERROR_INTERNAL;
	}

	int bytes = jack_ringbuffer_write_space(ringbuffer[0]);
	for (int c = 1; c < CHANNELS; c++) {
		int tmp = jack_ringbuffer_write_space(ringbuffer[0]);
		if (bytes > tmp) {
			bytes = tmp;
		}
	}

	int frames = bytes / sizeof(jack_default_audio_sample_t);
	int frame_size = sf_get_frame_size(sample_format);

#ifdef HAVE_SAMPLERATE
	return (int) ((float) (frames) / resample_ratio) * frame_size;
#else
	return frames * frame_size;
#endif
}

static int op_jack_pause(void)
{
	paused = true;
	return OP_ERROR_SUCCESS;
}

static int op_jack_unpause(void)
{
#ifdef HAVE_SAMPLERATE
	op_jack_reset_src();
#endif
	paused = false;
	return OP_ERROR_SUCCESS;
}

static int op_jack_set_server_name(const char *val)
{
	free(server_name);
	server_name = val[0] != '\0' ? xstrdup(val) : NULL;
	return OP_ERROR_SUCCESS;
}

static int op_jack_get_server_name(char **val)
{
	*val = xstrdup(server_name != NULL ? server_name : "");
	return OP_ERROR_SUCCESS;
}

#ifdef HAVE_SAMPLERATE
static int op_jack_set_resampling_quality(const char *val)
{
	if (strlen(val) != 1) {
		return -OP_ERROR_NOT_SUPPORTED;
	}
	switch (val[0]) {
	default:
	case '2':
		src_quality = SRC_SINC_BEST_QUALITY;
		break;
	case '1':
		src_quality = SRC_SINC_MEDIUM_QUALITY;
		break;
	case '0':
		src_quality = SRC_SINC_FASTEST;
		break;
	}
	return OP_ERROR_SUCCESS;
}

static int op_jack_get_resampling_quality(char **val)
{
	switch (src_quality) {
	case SRC_SINC_BEST_QUALITY:
		*val = xstrdup("2");
		break;
	case SRC_SINC_MEDIUM_QUALITY:
		*val = xstrdup("1");
		break;
	case SRC_SINC_FASTEST:
		*val = xstrdup("0");
		break;
	}
	return OP_ERROR_SUCCESS;
}
#endif

const struct output_plugin_ops op_pcm_ops = {
	.init         = op_jack_init,
	.exit         = op_jack_exit,
	.open         = op_jack_open,
	.close        = op_jack_close,
	.drop         = op_jack_drop,
	.write        = op_jack_write,
	.buffer_space = op_jack_buffer_space,
	.pause        = op_jack_pause,
	.unpause      = op_jack_unpause,
};

const struct output_plugin_opt op_pcm_options[] = {
	OPT(op_jack, server_name),
#ifdef HAVE_SAMPLERATE
	OPT(op_jack, resampling_quality),
#endif
	{ NULL },
};

const int op_priority = 2;
const unsigned op_abi_version = OP_ABI_VERSION;
