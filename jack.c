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
 * - handle server went away
 * - handle server changed sample rate
 * - resample to match server sample rate
 *    libsamplerate
 * - configurable maping of channels to ports
 */
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <string.h>
//#include <stdlib.h>
#include <stdio.h>

#include "op.h"
#include "utils.h"
#include "channelmap.h"
#include "xmalloc.h"
#include "debug.h"


#define CHANNELS 2
#define BUFFER_SIZE 16384 

struct {
	char* client_name;
	char* server_name;
} cfg;

jack_client_t *client;
jack_port_t *output_ports [CHANNELS];
jack_ringbuffer_t *ringbuf [CHANNELS];
sample_format_t sample_format;
/* bytes per sample */
unsigned int sample_stride = 0;
const channel_position_t* channel_map;
volatile int paused = 0;

float (*read_sample) (const char* buff);

static jack_default_audio_sample_t read_sample_le16(const char *buffer) {
	union c {
		uint16_t u;
		int16_t s;
	} c;
	jack_default_audio_sample_t res;

	c.u = read_le16(buffer);
	if (c.s > 0) {
		res = ((jack_default_audio_sample_t) c.s) / ((jack_default_audio_sample_t) INT16_MAX);
	} else {
		res = ((jack_default_audio_sample_t) c.s) / (- (jack_default_audio_sample_t) INT16_MIN);
	}
	return res;
}

static jack_default_audio_sample_t read_sample_le32(const char *buffer) {
	union c {
		uint32_t u;
		int32_t s;
	} c;
	jack_default_audio_sample_t res;

	c.u = read_le16(buffer);
	if (c.s > 0) {
		res = ((jack_default_audio_sample_t) c.s) / ((jack_default_audio_sample_t) INT32_MAX);
	} else {
		res = ((jack_default_audio_sample_t) c.s) / (- (jack_default_audio_sample_t) INT32_MIN);
	}
	return res;
}

static jack_default_audio_sample_t read_sample_le16u(const char *buffer) {
	jack_default_audio_sample_t res;
	uint32_t u;

	u = read_le16(buffer);
	res = (((jack_default_audio_sample_t) u) / ((jack_default_audio_sample_t) UINT16_MAX)) * 2.0 - 2.0;
	return res;
}

static jack_default_audio_sample_t read_sample_le32u(const char *buffer) {
	jack_default_audio_sample_t res;
	uint32_t u;

	u = read_le16(buffer);
	res = (((jack_default_audio_sample_t) u) / ((jack_default_audio_sample_t) UINT32_MAX)) * 2.0 - 2.0;
	return res;
}

static int op_jack_callback(jack_nframes_t frames, void* arg)
{
	int i = 0;
	size_t bytes_read = 0;
	size_t bytes_want = 0;
	size_t fill_length = 0;
	char *fill_offset = NULL;
	jack_default_audio_sample_t *jack_buf;

	for (i = 0; i < CHANNELS; i++) {
		jack_buf = jack_port_get_buffer(output_ports[i], frames);
		if (!paused) {
			bytes_want = frames * sizeof(jack_default_audio_sample_t);
			bytes_read = jack_ringbuffer_read(
				ringbuf[i], 
				(char*) jack_buf, 
				bytes_want
			);

			if (bytes_read < bytes_want) {
				d_print("underrun got %lu bytes, needed %lu\n", 
					bytes_read, 
					bytes_want
				);

				fill_length = bytes_want - bytes_read;
				fill_offset = ((char*) jack_buf) + bytes_read;
				memset(fill_offset, 0, fill_length);
			}
		} else {
			fill_length = sizeof(jack_default_audio_sample_t) * frames;
			memset(jack_buf, 0, fill_length);
		}
	}

	return 0;
}

static int op_jack_init(void)
{
	const char** ports;
	char port_name[20];
	jack_options_t options = JackNullOption;
	jack_status_t status;
	int i;



	client = jack_client_open("cmus", options, &status, cfg.server_name);
	if (client == NULL) {
		d_print("jack_client_new failed status = 0x%2.0x\n", status);
		return -OP_ERROR_INTERNAL;	
	}
	if (status & JackServerStarted) {
		d_print("jack_server started\n");
	}

	for (i = 0; i < CHANNELS; i++) {
		ringbuf[i] = jack_ringbuffer_create(BUFFER_SIZE);
		if (ringbuf[i] == NULL) {
			d_print("failed to create output buffer\n");
			while (--i >= 0) {
				jack_ringbuffer_free(ringbuf[i]);
			}
			return -OP_ERROR_INTERNAL;
		}
	}

	jack_set_process_callback(client, op_jack_callback, NULL);

	for (i = 0; i < CHANNELS; i++) {
		snprintf(port_name, sizeof(port_name)-1, "output %d", i);
		output_ports[i] = jack_port_register (
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

	ports = jack_get_ports(client, NULL, NULL, 
		JackPortIsPhysical|JackPortIsInput
	);
	if (ports == NULL) {
		d_print("cannot get playback ports\n");
		return -OP_ERROR_INTERNAL;
	}

	for (i = 0; i < CHANNELS; i++) {
		if (ports[i] == NULL) {
			d_print("could not connect output %d. to few playback ports.\n", i);
			break;
		}
		if (jack_connect(client, jack_port_name(output_ports[i]), ports[i])) {
			d_print("connot connect port %s\n", ports[i]);
			free(ports);
			return -OP_ERROR_INTERNAL;
		}
	}
	free(ports);

	return OP_ERROR_SUCCESS;
}

static int op_jack_exit(void) 
{
	int i;
	for (i = 0; i < CHANNELS; i++) {
		jack_ringbuffer_free(ringbuf[i]);
		ringbuf[i] = NULL;
	}
	jack_client_close(client);
	return OP_ERROR_SUCCESS;
}

static int op_jack_open(sample_format_t sf, const channel_position_t *cm)
{
	int bits;
	sample_format = sf;

	if (cm == NULL) {
		d_print("no channel_map\n");
		return -OP_ERROR_NOT_SUPPORTED;
	}
	channel_map = cm;

	d_print("big edian = %d\n", sf_get_bigendian(sf));
	d_print("channels = %d\n", sf_get_channels(sf));
	d_print("rate = %d\n", sf_get_rate(sf));
	d_print("signed = %d\n", sf_get_signed(sf));
	d_print("bits = %d\n", sf_get_bits(sf));
	d_print("frame_size = %d\n", sf_get_frame_size(sf));
	
	if (jack_get_sample_rate (client) != sf_get_rate(sf)) {
		d_print(
			"jack sample rate of %d does not match %d\n", 
			jack_get_sample_rate(client),
			sf_get_rate(sf)
		);
		return -OP_ERROR_SAMPLE_FORMAT;
	}

	if (sf_get_channels(sf) < CHANNELS) {
		d_print(
			"%d channels not supported\n", 
			sf_get_channels(sf)
		);
		return -OP_ERROR_SAMPLE_FORMAT;
	}

	bits = sf_get_bits(sf);
	
	if (bits == 16) {
		sample_stride = 2;

		if (sf_get_signed(sf)) {
			read_sample = &read_sample_le16;
		} else {
			read_sample = &read_sample_le16u;
		}
	} else if (bits == 32) {
		sample_stride = 4;
		if (sf_get_signed(sf)) {
			read_sample = &read_sample_le32;
		} else {
			read_sample = &read_sample_le32u;
		}
	} else {
		d_print(
			"%d bits not supported\n", 
			sf_get_bits(sf)
		);
		return -OP_ERROR_SAMPLE_FORMAT;
	}

	paused = 0;
	return OP_ERROR_SUCCESS;
}

static int op_jack_close(void)
{
	paused = 1;
	return OP_ERROR_SUCCESS;
}

static int op_jack_drop(void)
{
	d_print("drop called");
	return -OP_ERROR_NOT_SUPPORTED;
}

static int op_jack_write(const char *buffer, int count)
{
	int idx, c, pos, frame_size, channels, frame;
	jack_default_audio_sample_t buf[CHANNELS][BUFFER_SIZE];

	frame_size = sf_get_frame_size(sample_format);
	channels = sf_get_channels(sample_format);

	
	//demux and convert to float
	pos = 0;
	for (pos = 0; pos < count; ) {
		frame = pos / frame_size;
		for (c = 0; c < channels; c++)	{
			idx = pos + c * sample_stride;
			//for now, only 2 channels and mono are supported
			if (
				channel_map[c] == CHANNEL_POSITION_LEFT
				|| channel_map[c] == CHANNEL_POSITION_MONO
			) {
				buf[0][frame] = read_sample(&buffer[idx]);
			} else if (
				channel_map[c] == CHANNEL_POSITION_RIGHT
				|| channel_map[c] == CHANNEL_POSITION_MONO
			) {
				buf[1][frame] = read_sample(&buffer[idx]);
			}
		}
		pos += frame_size;
	}

	for (c = 0; c < CHANNELS; c++) {
		jack_ringbuffer_write(
			ringbuf[c], 
			(const char*) buf[c], 
			sizeof(jack_default_audio_sample_t) * (count / frame_size)
		);
	}
	return count;
}

static int op_jack_buffer_space(void)
{
	int frame_size;
	int bytes;
	int frames;

	bytes = jack_ringbuffer_write_space(ringbuf[0]);
	frames = bytes / sizeof(jack_default_audio_sample_t);
	frame_size = sf_get_frame_size(sample_format);
	return frames * frame_size;
}

static int op_jack_pause(void) {
	paused = 1;
	return OP_ERROR_SUCCESS;
}

static int op_jack_unpause(void) {
	paused = 0;
	return OP_ERROR_SUCCESS;
}

static int op_jack_set_option(int key, const char *val) 
{
	d_print("set option %d = %s\n", key, val);

	switch (key) {
	case 0:
		if (cfg.server_name != NULL) {
			free(cfg.server_name);
			cfg.server_name = NULL;
		}
		if (val[0] != '\0') {
			cfg.server_name = xstrdup(val);
		}
	break;
	default:
		d_print("unknown uption %d\n", key);
		return -OP_ERROR_NOT_OPTION;

	}
	return OP_ERROR_SUCCESS;
}

static int op_jack_get_option(int key, char **val)
{
	switch (key) {
	case 0: 
		if (cfg.server_name == NULL) {
			*val = xstrdup("");
		} else {
			*val = xstrdup(cfg.server_name);
		}
		break;
	default:
		return -OP_ERROR_NOT_OPTION;
	}
	return OP_ERROR_SUCCESS; 
}

const struct output_plugin_ops op_pcm_ops = {
	.init = op_jack_init,
	.exit = op_jack_exit,
	.open = op_jack_open,
	.close = op_jack_close,
	.drop = op_jack_drop,
	.write = op_jack_write,
	.buffer_space = op_jack_buffer_space,
	.pause = op_jack_pause,
	.unpause = op_jack_unpause,
	.set_option = op_jack_set_option,
	.get_option = op_jack_get_option
};


const char * const op_pcm_options[] = {
	"server_name",
	NULL
};

const int op_priority = 99;
