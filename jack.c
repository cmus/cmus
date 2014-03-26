
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

#include "op.h"
#include "utils.h"
#include "debug.h"

#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>


#define CHANNELS 2
#define BUFFER 16384 

jack_client_t *client;
const char* client_name = "cmus";
const char* server_name = NULL;
jack_port_t *output_ports [CHANNELS];
jack_ringbuffer_t *output_buf [CHANNELS];

sample_format_t sample_format;
int connected = 0;
//XXX accessed from both threads
volatile int paused = 0;


static int process(jack_nframes_t nframes, void* arg)
{
	int i = 0, available;
	jack_default_audio_sample_t *out;
	available = jack_ringbuffer_read_space(output_buf[i]) / sizeof(float);
	if (available == 0) {
		return 0;
	}
	if (!paused) {
		//XXX copy data
		for (i = 0; i < CHANNELS; i++) {

			if (nframes > available) {
				d_print("buf %i underrun need %d got %d\n", i, nframes, available);	
			}
			out = jack_port_get_buffer (output_ports[i], nframes);
			memset(out, 0, sizeof(jack_default_audio_sample_t) * nframes);
			jack_ringbuffer_read(output_buf[i], (char*)out, nframes * sizeof(float));
		}
	} else {
		for (i = 0; i < CHANNELS; i++) {
			out = jack_port_get_buffer (output_ports[i], nframes);
			memset(out, 0, sizeof(jack_default_audio_sample_t) * nframes);
		}
	}

	return 0;
}

static int connect(void) {
	const char **ports;
	int i;

	if (connected) {
		return OP_ERROR_SUCCESS;
	}
	if (jack_activate(client)) {
		d_print("JACK: jack_client_activate failed\n");
		return -OP_ERROR_INTERNAL;
	}

	ports = jack_get_ports (client, NULL, NULL, JackPortIsPhysical|JackPortIsInput);
	if (ports == NULL) {
		d_print("JACK: cannot get playback ports\n");
		return -OP_ERROR_INTERNAL;
	}

	for (i = 0; i < CHANNELS; i++) {
		if (ports[i] == NULL) {
			d_print("could not connect output %d. to few playback ports.\n", i);
			break;
		}
		d_print("connect: %s -> %s\n", jack_port_name(output_ports[i]), ports[i]);

		if (jack_connect (client, jack_port_name (output_ports[i]), ports[i])) {
			d_print("JACK: connot connect port %s\n", ports[i]);
			free(ports);
			return -OP_ERROR_INTERNAL;
		}
	}
	free(ports);
	connected = 1;
	return OP_ERROR_SUCCESS;
}

static int op_jack_init(void)
{
	char buf[20];
	jack_options_t options = JackNullOption;
	jack_status_t status;
	int i;

	d_print("JACK: op_jack_init\n");

	for (i = 0; i < CHANNELS; i++) {
		output_buf[i] = jack_ringbuffer_create(BUFFER);
		if (output_buf[i] == NULL) {
			d_print("failed to create output buffer\n");
			while (--i >= 0) {
				jack_ringbuffer_free(output_buf[i]);
			}
			return -OP_ERROR_INTERNAL;
		}
	}


	client = jack_client_open(client_name, options, &status, server_name);
	if (client == NULL) {
		d_print("JACK: jack_client_new failed status = 0x%2.0x\n", status);
		return -OP_ERROR_INTERNAL;	
	}
	if (status & JackServerStarted) {
		d_print("JACK: jack_server started\n");
	}

	if (status & JackNameNotUnique) {
		client_name = jack_get_client_name(client);
		d_print ("unique name `%s' assigned\n", client_name);
	}
	jack_set_process_callback(client, process, NULL);

	for (i = 0; i < CHANNELS; i++) {
		snprintf(buf, sizeof(buf) - 1, "out %d", i);
		output_ports[i] = jack_port_register (
			client, 
			buf,
			JACK_DEFAULT_AUDIO_TYPE, 
			JackPortIsOutput, 
			0
		);
		if (output_ports[i] == NULL) {
			d_print("JACK: no jack ports available\n");
			return -OP_ERROR_INTERNAL;
		}
	}


	connect();

	d_print("JACK: init ok\n");
	return OP_ERROR_SUCCESS;
}

static int op_jack_exit(void) 
{
	int i;
	d_print("JACK: closing client\n");
	for (i = 0; i < CHANNELS; i++) {
		jack_ringbuffer_free(output_buf[i]);
	}
	jack_client_close(client);
	return OP_ERROR_SUCCESS;
}

static int op_jack_open(sample_format_t sf, const channel_position_t *channel_map)
{

	sample_format = sf;

	if (channel_map == NULL) {
		d_print("no channel_map\n");
	}

	d_print("big edian = %d\n", sf_get_bigendian(sf));
	d_print("channels = %d\n", sf_get_channels(sf));
	d_print("rate = %d\n", sf_get_rate(sf));
	d_print("signed = %d\n", sf_get_signed(sf));
	d_print("bits = %d\n", sf_get_bits(sf));
	d_print("frame_size = %d\n", sf_get_frame_size(sf));
	
	if (jack_get_sample_rate (client) != sf_get_rate(sf)) {
		d_print(
			"jack sample rate %d does not match %d\n", 
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

	if (sf_get_bits(sf) != 16) {
		d_print(
			"%d bits not supported\n", 
			sf_get_bits(sf)
		);
		return -OP_ERROR_SAMPLE_FORMAT;
	}

	if (sf_get_signed(sf) != 1) {
		d_print("unsigned not supported\n");
		return -OP_ERROR_SAMPLE_FORMAT;
	}

	paused = 0;
	return connect();
}

static int op_jack_close(void)
{
	//jack_deactivate(client);
	paused = 1;
	return OP_ERROR_SUCCESS;
}

static int op_jack_drop(void)
{
	return -OP_ERROR_NOT_SUPPORTED;
}

static inline float s16le_float(const char *buffer) {
	union c {
		uint16_t u;
		int16_t s;
	} c;

	c.u = read_le16(buffer);
	return ((float) c.s) / 32768.0f;
}

static int op_jack_write(const char *buffer, int count)
{
	int j, pos, frame_size;
	float buf[CHANNELS][BUFFER];

	frame_size = sf_get_frame_size(sample_format);

	
	pos = 0;
	for (pos = 0; pos < count; ) {
		for (j = 0; j < CHANNELS; j++)	{
			buf[j][pos / frame_size] = s16le_float(&buffer[pos + j * 2]);
		}
		pos += frame_size;
	}

	for (j = 0; j < CHANNELS; j++) {
		jack_ringbuffer_write(output_buf[j], (const char*) buf[j], sizeof(float) * (count / frame_size));
	}
	return count;
}

static int op_jack_buffer_space(void)
{
	int frame_size;
	frame_size = sf_get_frame_size(sample_format);
	return jack_ringbuffer_write_space(output_buf[0]) / sizeof(float) * frame_size;
}

static int op_jack_pause(void) {
	d_print("paused");
	paused = 1;
	return OP_ERROR_SUCCESS;
}

static int op_jack_unpause(void) {
	d_print("unpaused");
	paused = 0;
	return connect();
}

static int op_jack_set_option(int key, const char *val) 
{
	int n;
	d_print("JACK: set option %d = %s\n", key, val);

	n = strlen(val);
	switch (key) {
	case 0: 
		if (n > 0) {
			client_name = strdup(val);
		} else {
			client_name = strdup("cmus");
		}
	break;
	case 1:
		if (n > 0) {
			server_name = strdup(val);
		} else {
			server_name = NULL;
		}
	break;
	default:
		d_print("JACK: unknown uption %d\n", key);

	}
	return OP_ERROR_SUCCESS;
}

static int op_jack_get_option(int key, char **val)
{
	return -OP_ERROR_NOT_SUPPORTED;
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
	"client_name",
	"server_name",
	NULL
};

const int op_priority = 99;
