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
 * - resample to match server sample rate
 *    libsamplerate
 * - handle server changed sample rate
 * - configurable maping of channels to ports
 */
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <string.h>
#include <stdio.h>

#include "op.h"
#include "utils.h"
#include "channelmap.h"
#include "xmalloc.h"
#include "debug.h"


#define CHANNELS 2
#define BUFFER_MULTIPLYER (sizeof(jack_default_audio_sample_t) * 8)

struct {
	char* client_name;
	char* server_name;
} cfg;

jack_client_t *client = NULL;
jack_port_t *output_ports[CHANNELS];
jack_ringbuffer_t *ringbuffer[CHANNELS];
size_t buffer_pos[CHANNELS];
jack_nframes_t jack_sample_rate = 0;

size_t buffer_size = 0;
sample_format_t sample_format;
unsigned int sample_bytes = 0;
const channel_position_t* channel_map;
volatile int paused = 1;

/* fail on the next call */
int fail = 0;

/* function pointer to appropriate read function */
float (*read_sample) (const char* buff);

static int op_jack_init(void);
static int op_jack_exit(void);
static int op_jack_open(sample_format_t sf, const channel_position_t* cm);
static int op_jack_close(void);
static int op_jack_write(const char* buffer, int count);
static int op_jack_drop(void);
static int op_jack_buffer_space(void);
static int op_jack_set_option(const int key, const char* val);
static int op_jack_get_option(const int key, char** val);
static int op_jack_pause(void);
static int op_jack_unpause(void);


/* read functions for various sample formats */

static jack_default_audio_sample_t read_sample_le16(const char *buffer) 
{
	union c {
		uint16_t u;
		int16_t s;
	} c;
	jack_default_audio_sample_t res;

	c.u = read_le16(buffer);
	if (c.s > 0) {
		res = ((jack_default_audio_sample_t) c.s) 
			/ ((jack_default_audio_sample_t) INT16_MAX);
	} else {
		res = ((jack_default_audio_sample_t) c.s) 
			/ (- (jack_default_audio_sample_t) INT16_MIN);
	}
	return res;
}

static jack_default_audio_sample_t read_sample_le32(const char *buffer) 
{
	union c {
		uint32_t u;
		int32_t s;
	} c;
	jack_default_audio_sample_t res;

	c.u = read_le16(buffer);
	if (c.s > 0) {
		res = ((jack_default_audio_sample_t) c.s) 
			/ ((jack_default_audio_sample_t) INT32_MAX);
	} else {
		res = ((jack_default_audio_sample_t) c.s) 
			/ (- (jack_default_audio_sample_t) INT32_MIN);
	}
	return res;
}

static jack_default_audio_sample_t read_sample_le16u(const char *buffer) 
{
	jack_default_audio_sample_t res;
	uint32_t u;

	u = read_le16(buffer);
	res = (((jack_default_audio_sample_t) u) 
		/ ((jack_default_audio_sample_t) UINT16_MAX)) * 2.0 - 2.0;
	return res;
}

static jack_default_audio_sample_t read_sample_le32u(const char *buffer) 
{
	jack_default_audio_sample_t res;
	uint32_t u;

	u = read_le16(buffer);
	res = (((jack_default_audio_sample_t) u) 
		/ ((jack_default_audio_sample_t) UINT32_MAX)) * 2.0 - 2.0;
	return res;
}

/* jack callbacks */

static int op_jack_cb(jack_nframes_t frames, void* arg) 
{
	int i = 0;
	size_t bytes_read = 0;
	size_t bytes_want = 0;
	size_t fill_length = 0;
	char *fill_offset = NULL;
	jack_default_audio_sample_t *jack_buf;

	bytes_want = frames * sizeof(jack_default_audio_sample_t);

	if (!paused) {
		for (i = 0; i < CHANNELS; i++) {
			jack_buf = jack_port_get_buffer(output_ports[i], frames);

			bytes_read = jack_ringbuffer_read(
				ringbuffer[i], 
				(char*) jack_buf, 
				bytes_want
			);

			buffer_pos[i] += bytes_read;

			if (bytes_read < bytes_want) {
				d_print("channel %d underrun got %lu bytes, needed %lu\n", 
					i,
					bytes_read, 
					bytes_want
				);

				fill_length = bytes_want - bytes_read;
				fill_offset = ((char*) jack_buf) + bytes_read;
				memset(fill_offset, 0, fill_length);
			}
		}
	} else {
		for (i = 0; i < CHANNELS; i++) {
			jack_buf = jack_port_get_buffer(output_ports[i], frames);
			memset((char*) jack_buf, 0, bytes_want);
		}
	}

	return 0;
}

/* init or resize buffers if needed */
static int op_jack_buffer_init(jack_nframes_t samples, void* arg) 
{
	int i;
	jack_ringbuffer_t* new_buffer;
	char* tmp = NULL;
	size_t length;

	if (buffer_size > samples * BUFFER_MULTIPLYER) {
		/* we just don't shrink buffers, since this could result
		 * in gaps and they won't get that big anyway
		 */
		return 0;
	}
	buffer_size = samples * BUFFER_MULTIPLYER;
	d_print("new buffer size %lu\n", buffer_size);

	for (i = 0; i < CHANNELS; i++) {
		new_buffer = jack_ringbuffer_create(buffer_size);

		if (!new_buffer) {
			d_print("ringbuffer alloc failed\n");
			if (tmp != NULL) {
				free(tmp);
			}
			fail = 1;
			op_jack_exit();
			return 1;
		}
		if (ringbuffer[i] != NULL) {
			if (tmp == NULL) {
				tmp = xmalloc(buffer_size);
				if (!tmp) {
					d_print("xalloc failed\n");
					fail = 1;
					op_jack_exit();
					return 1;
				}
			}
			length = jack_ringbuffer_read_space(ringbuffer[i]);
			
			/* actualy this could both read/write less than length.
			 * In that case, which should not happen[TM], there will 
			 * be a gap in playback. 
			 */
			jack_ringbuffer_read(
				ringbuffer[i], 
				tmp, 
				length
			);
			jack_ringbuffer_write(
				new_buffer, 
				tmp, 
				length
			);

			jack_ringbuffer_free(ringbuffer[i]);
		}

		buffer_pos[i] = 0;
		ringbuffer[i] = new_buffer;
	}
	free(tmp);
	return 0;
}

static int op_jack_sample_rate_cb(jack_nframes_t samples, void* arg) 
{
	if (jack_sample_rate != samples) {
		//just fail for now
		fail = 1; 
		return 1;
	}
	return 0;
}

static void op_jack_shutdown_cb(void* arg) 
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
	const char** ports;
	char port_name[20];
	jack_options_t options = JackNullOption;
	jack_status_t status;
	size_t jack_buffer_size = 0;
	int i;

	if (fail) {
		/* since jackd failed, it will not be autostarted. Either jackd 
		 * was killed intentionaly or it died by heartattack.
		 * Until it is restarted, init will happily fail again 
		 * and again and again..
		 */
		options |= JackNoStartServer;
	}
	client = jack_client_open("cmus", options, &status, cfg.server_name);
	if (client == NULL) {
		d_print("jack_client_new failed status = 0x%2.0x\n", status);
		return -OP_ERROR_INTERNAL;	
	}
	if (status & JackServerStarted) {
		d_print("jackd started\n");
	}

	jack_buffer_size = jack_get_buffer_size(client);
	jack_sample_rate = jack_get_sample_rate(client);
	op_jack_buffer_init(jack_buffer_size, NULL);

	jack_set_process_callback(client, op_jack_cb, NULL);
	jack_set_sample_rate_callback(client, op_jack_sample_rate_cb, NULL);
	jack_set_buffer_size_callback(client, op_jack_buffer_init, NULL);
	jack_on_shutdown(client, op_jack_shutdown_cb, NULL);


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
			d_print("could not connect output %d. too few ports.\n", i);
			break;
		}
		if (jack_connect(client, jack_port_name(output_ports[i]), ports[i])) {
			d_print("connot connect port %s\n", ports[i]);
			free(ports);
			return -OP_ERROR_INTERNAL;
		}
	}
	free(ports);

	fail = 0;

	return OP_ERROR_SUCCESS;
}

static int op_jack_exit(void) 
{
	int i;
	for (i = 0; i < CHANNELS; i++) {
		if (ringbuffer[i] != NULL) {
			jack_ringbuffer_free(ringbuffer[i]);
		}
		ringbuffer[i] = NULL;
	}
	if (client != NULL) jack_client_close(client);
	buffer_size = 0;
	client = NULL;
	return OP_ERROR_SUCCESS;
}

static int op_jack_open(sample_format_t sf, const channel_position_t *cm) 
{
	int bits;
	sample_format = sf;

	if (fail) {
		//jack went away so lets see if we can recover
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

	if (jack_sample_rate != sf_get_rate(sf)) {
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
		sample_bytes = 2;

		if (sf_get_signed(sf)) {
			read_sample = &read_sample_le16;
		} else {
			read_sample = &read_sample_le16u;
		}
	} else if (bits == 32) {
		sample_bytes = 4;
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
	int i;
	for (i = 0; i < CHANNELS; i++) {
		jack_ringbuffer_reset(ringbuffer[i]);
	}
	return -OP_ERROR_NOT_SUPPORTED;
}

static int op_jack_write(const char *buffer, int count) 
{
	int idx;
	int c;
	int pos;
	int frame_size;
	int channels;
	int frame;
	int byte_length;
	size_t frames;
	size_t frames_min;
	size_t frames_available;
	jack_default_audio_sample_t buf[CHANNELS][buffer_size];

	if (fail) {
		op_jack_exit();
		return -OP_ERROR_INTERNAL;
	}

	frame_size = sf_get_frame_size(sample_format);
	channels = sf_get_channels(sample_format);

	frames = count / frame_size;

	/* since this is the only place where the ringbuffers get
	 * written, available space will only grow, therefore frames_min 
	 * is safe.
	 */
	frames_min = SIZE_MAX;
	for (c = 0; c < CHANNELS; c++) {
		frames_available = jack_ringbuffer_write_space(ringbuffer[c])
			/ sizeof(jack_default_audio_sample_t);
		if (frames_available < frames_min) {
			frames_min = frames_available;
		}
	}

	if (frames > frames_min) {
		frames = frames_min;
	}
	
	//demux and convert to float
	pos = 0;
	for (pos = 0; pos < count; ) {
		frame = pos / frame_size;
		for (c = 0; c < channels; c++)	{
			idx = pos + c * sample_bytes;
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

	byte_length = frames * sizeof(jack_default_audio_sample_t);
	for (c = 0; c < CHANNELS; c++) {
		jack_ringbuffer_write(
			ringbuffer[c], 
			(const char*) buf[c], 
			byte_length
		); 
	}
	return frames * frame_size;
}

static int op_jack_buffer_space(void) 
{
	int frame_size;
	int bytes;
	int frames;
	int tmp;
	int c;

	if (fail) {
		op_jack_exit();
		return -OP_ERROR_INTERNAL;
	}


	bytes = jack_ringbuffer_write_space(ringbuffer[0]);
	for (c = 1; c < CHANNELS; c++) {
		tmp = jack_ringbuffer_write_space(ringbuffer[0]);
		if (bytes > tmp) {
			bytes = tmp;
		}
	}
	frames = bytes / sizeof(jack_default_audio_sample_t);
	frame_size = sf_get_frame_size(sample_format);
	return frames * frame_size;
}

static int op_jack_pause(void) 
{
	paused = 1;
	return OP_ERROR_SUCCESS;
}

static int op_jack_unpause(void) 
{
	paused = 0;
	return OP_ERROR_SUCCESS;
}

static int op_jack_set_option(int key, const char *val) 
{
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
